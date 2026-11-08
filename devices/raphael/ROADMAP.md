# Redmi K20 Pro: what is left for a daily driver

Legend: **[x]** works, **[~]** works partly, **[ ]** not done. Notes say what was seen on the phone (8 November 2026).
The order is roughly the order in which things matter. See [README.md](README.md) for how to build and run.

## A. To use it as your phone

- [ ] **Starts without a PC.** Today the loader boots from RAM and a PC uploads about 2.5 GB over USB every time.
      Plan: put the loader and system on the phone's own flash, after a full backup (`tools/backup-partitions.py`).
      Never write `xbl` or `abl`.
- [x] **Screen, touch, buttons, brightness slider, battery level.**
- [x] **Saved settings, apps and Wi-Fi networks** across restarts (`/var` kept in the `darwin` partition,
      `ufs=2 ufs_native=1 ufs_write=1 var_store=1`). Needs the partition table change described in
      `tools/userdata-carve.py`.
- [ ] **Saved storage hardened.** Untested: power cut in the middle of a save, speed under load, UFS power saving.
      (A save writes the other of two slots first, so the last good copy should survive; not yet tried on the phone.)
- [~] **Wi-Fi.** Joins WPA2 and WPA3. Open: slow join; a freshly restarted radio hears nothing for a few seconds (the
      driver now asks again); no automatic rejoin by the kernel after a toggle or reboot.
- [~] **Sound.** Loudspeaker works, the amplifier sleeps after 30 s and wakes. Open: ringtone choice and preview, the
      tri-tone and SMS sound IDs (1007, 1315), a very quiet keyboard click, and confirmation by daily use that the
      `/var` restore no longer makes the audio server refuse playback.
- [ ] **Mobile data, calls, SMS.** The modem driver is left out of the build. Largest item.
- [ ] **Charging while it runs.** Battery level works; charging under this kernel is not supported yet.
- [ ] **Power menu, shutdown, reboot from the phone.**

## B. Expected on a phone

- [ ] **Microphone** (the audio DSP's mic path exists in the repo, not enabled here).
- [ ] **Bluetooth.** WCN3990/3998 on UART serial engine 13; its firmware is in the phone's `bluetooth` partition.
      Stage 1: kernel brings the chip up and scans. Stage 2: a `/dev/cu.bluetooth` HCI device. Stage 3: make
      `bluetoothd`/`BlueTool` (written for Broadcom chips) accept it. No iOS-side code exists yet.
- [ ] **Sensor DSP (SLPI)** for auto-brightness, auto-rotate and proximity. Firmware: `slpi_rp.*` in the modem image.
- [ ] **Torch** (PM8150L flash LED, reference: `leds-qpnp-flash-v2.c` of an SM8150 kernel).
- [ ] **Haptics** (ERM motor on the PM8150B).
- [ ] **Camera.**
- [ ] **Fingerprint** (in-display). Not researched.
- [ ] **Thermal protection** (the thermal driver is off in the config).
- [ ] **GPS / location, USB behaviour.** Not looked at.

## C. Comfort

- [ ] Faster boot (about 3 minutes to a usable home screen).
- [ ] Better Wi-Fi range (the nearest 2.4 GHz access point shows at about -77 dBm).
- [ ] Memory (about 320-380 MB free under load).
- [ ] This device's page in the documentation and the merge into upstream.

## D. Optional: third-party software

- [~] **Package manager** (Procursus + Sileo under `/var/jb`): exists upstream as the `JB=1` image option, not tried on
      this phone yet. See the README. No jailbreak exploit is needed: this kernel does not enforce code signing.

## E. Newer iOS

- [ ] **iOS 17/18/26.** Research item, see the README ("Newer iOS versions"). Today: iOS 16.5 on XNU 8796.

## Not goals

Apple ID, iCloud, the App Store and other Apple services.
