/*
 * The vibration motor of the Redmi K20 Pro (raphael): an eccentric rotating mass (ERM) driven by the haptics block of the
 * PM8150B (SPMI slave 3, registers 0xc000; the device tree's pm8150b_haptics, "qcom,haptics", qcom,actuator-type "erm",
 * qcom,vmax-mv 3000). Register layout and the order of the writes: Xiaomi's drivers/input/misc/qti-haptics.c of an SM8150
 * kernel (qti_haptics_config_vmax(), _config_wf_src(), _module_en(), _play()).
 *
 * Nothing here is done unless a boot-arg asks for it:
 *
 *   haptic=1   a look, reading only: the arbiter's channel for the block, its type and registers
 *   haptic=2   ... and one buzz of haptic_ms (default 150, at most 1000), during start-up (no system needed)
 *   haptic=5   ... the motor for Darwin until the run ends: what Darwin plays on its "Actuator" device (the HAL plug-in,
 *              userland/src/audio_plugin.c) is written to /dev/hakios-haptic as samples of one channel; while they are not
 *              silence the motor runs, at most HAPTIC_MAX_MS at a time, after which it rests as long
 *
 * Limits, whatever asks: the drive voltage at most VMAX_MV (below the device tree's 3000 mV); the block's short-circuit flag,
 * a write that does not read back, a transfer that fails: the motor stops and nothing more is written for the run. At the
 * run's end (and on a panic's way out) it stops.
 *
 * The same device node and the same reading of Darwin's samples as drivers/haptic/hakios.haptic.c (redwood's Awinic chip):
 * a kernel has one of the two (CONFIG_HAKIOS_HAPTIC or CONFIG_HAKIOS_RAPHAEL_HAPTIC), never both.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include <sys/conf.h>
#include <sys/hakios.input.h>
#include <sys/systm.h>
#include <sys/uio.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern kern_return_t thread_terminate(thread_t thread);
extern void loader_log_putc(char c);

static uint32_t level;
static bool broken, leaving, playing, module_on;

static int say(const char *format, ...) __printflike(1, 2);

static int
say(const char *format, ...)
{
	char line[300];
	va_list list;
	int length;

	va_start(list, format);
	length = vsnprintf(line, sizeof(line), format, list);
	va_end(list);
	for (const char *c = line; *c != 0; c++) {
		loader_log_putc(*c);
	}
	if (!leaving && ml_get_interrupts_enabled()) {
		printf("%s", line);
	}
	return length;
}

static inline uint32_t
rd(vm_offset_t address)
{
	return *(volatile uint32_t *)address;
}

static inline void
wr(vm_offset_t address, uint32_t value)
{
	*(volatile uint32_t *)address = value;
}

static uint64_t
now_us(void)
{
	uint64_t ns;

	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / NSEC_PER_USEC;
}

static void
wait_ms(uint32_t ms)
{
	if (leaving) {
		uint64_t until = now_us() + ms * 1000ull;

		while (now_us() < until) {
		}
	} else {
		delay_for_interval(ms, NSEC_PER_MSEC);
	}
}

/* ---- the PMIC arbiter (version 5), as drivers/camera/hakios.torch.c uses it -------------------------------------------- */

