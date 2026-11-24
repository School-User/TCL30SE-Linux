# TCL 30 SE (6165S) — modded GSI + Linux: project state

Last updated: 2026-11-07 by casper-71. The other session (casper-7e) appends its own section at the bottom.
No secrets in this file.

## Goal
1. Make the Android A15 GSI on the TCL 30 SE (MT6762, Helio G25, kernel 4.19.188) run faster, with Lawnchair as the home launcher, bloat removed, and working gestures (swipe up for home/recents, back).
2. Later: Linux next to Android (chroot first), then a dual-boot / postmarketOS-style Linux, which needs our own kernel port. NOT started. The user said not to add Linux support yet.

## Device facts (see also notes/device-facts.md)
- 6165S / Cruze_Pro, bootloader unlocked, A/B virtual-A/B with dynamic partitions, vendor frozen at Android 12 (VNDK 31).
- Currently installed: ponces `aosp-arm64-ab-vanilla-15.0-20241108` + Magisk boot with the BPF fix (`bpf/boot_a.bpffix.img`; any new boot.img must keep that patch).
- Root: `adb shell /debug_ramdisk/su -c ...` (plain `su` is the MTK vendor su).
- Rules the user set: **no test reboots without asking** (reboots with USB in can hang in off-mode charging; unplug USB before any boot). Don't flash for real without an OK.

## Backups (all verified)
- Stock Android 12 full dump (every partition except userdata): `/mnt/1tb/tcl30se-backup/backup/` (31 GB, sha256 re-checked 2026-11-19). `~/Projects/tcl30se-gsi/backup` is now a symlink to it (local copy removed 2026-11-19 to free space), so the 1TB drive must be mounted. Restore with `make rollback` / `make rollback-hard`.
- Original GSI images: `~/Projects/tcl30se-gsi/images/` (A15 20241108 is the base; A14 20231108 is the older fallback).

## What we built (2026-11-06/07)
Workspace: `/mnt/1tb/android/workspace/` (gsi/ = working copies and `mods/apply*.sh`; out/ = finished images).
- `out/system-mod1.img` (+ `.raw.gz`): gesture-first image.
  - Lawnchair 15 Beta 3 baked in as a priv-app in `/system_ext/priv-app/Lawnchair` plus a privapp-permissions XML. Launcher3/Trebuchet and its overlay removed.
  - Static RRO `GsiMod_recents.apk` sets `config_recentsComponentName` to `app.lawnchair/com.android.quickstep.RecentsActivity`. This is what makes SystemUI's OverviewProxy connect, so gestures work. TCL's vendor overlay pointed it at a launcher that isn't on the GSI, which is why gestures were dead.
  - Static RRO `GsiMod_anim.apk` sets window/transition animation scale to 50%.
  - About 45 packages removed: print stack, DeviceAsWebcam, MusicFX, Music/Calendar/PhotoTable/Browser2, lock-screen clock plugins, all font/icon-pack/cutout-emulation overlays, and more. Camera2, Dialer, Contacts, Settings, TrebleApp stay.
- `out/system-mod2.img`: mod1 plus `GsiMod_nav3.apk` (3-button navigation default). Fallback if gestures ever misbehave. Not tested.
- Scripts: `notes/gsi-mod1-apply.sh`, `notes/gsi-mod2-apply.sh`; `dsu-test.sh` (push/start/status/wipe helper for DSU tests).

## Test result
- DSU test of mod1 (2026-11-07, second attempt) booted fine, and the user confirmed gestures work. SystemUI `OverviewProxyService` showed `isConnected=true`.
- Caveat: Lawnchair's `TouchInteractionService` crashed twice at boot with `NoSuchMethodError ActivityTaskManager.getInstance()` (Lawnchair is built against a newer SDK than this A15), then stayed connected. Watch for gestures dying after reboot or a long idle. Not fixed because it works.
- Ogesture does NOT work for the user. Don't use it as a fallback.
- **Not flashed for real.** The phone is running mod1 in a DSU session (separate 8 GB userdata, temporary). `make flash` would wipe userdata. The user wants to use it for a while first.

## Gotchas learned
- NEVER `zipalign` an already-signed APK. It destroys the v2 signature and the priv-app silently fails to scan, so the phone falls back to the bare `FallbackHome` with no launcher. The first DSU test failed exactly this way.
- Files added to a mounted GSI image have no SELinux label, so set `setfattr -n security.selinux -v u:object_r:system_file:s0` on every new file or dir. Delete `.idsig` files that apksigner leaves behind.
- The GSI ext4 has `shared_blocks`. Grow the file first (`truncate -s +1G`, `resize2fs`), then `e2fsck -fy -E unshare_blocks`, then it mounts read-write. Shrink with `resize2fs -M` afterwards.
- RROs: aapt2 compile/link, zipalign, then sign (order matters), static, priority 999.
- DSU install: `./dsu-test.sh start <file-on-phone> <raw-image-bytes>`. DSU userdata is separate, so push files while in the normal system, not inside DSU. To redo it, reboot to normal first, `gsi_tool wipe`, push, install. The first DSU boots can land in the charger loop; hold Power and retry.
- Phone adb drops out when it's on a wall charger or in a charge-only USB mode. Use a data cable and File transfer mode.

