# Native Debian on the TCL 30 SE: design

Date: 2026-11-10. Time box: 4 hours from approval.

## Change of direction (user, 2026-11-10)

Mainline first, Android's kernel as the fallback. Kernel: `TheKnightSky/linux-mt6765` branch `mt6765-devel` (7.1), board file `mt6762-tcl-cruze.dts` in `/mnt/1tb/android/linux/linux-mt6765`. Because this kernel is ours to configure, the systemd limits below apply only to the fallback; Debian on mainline uses normal systemd. Android's `.ko` files do not load on 7.1, so on mainline only firmware blobs can be reused. Cutoff: about three flash-and-read-crash-log cycles without a USB gadget, then fall back to the stock kernel and say so. If mainline hardware falls short, reverse-engineer that driver from Android's. The user wants Android deleted entirely except drivers; that is the last step, after Debian gives a USB shell, because Android reads the crash log (pstore) after a failed boot until then.

## Intent

Turn the phone into a portable Linux machine. The user wants to test the hardware first and move toward daily use later (what daily use requires is undecided). Debian replaces Android as the running system. Hardware support comes from the drivers Android already has on the phone.

## Success at the 4-hour mark

Must have: the phone boots Debian with no Android running, and the user gets a root shell from the laptop over the USB cable.

Then, in this order, as far as time allows:
1. Android's kernel modules load in Debian, and touch, keys, battery, backlight and vibrator are readable or controllable.
2. Something visible on the screen (emblem or status text).
3. Wi-Fi.

Reporting rule: say which rungs were reached and which were not. Do not call a rung done without observing it on the phone.

## Goals

- Debian arm64 as the only system in use; the whole 111 GB `userdata` partition becomes its root filesystem.
- Internet for the phone through the laptop over USB until Wi-Fi works.
- A hardware test script whose results are written down.

## Non-goals (this round)

GPU acceleration, a desktop, calls/SMS/modem, cameras, audio, GPS, Bluetooth, mainline kernel work, Arch.

## Anti-goals

- A phone that cannot be recovered. Never write `preloader`/`mmcblk0boot*`, `lk`, `nvram`, `nvdata`, `nvcfg`, `seccfg`, `proinfo`, `persist`, `md1img`, or `super`.
- Erasing `userdata` before Linux has proven it can boot and give a USB shell (M1).
- Claiming hardware works without checking it on the device.

## Facts this design relies on (checked 2026-11-10)

- Kernel: stock `4.19.188` binary, no source, cannot be rebuilt. Boot image header v2, page 2048, kernel 0x40080000, ramdisk 0x51b00000, tags/dtb 0x47880000, dtb in the image. Prior build args in `bpf/mkargs.txt`, tool `bpf/mkbootimg.py`.
- Kernel lacks `DEVTMPFS`, `FHANDLE`, `AUTOFS`, `VT`, `USER_NS`, `BINFMT_MISC`. So systemd and udev are out, and there is no kernel text console on the screen.
- Kernel has `BLK_DEV_INITRD`, `RD_GZIP`, ext4, f2fs, configfs gadget with ACM/NCM/RNDIS (UDC `musb-hdrc`), DM, evdev, TUN, ZRAM. `MODULE_SIG` is off.
- Firmware loader has no user helper; search path includes `/vendor/firmware`.
- `userdata` = `/dev/mmcblk0p61`, 111380758528 bytes. Currently encrypted f2fs, contents disposable (restore script recreates them).
- Modules: `/vendor/lib/modules/{wmt_drv,wmt_chrdev_wifi,wlan_drv_gen4m,connfem,...}.ko`; touch `/odm/ctp_ko/{ft8756,nt36525b_spi,nl9911c_tddi}.ko` (Input names `fts_ts`, `chipone-tddi`).
- Wi-Fi needs `/vendor/bin/wmt_loader` and `wmt_launcher -p /vendor/firmware/`, which are bionic binaries (`/system/bin/linker64`; libcutils, liblog, libc++, libc, libm, libdl).
- Display: `/dev/graphics/fb0` (mtkfb) and `/dev/dri/card0`. Backlight under `/sys/class/leds/lcd-backlight`.
- Known bootloader behaviour: `fastboot boot` crashes this lk, so every test is a flash. `boot_b`, `vbmeta_b`, `dtbo_b` exist; `system`/`vendor` have no `_b` but Linux does not need them.
- No microSD card. No documented button combo for fastboot. `mtkclient` is not installed.