#define ARB_CORE_PHYS           0x0c440000UL
#define ARB_CORE_SIZE           0x1100UL
#define ARB_CNFG_PHYS           0x0c40a000UL
#define ARB_CNFG_SIZE           0x1000UL
#define ARB_OBSERVER_PHYS       0x0e600000UL
#define ARB_OBSERVER_SIZE       0x10000UL
#define ARB_CHNLS_PHYS          0x0c600000UL
#define ARB_SYSTEM              0
#define ARB_VERSION             0x000
#define ARB_VERSION_5           0x50000000u
#define ARB_VERSION_7           0x70000000u
#define ARB_FEATURES            0x004
#define ARB_CHANNEL(n)          (0x900 + 4 * (n))
#define ARB_OWNER(n)            (0x700 + 4 * (n))
#define ARB_MAX_CHANNELS        512
#define CHANNEL_COMMAND         0x00
#define CHANNEL_STATUS          0x08
#define CHANNEL_WDATA0          0x10
#define OBSERVER_DATA0          0x18
#define OBSERVER_DATA1          0x1c
#define STATUS_DONE             0x1
#define STATUS_FAILURE          0x2
#define STATUS_DENIED           0x4
#define STATUS_DROPPED          0x8
#define COMMAND_WRITE           (0u << 27)
#define COMMAND_READ            (1u << 27)
#define TRANSFER_TIMEOUT_US     2000
#define DROPPED                 "the transfer was dropped"

#define HAPTICS_ID              0x3c0           /* slave 3, peripheral 0xc0 */

static vm_offset_t arb_core, arb_cnfg, arb_observer, hap_write;
static uint16_t hap_channel;
static bool hap_found, hap_writable;

static void
lock(void)
{
	if (!leaving) {
		hakios_spmi_lock();
	}
}

static void
unlock(void)
{
	if (!leaving) {
		hakios_spmi_unlock();
	}
}

static const char *
wait_done(vm_offset_t channel)
{
	uint64_t start = now_us();
	uint32_t status;

	do {
		status = rd(channel + CHANNEL_STATUS);
	} while (!(status & STATUS_DONE) && now_us() - start < TRANSFER_TIMEOUT_US);
	return !(status & STATUS_DONE) ? "no answer from the arbiter" : (status & STATUS_DENIED) ? "the arbiter refuses" :
	       (status & STATUS_FAILURE) ? "the transfer failed" : (status & STATUS_DROPPED) ? DROPPED : NULL;
}

static const char *
spmi_read(uint8_t reg, uint8_t *bytes, uint32_t count)
{
	vm_offset_t channel = arb_observer + 0x10000UL * ARB_SYSTEM + 0x80UL * hap_channel;
	const char *problem = NULL;
	uint32_t data[2] = { 0, 0 };

	if (!hap_found) {
		return "no channel";
	}
	for (int tries = 0; tries < 3; tries++) {
		lock();
		wr(channel + CHANNEL_COMMAND, COMMAND_READ | (uint32_t)reg << 4 | (count - 1));
		problem = wait_done(channel);
		data[0] = rd(channel + OBSERVER_DATA0);
		data[1] = count > 4 ? rd(channel + OBSERVER_DATA1) : 0;
		unlock();
		if (problem == NULL || strcmp(problem, DROPPED) != 0) {
			break;
		}
	}
	if (problem != NULL) {
		return problem;
	}
	for (uint32_t i = 0; i < count; i++) {
		bytes[i] = (uint8_t)(data[i / 4] >> (8 * (i % 4)));
	}
	return NULL;
}

/* ---- the haptics block ----------------------------------------------------------------------------------------------- */

#define HAP_TYPE                0x04
#define HAP_SUBTYPE             0x05
#define HAP_STATUS1             0x0a            /* [3] short circuit, [1] busy */
#define  STATUS1_SC             0x08
#define HAP_EN_CTL1             0x46            /* [7] the block on */
#define HAP_SEL                 0x4e            /* [5:4] the waveform's source: 0 a constant drive of VMAX */
#define HAP_VMAX_CFG            0x51            /* [5:1] the drive voltage, 116 mV a step */
#define HAP_PLAY                0x70            /* [7] play */

#define VMAX_MV                 2552            /* 22 steps of 116 mV: under the device tree's 3000 */
#define VMAX_CODE               ((VMAX_MV / 116) << 1)
#define HAPTIC_MAX_MS           1000

static const uint8_t writable_regs[] = { HAP_EN_CTL1, HAP_SEL, HAP_VMAX_CFG, HAP_PLAY };
static bool forcing;            /* (while the motor is being stopped: the stop's writes pass the `broken' latch) */

