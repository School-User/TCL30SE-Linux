# Setup (experimental, one phone so far)

This is a record of how the TCL 30 SE (6165S, "cruze_pro", MT6765) runs Debian, not a one-click installer. Expect to read
`debian/HANDOFF.md` and `gpu/GPU-WRITEUP.md` while doing it. You need an unlocked bootloader and a full backup first
(`backup.sh`, `make backup`): the stock firmware is also where the vendor blobs come from.

## 1. Kernel
- Source: private repo TCL30SE-Linux-Kernel (branch `main`) = TheKnightSky/linux-mt6765 `mt6765-devel` + `kernel/0001`
  (device support), `kernel/0002` (MediaTek connectivity port), `kernel/0003` (USB host + dock hot-swap).
- Config: `kernel/config-7.1.0-mt6765nd`. Build: `debian/build-nodebug.sh`; boot image: `debian/mkboot.sh`
  (`O=... OUTIMG=...`); flash to boot_b only: `debian/deploy.sh` (checks the partition and md5 before rebooting).

## 2. Root filesystem
- Debian 13 arm64 on userdata; the initramfs (`debian/initramfs/init`) mounts it. A USB gadget gives SSH at 172.16.42.1 in
  device mode; Wi-Fi SSH once NetworkManager has a profile.

## 3. Out-of-tree modules and services (`debian/src/`)
- `casper-ion`, `casper-ashmem`, `casper-gpu-dt` + `pvrsrvkm` (GPU), `casper-vcodec` (video decoder), `casper-mt6765-dvfs`
  (CPU clocks), `mt6357-jack`. Build: `debian/build-modules-nd.sh`, `gpu/build-pvr-nd.sh`.
- Vendor blobs (PowerVR userspace, `rgx.fw`, Wi-Fi/BT firmware, decoder libraries) are NOT in this repo: extract them from
  your own stock `vendor`/`system` images.
- USB: `debian/src/casper-usb` (`casper-usb-boot.service`, `casper-usb-mode host|host-ext|device`, `casper-usb-menu`).

## 4. GPU userspace
- libhybris + the EGL platform plugin in `gpu/eglplatform-casper` (built on the phone with `build-on-phone.sh`), launchers
  `gpuwin` / `firefox-gpu`. Details and measurements: `gpu/GPU-WRITEUP.md`.

## 5. Desktop
- `desktop-deck/` (cyberdeck session), `desktop-xfce/` (XFCE on labwc, battery helpers), `pocketshell/` (submodule).
- Switch with `casper-desktop deck|xfce|pocketshell`.

## Known limits
Compositor is software (pixman); frames are CPU-copied to it. No PD negotiation, so USB-C docks only give USB + power in,
never HDMI. Battery % is a voltage estimate (no fuel gauge). USB mode changes need a reboot.