## How it is built (user's decisions)

- Fully native in the sense the user chose: Android's kernel binary, no Android userspace running, drivers taken from Android.
- Android is fully replaced as the running OS, with `userdata` given to Debian.

## How it is built (Claude's calls; check these)

- **Debian over Arch.** Arch is tied to systemd, which this kernel cannot run. Debian stable arm64 with `sysvinit-core`, systemd excluded.
- **Device nodes:** `/dev` on tmpfs, populated by busybox `mdev`, since there is no devtmpfs or udev.
- **Boot chain:** new gzip ramdisk with static busybox as `/init`. Same kernel, dtb, and header values as today's boot image. It builds `/dev`, starts the USB gadget, mounts `/dev/mmcblk0p61` (ext4) and `switch_root`s to Debian. If the mount fails it stays in the ramdisk with the USB shell, so a broken root filesystem is still reachable.
- **Use slot B for Linux.** `boot_b` plus `vbmeta_b` (verification off), switched with `fastboot --set-active=b`. Slot A's boot and the `system_a` partition stay in place, unused, so going back is `--set-active=a` plus `-w`, not a 2 GB reflash. The user asked for full replacement; this keeps inert partitions only as the way back. Reclaiming them can come later.
- **Harvest instead of mounting vendor.** Copy `/vendor`, `/odm`, the bionic runtime and `/system/bin/linker64` off the running phone to the laptop, then into the Debian image at the same paths. No device-mapper handling of `super` is needed.
- **Root filesystem delivery:** build on the laptop with debootstrap plus `qemu-aarch64-static` (`debootstrap` must be installed; qemu is present), make a small ext4 image, `img2simg`, `fastboot flash userdata`, then `resize2fs` on first boot.
- **Access:** USB composite gadget with ACM (serial getty on `ttyGS0`) and NCM (SSH), key-based login from the laptop, phone internet via NAT on the laptop.
- **Screen:** a small program drawing to the framebuffer, not a terminal emulator.
- **Project location:** `~/Projects/tcl30se-gsi/debian/`.

## Order of work and gates

- **M0, before anything is written to the phone (about 20 min).** Harvest and check the copy on the laptop. The user confirms a button combination that reaches fastboot from a powered-off phone. Install `mtkclient` as a last-resort path, noting that BROM access on this phone is unproven. Gate: the harvest is verified and the button combo is known.
- **M1 (about 60 min).** Ramdisk only: flash `boot_b`, switch to B, get a shell over USB. `userdata` untouched; Android still reachable via slot A. Gate: shell seen from the laptop, and switching back to A has been shown to work.
- **M2 (about 45 min).** The point of no return for Android's data. Flash Debian to `userdata`, boot, SSH in, grow the filesystem. Gate: rung "must have" is met.
- **M3 (about 45 min).** Load modules, run the hardware test, write up results.
- **M4 (rest of the time).** Screen, then Wi-Fi. Note the unresolved receive problem seen on Android may follow, since the driver and firmware are the same.

## Risks

- lk may refuse slot B or may not fall back on failure. M1 tests this while Android is intact.
- Silent hang at boot with no display and no UART: the first ramdisk should be minimal, and should vibrate or set the backlight early as a sign of life.
- Bionic helpers may need Android properties or sockets that do not exist under Debian; Wi-Fi is the last rung for this reason.
- Kernel may misbehave without Android's init settings (for example, watchdog or thermal services). If the phone reboots after a fixed time, look at watchdog feeding first.

## Left to the builder

Exact package list, mdev rules, gadget IDs, the script layout, and the screen program's implementation.