static void
give_up(const char *why)
{
	if (!broken) {
		broken = true;
		say("haptic: %s: no more writing to the haptics block for the rest of the run\n", why);
	}
}

static bool
put(uint8_t reg, uint8_t value)
{
	const char *problem = NULL;
	uint8_t back = 0xff;
	bool allowed = false;

	for (size_t i = 0; i < sizeof(writable_regs); i++) {
		allowed |= writable_regs[i] == reg;
	}
	if ((broken && !forcing) || !allowed || !hap_writable || hap_write == 0) {
		give_up(allowed ? "a write that may not be made" : "a register that is not to be written");
		return false;
	}
	if (((rd(arb_core + ARB_CHANNEL(hap_channel)) >> 8) & 0xfff) != HAPTICS_ID) {
		give_up("the arbiter's channel is no longer the haptics block's");
		return false;
	}
	for (int tries = 0; tries < 3; tries++) {
		lock();
		wr(hap_write + CHANNEL_WDATA0, value);
		wr(hap_write + CHANNEL_COMMAND, COMMAND_WRITE | (uint32_t)reg << 4);
		problem = wait_done(hap_write);
		unlock();
		if (problem == NULL || strcmp(problem, DROPPED) != 0) {
			break;
		}
	}
	/* (the play register reads back as it pleases once the play is over: only that it was accepted counts there) */
	if (problem != NULL || (reg != HAP_PLAY && (problem = spmi_read(reg, &back, 1)) != NULL)) {
		say("haptic: %#04x <- %#04x: %s\n", reg, value, problem);
		give_up("a write that failed");
		return false;
	}
	if (reg != HAP_PLAY && back != value) {
		say("haptic: %#04x <- %#04x, but it reads %#04x\n", reg, value, back);
		give_up("a register that did not read back as written");
		return false;
	}
	return true;
}

static bool
put_bits(uint8_t reg, uint8_t mask, uint8_t value)
{
	uint8_t now;

	if (spmi_read(reg, &now, 1) != NULL) {
		give_up("a read that failed");
		return false;
	}
	return (now & mask) == (value & mask) || put(reg, (uint8_t)((now & ~mask) | (value & mask)));
}

static bool
short_circuit(void)
{
	uint8_t status = 0;

	if (spmi_read(HAP_STATUS1, &status, 1) != NULL) {
		return true;
	}
	return (status & STATUS1_SC) != 0;
}

/* the motor on: the drive voltage, a constant source, the block on, play (qti-haptics' order) */
static bool
motor_on(void)
{
	if (broken || short_circuit()) {
		give_up("the block says short circuit (or cannot be read)");
		return false;
	}
	if (!put_bits(HAP_VMAX_CFG, 0x3e, VMAX_CODE) || !put_bits(HAP_SEL, 0x31, 0x00)) {
		return false;
	}
	module_on = true;
	if (!put(HAP_EN_CTL1, 0x80)) {
		return false;
	}
	playing = true;
	return put(HAP_PLAY, 0x80);
}

/* the motor off: play off, then the block off; both tried whatever went before */
static bool
motor_off(void)
{
	bool ok;

	forcing = true;
	ok = put(HAP_PLAY, 0x00);
	wait_ms(1);
	ok = put(HAP_EN_CTL1, 0x00) && ok;
	forcing = false;
	if (ok) {
		playing = module_on = false;
	}
	return ok;
}

static void
show_registers(void)
{
	uint8_t r[8];
	char line[200];

	for (unsigned base = 0; base < 0x100; base += 0x20) {
		int n = snprintf(line, sizeof(line), "haptic: %#04x:", base);

		for (unsigned i = 0; i < 0x20; i += 8) {
			if (spmi_read((uint8_t)(base + i), r, 8) != NULL) {
				n += snprintf(line + n, sizeof(line) - (size_t)n, " (unreadable)");
				continue;
			}
			for (unsigned j = 0; j < 8; j++) {
				n += snprintf(line + n, sizeof(line) - (size_t)n, " %02x", r[j]);
			}
		}
		say("%s\n", line);
	}
}