## Linux / kernel research (2026-11-07, not started)
- No TCL 6165S / MT6762 kernel source found. TCL's SourceForge project `tcl-mobile` is the place to check next. Their GitHub org is TV code.
- postmarketOS has the Redmi 9A (MT6762G, same SoC family) as a "testing" device, and a mainline fork `EvilSoCute/linux-mt6765` exists. That is the likely base for any mainline port. Display, touch, Wi-Fi and the rest would need a device tree for the 6165S.
- The stock kernel `Image` and boot image are already unpacked in `bpf/`.

## Next steps (not started)
1. User uses mod1 in DSU for a day, then decides whether to flash it (`GSI=` image in the Makefile; a real flash wipes userdata, so reinstall apps and re-grant Magisk Shell).
2. Optional speed tuning once there is a baseline (dexopt settings, zram, background limits).
3. Check TCL's SourceForge for the 6165S kernel source. If it's missing, read the config and device tree out of the stock kernel.
4. Chroot Linux (Termux/proot or a rooted chroot) before any dual-boot work.

## Android build environment (casper-7e, 2026-11-06)
Android SDK, tools and ccache are on `/mnt/1tb/android` (`. /mnt/1tb/android/env.sh`). See `/mnt/1tb/android/README.md`. No NDK or AOSP checkout (a full AOSP build doesn't fit this laptop).

<!-- casper-7e: append your section below this line -->

### casper-7e details (added 2026-11-07)

**Laptop tools (/mnt/1tb/android)**
- `sdk/`: cmdline-tools 23.0, platform-tools 37.0.1, build-tools 35.0.0, platforms;android-35. Installed with the old `~/Android/Sdk` sdkmanager (`--sdk_root=/mnt/1tb/android/sdk`). Licenses were copied from the old SDK. The old SDK (4 GB, API 34) is untouched; Android Studio 2026.2.2.8 still points at it.
- `env.sh`: sets `ANDROID_HOME`/`ANDROID_SDK_ROOT`, puts cmdline-tools, build-tools 35.0.0, platform-tools and `/mnt/1tb/android/tools` on `PATH`, and sets `CCACHE_DIR=/mnt/1tb/android/ccache`. `. /mnt/1tb/android/env.sh` to use it.
- `tools/apktool`: wrapper around `tools/apktool.jar` (3.0.3, downloaded from the GitHub release). Used a jar because pacman and AUR have no apktool package.
- pacman installs: erofs-utils, f2fs-tools, jadx, repo, ccache, squashfs-tools. Already present: android-tools 37 (lpunpack, lpmake, simg2img, mkbootimg, unpack_bootimg, fastboot), e2fsprogs, clang, gcc, dtc, openjdk 21.
- `workspace/bootcfg/`: copy of `boot_a.img` taken from `/mnt/1tb/tcl30se-backup`, unpacked into `out/` (kernel, ramdisk, dtb), plus `config.txt` (the kernel config embedded in the stock kernel).

**Build machine**
- Large source trees and kernel builds ran on a separate x86_64 Linux build VM; adb, fastboot and flashing stay on the laptop.

**Why these choices**
- SDK on `/mnt/1tb`: the home partition has about 120 GB free. A fresh SDK was cheaper and safer than moving the existing one.
- No NDK or AOSP checkout on the laptop: 14 GB RAM, and an AOSP tree plus output is 200+ GB. System-image edits need only simg2img, erofs/mount tools, apktool and avbtool. The kernel is the easier thing to move between machines (few GB), but see below.

**Stock kernel findings (read from `boot_a.img`, phone not touched)**
- Kernel 4.19.188, config embedded and extracted to `workspace/bootcfg/config.txt`.
- Enabled: UTS/PID/NET namespaces, cgroups (memcg, cpuset, cpuacct, freezer, sched, bpf), overlayfs, FUSE, ext4/f2fs, veth, bridge, iptables NAT + masquerade, tun, seccomp, zram, COMPAT (arm32 runs). A plain chroot with bind-mounted /dev, /proc, /sys should work on the stock kernel.
- Missing: `USER_NS`, `SYSVIPC`/`POSIX_MQUEUE` (so no IPC namespace), `BINFMT_MISC`, `CGROUP_DEVICE`, `CGROUP_PIDS`, `CFS_BANDWIDTH`, `FHANDLE`, `EROFS_FS`, `SQUASHFS`. These block systemd, Docker/LXC, rootless containers, qemu-user and SysV-IPC daemons.
- SELinux is enforcing; chroot/mount will need root (Magisk) and possibly a policy tweak.

**Still missing**
- Kernel build: no kernel source for the 6165S. Not on GitHub. A GPL request was posted: https://github.com/TCLOpenSource/TCL_Kernel_OpenSource/issues/4 (draft letter: `notes/gpl-request-tcl.md`, not sent anywhere; no official TCL email found). The 6165H request on the TCL Mobile SourceForge tracker has been open since 2023. Also needed once source exists: a cross toolchain (the build used Android clang 11.0.1 r383902) and the defconfig; none are on the VM yet.
- Chroot Linux: no arm64 rootfs or launch script yet. Plan is a Debian/Ubuntu arm64 rootfs built on the VM, with `proot` for the missing kernel features.
- Not installed: NDK, AOSP source, a cross toolchain.

**Problems hit**
- SSH from the laptop to the build VM timed out intermittently at connect (pings were fine), so long installs on the VM were run with nohup and polled.
- `repo --version` on the VM prints "repo not installed"; that is the Ubuntu launcher stub and it fetches the real tool on first `repo init`.
- An adb server (tcp:5037) was already running when I started; I did not touch it.
