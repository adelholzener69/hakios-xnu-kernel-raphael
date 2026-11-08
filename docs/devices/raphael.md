<!-- Made by tools/docs-gen.py from devices/raphael/device.conf. Do not edit: change that, then run tools/docs-gen.py. -->


# Redmi K20 Pro / Mi 9T Pro (`raphael`)

| | |
|---|---|
| Vendor | Xiaomi |
| SoC | `sm8150` |
| Configuration | `make raphael_defconfig` |
| Maintainer | adelholzener69 |
| Repository | [`devices/raphael/`](https://github.com/oosexgang/hakios-xnu-kernel/tree/main/devices/raphael) |
| Firmware | `devices/raphael/kit/pull-firmware.sh` pulls it from your own phone |

## What works

| Part | State | |
|---|---|---|
| display | works | UEFI's framebuffer through the loader, panel brightness over DCS (brightness=1 brightness_dcs=1) |
| touch | works | touchscreen=1 |
| buttons | works | buttons=1 |
| cpu | works | 7 of 8 cores, cpufreq=1 |
| gpu | works | Adreno 640 through GLES (gpu=1); needs fw-gpu/ |
| audio | works | loudspeaker only, TFA9874 amplifier (adsp_step=12 tfa=1 tfa_tx=1 ios_audio=1); needs fw-audio/ with mono.cnt |
| battery | works | level through the loader (battery=1) |
| wifi | partly | WCN3990 / ath10k SNOC: WPA2 and WPA3 join and load pages; slow to join, off/on can leave the scan empty (wifi_step=15 ...) |
| storage | no | the RAM image only; nothing is saved |
| cellular | no |  |
| bluetooth | no |  |
| haptic | works | the PM8150B haptics block driving the ERM motor, from iOS (haptic=5) |
| torch | works | Control Center flashlight with dimming, PM8150L flash LED at most 100 mA (torch=3) |
| camera | no |  |

The state is what has been seen on this device, written down in `devices/raphael/device.conf`; a part the table does not name has not been looked at.