/* ---- Darwin's samples (/dev/hakios-haptic), read as drivers/haptic/hakios.haptic.c reads them ------------------------- */

#define ACTIVE_US               30000           /* a write that was not silence counts for this long */
#define SILENT_BELOW            256             /* of 32768 */

static volatile uint64_t heard_us, quiet_us;

static void
heard(uint32_t peak)
{
	if (peak > SILENT_BELOW) {
		heard_us = now_us();
	} else {
		quiet_us = now_us();
	}
}

void
hakios_haptic_pretend(uint32_t peak)
{
	heard(peak);
}

static int
haptic_write(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	int16_t samples[256];
	int error = 0;

	while (uio_resid(uio) >= 2) {
		user_ssize_t bytes = uio_resid(uio);
		uint32_t peak = 0;

		if (bytes > (user_ssize_t)sizeof(samples)) {
			bytes = sizeof(samples);
		}
		bytes &= ~(user_ssize_t)1;
		if ((error = uiomove((caddr_t)samples, (int)bytes, uio)) != 0) {
			break;
		}
		for (unsigned i = 0; i < bytes / 2; i++) {
			uint32_t v = samples[i] < 0 ? (uint32_t)-(int32_t)samples[i] : (uint32_t)samples[i];

			peak = v > peak ? v : peak;
		}
		heard(peak);
	}
	return error;
}

static int
haptic_open(__unused dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	return 0;
}

static const struct cdevsw haptic_cdevsw = {
	.d_open = haptic_open, .d_close = haptic_open, .d_read = eno_rdwrt, .d_write = haptic_write,
	.d_ioctl = eno_ioctl, .d_stop = eno_stop, .d_reset = eno_reset, .d_select = eno_select,
	.d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0
};

static void
serve(void)
{
	int major = cdevsw_add(-1, &haptic_cdevsw);
	uint64_t on_since = 0, resting_until = 0;
	uint32_t buzzes = 0, cut = 0;

	if (major < 0 || devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "hakios-haptic") == NULL) {
		say("haptic: /dev/hakios-haptic cannot be made\n");
		return;
	}
	say("haptic: THE MOTOR IS DARWIN'S (/dev/hakios-haptic) until the run ends: %u mV, at most %u ms at a time\n", VMAX_MV,
	    HAPTIC_MAX_MS);
	while (!broken) {
		uint64_t now = now_us();
		bool want = heard_us != 0 && now - heard_us < ACTIVE_US && now >= resting_until;

		if (want && !playing) {
			if (!motor_on()) {
				break;
			}
			on_since = now;
			buzzes++;
		} else if (playing && (!want || now - on_since >= HAPTIC_MAX_MS * 1000ull)) {
			if (want) {
				cut++;
				resting_until = now + (now - on_since);
			}
			if (!motor_off()) {
				break;
			}
		}
		if (playing && short_circuit()) {
			(void)motor_off();
			give_up("short circuit while it ran");
			break;
		}
		wait_ms(5);
	}
	if (playing || module_on) {
		(void)motor_off();
	}
	say("haptic: the motor is given up on (%u buzzes, %u cut at %u ms)\n", buzzes, cut, HAPTIC_MAX_MS);
}

/* ---- the start ------------------------------------------------------------------------------------------------------- */

