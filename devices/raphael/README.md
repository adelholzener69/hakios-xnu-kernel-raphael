# Redmi K20 Pro / Mi 9T Pro (`raphael`)

HakiOS on the Xiaomi **Redmi K20 Pro** (Mi 9T Pro), Snapdragon 855 (SM8150). It boots **from RAM**: nothing is
installed, your Android stays untouched, and a reboot brings it back.

> Research project, not a ROM. Apple's services (Apple ID, iCloud, App Store) do not work.

## What works

| Part | State | Notes |
|---|---|---|
| Screen | works | brightness slider drives the panel itself |
| Touch, side buttons | works | |
| CPU | works | 7 of 8 cores, frequency scaling |
| GPU | works | Adreno 640, OpenGL ES |
| Battery level | works | |
| Wi-Fi | **partly** | WPA2 and WPA3 join and load pages. Joining is slow, and switching Wi-Fi off and on in Settings can leave the network list empty for a while |
| Sound | works | loudspeaker only (add `adsp_step=12 adsp_keep=1 adsp_pause=500 tfa=1 tfa_tx=1 ios_audio=1` to the boot arguments; needs `fw-audio/` from your phone). The amplifier powers down after 30 s of silence and wakes when a sound comes |
| Bluetooth, mobile data, camera, haptics, torch | not yet | |
| Saved storage | no | everything lives in RAM and is gone after a reboot |

## What you need

- A Redmi K20 Pro with an **unlocked bootloader** (this is a fully supported, official Xiaomi step).
- A Linux PC with `fastboot`, `adb` and a USB cable. Use a port on the back of the PC, no hub.
- Your phone's own firmware files. They are copied from **your** phone and are never part of this repository.
- A copy of the iOS 16.5 files for the userland (see the main [README](../../README.md) and `docs/`).

## Step by step

**1. Copy your phone's firmware** (phone in TWRP, read-only):

```sh
devices/raphael/kit/pull-firmware.sh      # makes fw-raphael/ and fw-audio/ (private: never share or commit them)
```

`fw-raphael/` is git-ignored, so a `git clean` or a fresh clone **deletes it**. If Wi-Fi suddenly stops working,
check that the folder is still there and run the script again.

**2. Build the kernel:**

```sh
make raphael_defconfig
python3 tools/mk-ath10k.py                                   # the Wi-Fi driver object
WIFI_OBJECT="$PWD/xnu-deps/build/ath10k/out/ath10k.o" make kernel
```

**3. Boot it** (phone in the bootloader: hold Power + Volume Down):

```sh
fastboot boot <your boot image>          # the loader; build it with devices/raphael/kit/build-uefi-image.sh
./xl ramdisk <the big image>             # about 100 s: do not touch the cable
XL_KERNEL=hakios-kernel ./xl run
```

After about a minute and a half you should see the iOS home screen. Connect to Wi-Fi in Settings and give it a
minute to list networks.

> The image and boot-image steps were last run in the earlier tree this port came from, and have not yet been
> re-run end to end here. The kernel build above, and booting that kernel to the home screen, have been.

## Safety

- It runs from RAM. **Never flash `xbl` or `abl`**: an anti-rollback fuse can turn the phone into a brick.
- Use the **rear USB port**, no hub: a flaky connection is the usual reason an upload fails.
- After a run ends, put the phone back into the bootloader and start from step 3. Reusing the loader is not supported.
- Keep your `fw-*/` folders private. They contain data from your phone.

## Troubleshooting

| Symptom | Try |
|---|---|
| No Wi-Fi at all | Is `fw-raphael/` still there? Was the image built with it (`MPSS_FW=$PWD/fw-raphael`)? |
| Network list empty after toggling Wi-Fi | Wait ~20 s, or toggle again; this is a known bug |
| Upload fails or the phone vanishes | Rear USB port, shorter cable, no hub; reboot into the bootloader and retry |
| Screen stays on the vendor logo | Check you used the loader built for your firmware (`kit/build-uefi-image.sh`) |

## How this directory is laid out

| Path | What |
|---|---|
| `device.conf` | what works, as seen on this phone (also shown on the documentation's device page) |
| `configs/` | the kernel configuration: which drivers are built (`raphael.config`) |
| `soc/` | the Snapdragon 855 family layer |
| `kit/` | firmware copy script, device-tree and UEFI-image builders |
| `uefi/` | the phone's UEFI port |
| `build.conf`, `raphael.jobs` | settings and start-up jobs for the RAM image |
| `README.port-notes.md` | the earlier port's notes, kept for reference |

The raphael-only drivers live in `drivers/` next to the others: `hakios.raphael_touch.c` (touch),
`hakios.raphael_{amp,mi2s,tfa}.c` (loudspeaker), `hakios.display.c` and `hakios.brightness.c`.

## Credits

Based on Helium-Studio's `raphael-bringup` work; the touch, GPU display path, Wi-Fi, sound and brightness work
continues it. NXP's TFA98xx amplifier code (Apache-2.0) is included under `bsd/dev/tfa98xx/`.