static void
haptic_work(void)
{
	uint32_t version, channels;
	uint8_t id[2] = { 0, 0 };

	say("haptic: level %u: %s\n", level, level == 1 ? "a look, reading only" : level == 2 ? "one buzz" : "the motor for Darwin");
	arb_core = ml_io_map(ARB_CORE_PHYS, ARB_CORE_SIZE);
	arb_cnfg = ml_io_map(ARB_CNFG_PHYS, ARB_CNFG_SIZE);
	arb_observer = ml_io_map(ARB_OBSERVER_PHYS, ARB_OBSERVER_SIZE);
	version = rd(arb_core + ARB_VERSION);
	channels = rd(arb_core + ARB_FEATURES) & 0x7ff;
	if (version < ARB_VERSION_5 || version >= ARB_VERSION_7) {
		say("haptic: PMIC arbiter version %#x, not 5: nothing done\n", version);
		return;
	}
	if (channels == 0 || channels > ARB_MAX_CHANNELS) {
		channels = ARB_MAX_CHANNELS;
	}
	for (uint32_t n = 0; n < channels; n++) {
		uint32_t map = rd(arb_core + ARB_CHANNEL(n)), owner;

		if (map == 0 || ((map >> 8) & 0xfff) != HAPTICS_ID) {
			continue;
		}
		owner = rd(arb_cnfg + ARB_OWNER(n)) & 7;
		say("haptic: channel %u is the PM8150B's haptics block (%#x); written by system %u%s\n", n, map, owner,
		    owner == ARB_SYSTEM ? ", which is us" : "");
		if (!hap_found || owner == ARB_SYSTEM) {
			hap_found = true;
			hap_channel = (uint16_t)n;
			hap_writable = owner == ARB_SYSTEM;
		}
	}
	if (!hap_found || spmi_read(HAP_TYPE, id, 2) != NULL) {
		say("haptic: the haptics block cannot be read\n");
		return;
	}
	say("haptic: the block says type %#x, subtype %#x\n", id[0], id[1]);
	show_registers();
	if (level < 2) {
		return;
	}
	if (!hap_writable) {
		say("haptic: the channel is not ours to write through: nothing written\n");
		return;
	}
	hap_write = ml_io_map(ARB_CHNLS_PHYS + 0x10000UL * hap_channel, 0x1000);
	if (level == 2) {
		uint32_t ms = 150;

		PE_parse_boot_argn("haptic_ms", &ms, sizeof(ms));
		ms = ms < 20 ? 20 : ms > HAPTIC_MAX_MS ? HAPTIC_MAX_MS : ms;
		say("haptic: one buzz of %u ms at %u mV\n", ms, VMAX_MV);
		if (motor_on()) {
			uint8_t status = 0;

			wait_ms(ms);
			(void)spmi_read(HAP_STATUS1, &status, 1);
			say("haptic: status while it ran: %#x%s\n", status, (status & STATUS1_SC) ? " (SHORT CIRCUIT)" : "");
		}
		say("haptic: %s\n", motor_off() ? "stopped" : "COULD NOT BE STOPPED");
		show_registers();
	} else {
		serve();
	}
}

static void
haptic_thread(__unused void *parameter, __unused wait_result_t result)
{
	haptic_work();
	say("haptic: done\n");
	thread_terminate(current_thread());
}

void
hakios_haptic_init(void)
{
	thread_t thread;

	if (!hakios_is_raphael() || !PE_parse_boot_argn("haptic", &level, sizeof(level)) || level == 0) {
		return;
	}
	level = level >= 5 ? 5 : level > 2 ? 2 : level;
	if (level < 5) {
		haptic_work();          /* (the look and the one buzz during start-up: they need no system) */
		say("haptic: done\n");
		return;
	}
	if (kernel_thread_start(haptic_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}

/* At the run's end and on a panic's way out: the motor stopped, if it was ever started. */
void
hakios_haptic_quiesce(void)
{
	if (hap_write == 0 || !(playing || module_on)) {
		return;
	}
	leaving = true;
	broken = false;                 /* (one last time, whatever went before) */
	say("haptic: at the run's end: the motor %s\n", motor_off() ? "stopped" : "COULD NOT BE STOPPED");
}
