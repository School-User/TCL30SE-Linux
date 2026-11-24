- **Current image (2026-11-11 06:37): `out/boot-b-mainline.img` md5 `3d21828cb339e5e275dd490ce6e559ba`, kernel `#54`.**
  Same config as `#53` (internal Wi-Fi stack off) plus `&i2c0` enabled for the speaker amp. Copy:
  `backups/boot-b-mainline-3d21828c-kernel54-audio.img`. Internal audio plays but the owner hears only static (see "Audio WORKS" at the end).
  The previous `#53` entry below is kept for history.
- Last flashed image: `debian/out/boot-b-mainline.img`, md5 `070ab18c86082f4158ceb9f7d1fdd972` (2026-11-11 00:40,
  `PGTABLE_LEVELS=4` / `VA_BITS=48` / `COMPAT=y` / `SOUND=y` / `SND=y` / `SND_USB_AUDIO=y` / `SND_SOC=y`,
  `CONFIG_MTK_COMBO` **off**, plus the new AFE/codec/sound-card DTS nodes; checked with `/tmp/an.py` before flashing).
  Fallbacks: `boot-b-good-desktop.img` (boots, but pre-ALSA) and `boot-b-touch-old62.img` (07:07 image with the
  broken 62-byte read). Do **not** flash the `CONFIG_MTK_COMBO=y` image (md5 `2f20df62`): it does not boot.

# Handoff: native Debian on the TCL 30 SE (2026-11-10)

Phone: TCL 30 SE (6165S, "cruze_pro", MT6762/MT6765). Laptop user is `casper`.
Full running log with every finding: `~/.claude/projects/-home-casper/memory/tcl_native_debian_bringup.md`. Read the
last sections first (TOUCH WORKS, USB HOST WORKS, DESKTOP, PHOSH); older touch sections are superseded.

## State right now

Working:
- Debian 13 (trixie) boots unattended from slot B on mainline 7.1 (fork TheKnightSky/linux-mt6765), root on userdata (103 GB).
- Screen (simpledrm, 720x1600), charging (SY6970 via bq25890 driver), eMMC, volume/power keys.
- Sxmo desktop (sway) autostarts as user `siddh` on tty1. Screen margins for the notch/rounded corners are at the end
  of `~/.config/sxmo/sway`. User prefers Sxmo; Phosh is installed but disabled (`casper-desktop phosh|sxmo`).
- Touch (FocalTech FT8756, SPI). Works end-to-end and is verified on the phone (see "Touch: what matters").
  The 90-byte fix only exists in images built after 2026-11-10 09:56: the 07:07 image restored after the bad mt76
  boot still had the 62-byte read, logged a `report ret -5` burst and delivered no events at all.
- USB host mode (keyboard/mouse through a dongle). User confirmed it works.
- Time and clock format (2026-11-10): the local timezone, clock set to the real time (it was 7 months behind,
  which broke apt `InRelease` verification), and the Sxmo clocks switched from 24-hour to 12-hour:
  `export SXMO_STATUS_DATE_FORMAT="%-I:%M %p"` in `~/.config/sxmo/profile`, plus `%H:%M` -> `%-I:%M %p` in
  `/usr/share/sxmo/default_hooks/sxmo_hook_statusbar.sh` and `sxmo_hook_desktop_widget.sh`. Note the
  `~/.config/sxmo/hooks` override dir is NOT in Sxmo's PATH on this install, so edits there do nothing. A
  `sxmo-utils` upgrade reverts the two hook files (the profile export alone does not cover the monitor-spawned hooks).
  NTP is inactive: there is no WAN, UDP cannot go through the socks proxy, so the clock drifts by hand again.

Not working / not started:
- Internal Wi-Fi/Bluetooth (MT6631 connsys): **the vendor gen4m/WMT stack now compiles and links
  into `vmlinux`** (2026-11-10 23:13, see the section near the end). No mainline driver exists, so this
  is the 4.19 MediaTek stack ported forward, not a rewrite. Nothing is flashed and nothing probes yet:
  the DTS `consys@18002000`/`wifi@18000000` nodes and both reserved-memory regions are in, the HIF is
  AXI/PDMA (not SDIO; stock `msdc@11240000` is `no-sdio`), and the MT6631 firmware plus this
  handset's NVRAM are recovered and staged in `debian/firmware/`. Still missing: the EINT GPIO (no
  `connectivity-combo` node in the stock DTB or either dtbo slot), EMI MPU programming, and a real
  CONN power domain. `debian/WIFI-PORT.md` has the layer map and the rest.
- USB Wi-Fi: **ready as of 2026-11-10 22:20** (image md5 `282458d0659e9526858f75e75394eaed`). `CONFIG_WLAN=y` plus
  mt76 (mt76x0u/mt76x2u/mt7921u/mt7925u/mt7601u), rtl8xxxu, rtw88 USB (8822bu/8821cu/8723du), r8188eu, ath9k_htc,
  carl9170, zd1211rw are modules; the full tree is installed in `/lib/modules/7.1.0-mt6765+` on the phone (persists,
  root is on userdata) and the needed blobs were fetched into `/lib/firmware` (mediatek/mt76*, rtlwifi, rtw88).
  `/root/wifi-up.sh` (also `wifi-up.service` + `90-wifi-up.rules`) modprobes the drivers, scans, and joins if
  `/root/wifi.conf` exists (line 1 SSID, line 2 passphrase, mode 600). The log is `/root/wifi.log`.
  Test procedure: unplug the laptop, plug the dongle (port auto-becomes host after ~3 s), wait, unplug the dongle
  and the phone returns to device mode after ~60 s - then read `/root/wifi.log`.
- Battery percentage (no fuel gauge driver), suspend (masked on purpose), GPU (software rendering only).
- Audio: **works as of 2026-11-11 06:45 at the driver level (see "Audio WORKS" at the end); not yet confirmed by ear.** Old note: The AFE/codec/card port now
  builds, probes and loads through the codec, but card registration hard-resets the phone and there is no
  kernel console to trace it. ALSA itself is built in (`SOUND=y`, `SND=y`, `SND_USB_AUDIO=y`), so a USB-C
  DAC/headset gives sound today.

## Open request when I stopped

`can you install fastfetch` — **done** (`/usr/bin/fastfetch` on the phone, checked 2026-11-11 05:51). For other
packages use the same pattern:

    ssh -i ~/.ssh/tcl30se -R 1080 root@172.16.42.1 \
      'apt-get -o Acquire::http::Proxy=socks5h://127.0.0.1:1080 install -y --no-install-recommends fastfetch'

(The proxy is needed because the laptop is often on school Wi-Fi, where plain NAT from the phone does not get out.)

## How to reach the phone

    i=$(ip -br link | awk '/enp.*u[0-9]/{print $1}' | head -1)
    sudo -n ip addr add 172.16.42.2/24 dev $i          # needed after every replug; the laptop takes no lease
    ssh -i ~/.ssh/tcl30se root@172.16.42.1             # or siddh@ (same key, NOPASSWD sudo)

`lsusb | grep 0525:a4a2` shows whether the phone is attached as a USB device. If it is absent, the port may be in
host mode (see below) or the cable is out. Screenshot: as siddh,
`XDG_RUNTIME_DIR=/run/user/1000 WAYLAND_DISPLAY=wayland-1 grim /tmp/s.png`, then scp.

## Build and deploy

- Kernel tree `/mnt/1tb/android/linux/linux-mt6765`, out dir `/mnt/1tb/android/linux/out`, board file
  `arch/arm64/boot/dts/mediatek/mt6762-tcl-cruze.dts`.
- `~/Projects/tcl30se-gsi/debian/deploy.sh [--no-build]`: builds (`mkboot.sh`), writes boot_b over SSH, verifies md5,
  reboots, waits for SSH. About 1-2 minutes. Run with `</dev/null`; a config change can make `make` prompt
  (run `make ... olddefconfig </dev/null` first).
- Last flashed image: see the note at the very top of this file (md5 `070ab18c...`, kernel `#53`). A byte-exact
  `boot_b` dump of it is in `debian/backups/boot_b-full-2026-11-11-kernel53.img`.
- **`deploy.sh` is safe again (2026-11-11 06:35):** `out/.config` is now exactly the running `#53`/`#54` config
  (`CONFIG_MTK_COMBO` off). The Wi-Fi-port config with `CONFIG_MTK_COMBO=y` is saved as
  `out/.config.combo-on-20261111`; copying it back makes `deploy.sh` build the boot-looping image again.
- Recovery if Linux does not come up: hold Power 10-15 s to force a restart (PMIC long-press reset is enabled).
  Fastboot can flash boot_b (`fastboot flash boot_b <img>; fastboot --set-active=b; fastboot reboot`).
  `reboot bootloader` from mainline does NOT reach fastboot. The stock-kernel test image
  (`debian/out/boot-b-stock-touch.img`) drops to fastboot by itself after 150 s.

## USB port behaviour (new today)

- `casper-usb host|device` (root): host = charger 5 V boost on + controller in host mode; device = laptop/charging.
- `casper-usb-auto.service` (enabled): when the laptop/charger is unplugged the port becomes a powered host after
  ~3 s; it stays host while a USB device is attached and returns to device after 60 s with nothing attached.
  Consequence: after pulling the dongle, wait about a minute before expecting the phone on the laptop.
- The wusb3801 USB-C chip (on a bit-banged i2c-gpio bus, GPIO50/51) does not reliably detect the user's dongle,
  which is why switching is time-based. Sxmo's Scripts menu has two entries for manual switching.
- Kernel fixes are in `drivers/usb/musb/mediatek.c` (session/PHY ordering both directions) and
  `drivers/usb/typec/wusb3801.c` (role switch).

## Touch: what matters

- Driver: `drivers/input/touchscreen/focaltech-ft8756.c`. One 90-byte read of reg 0x01 per interrupt, CRC retry,
  no firmware download when reg 0xA3 already reads 0x87 (the chip runs its own firmware).
- Never poll the chip from userspace or read when no interrupt is pending: it desynchronises the chip's reply
  queue until reboot. The test scripts in `/root` on the phone (ftprobe.py, clean.py, regs.py, ...) do exactly that.
- Verified 2026-11-10 on md5 `afb12672...`: probe at 0.55 s says `FT8756 firmware already running (version 87)`,
  zero `report ret` errors, IRQ 15 went 103 -> 621, and `evtest /dev/input/event1` logged 669 events with MT
  tracking ids, `BTN_TOUCH` 1/0 and positions (449,627 -> 463,656). `gpio-0` (INT) idles hi, so nothing is stuck.
- Taps do reach Sway (`type: touch`, `send_events: enabled`, identity calibration matrix, output `Unknown-1`
  active at scale 2, and `EVIOCGRAB` on event1 succeeds, so lisgd is not stealing it). Sxmo's wallpaper has no
  tap targets, so a plain tap on the background correctly does nothing: test with an edge swipe (bottom edge up
  opens the menu) or a tap on the status bar before concluding touch is broken.
- Reset GPIO11 does not reset the chip. A pulse on GPIO45 (display reset) does, and blanks the screen until reboot.

## Audio: state (2026-11-10) — HISTORICAL, see "Audio status, 2026-11-11 01:10"

- Was `# CONFIG_SOUND is not set`; as of md5 `cf3625ea...` ALSA is in: `CONFIG_SOUND=y`, `CONFIG_SND=y`,
  `CONFIG_SND_USB_AUDIO=y`. The kernel now says `usbcore: registered new interface driver snd-usb-audio` and
  `/proc/asound/cards` exists but reports `--- no soundcards ---`: `snd-usb-audio` only creates a card when a DAC
  is plugged, and the internal codec has no AFE to attach to. `alsa-utils` (aplay/speaker-test) is NOT installed.
- Already reusable in-tree: `sound/soc/codecs/mt6357.c` (1855 lines, the same MT6357 PMIC codec this phone has),
  `sound/soc/mediatek/mt8365/mt8365-mt6357.c` (machine driver that binds codec to AFE) and
  `drivers/clk/mediatek/clk-mt6765-audio.c`, which already exposes the audsys gates
  (`CLK_AUDIO_AFE`/`ADC`/`DAC`/`TML`, parent `audio_ck`).
- Missing: an MT6765 AFE platform driver. `grep -rl mt6765 sound/` returns nothing. The AFE window is 0x11220000 —
  `mt6765.dtsi` declares it as `audio: clock-controller@11220000`, `mt8365.dtsi` declares the same address as
  `afe: audio-controller@11220000`. Template: MT8365 AFE (~6.3k lines over 7 files), or MT8183 (~6k). Without it
  there is no PCM path, so `aplay -l` shows no card no matter what the codec does.
- Internal speaker/headphone audio is therefore a port of the same shape as the Wi-Fi one (codec side already
  solved, AFE side not). The cheapest audio that can work today is USB audio class: hand-edit `.config` with
  `CONFIG_SOUND=y`, `CONFIG_SND=y`, `CONFIG_SND_USB_AUDIO=y` (never `olddefconfig`) and plug a USB-C DAC/headset.
- `bpf/unpacked/dtb` is an **Android dtb.img**, not a bare FDT: magic `d7b7ab1e`, 0x40-byte header. The real FDT
  starts at offset **0x40** and `dtc` reads it fine after `tail -c +65 bpf/unpacked/dtb > /tmp/stock.fdt`.
  From that FDT (board truth, useful for the AFE port): `pmic_accdet` = `mediatek,mt6357-accdet` with headset-mic
  settings, `pmic_codec` = `mediatek,mt6357-sound` (io-channels `pmic_codec`, `pmic_accdet`), `audclk@11220000`
  (`mediatek,mt6765-audsys`, syscon) and the vendor-only `audio@11220000` (`mediatek,audio`, syscon, btcvsd).

### AFE port started (2026-11-10, NOT functional yet)

- Vendor source: `/mnt/1tb/android/linux/vendor-alps` (sparse clone, branch `4.19.191_mt6765`, repo
  `KrutosVIP/generic_kernel_mediatek_alps`). It has a purpose-built MT6765 audio driver:
  `sound/soc/mediatek/mt6765/` = 5,835 lines — `mt6765-sound.c` (2659), `mtk-auddrv-clk.c` (1470),
  `mtk-soc-afe-connection.c` (1061), `mtk-auddrv-afe.h` (492, 315 AFE register defines), `mtk-soc-pcm-platform.h`.
  It sits on MediaTek's private `mtk-auddrv-*`/`mtk-soc-*` framework (13 private headers) and cannot compile
  against mainline ASoC, so it is a register/clock reference, not something to drop in.
- Scaffold made in the kernel tree: `sound/soc/mediatek/mt6765/` (11 files, 4,796 lines) by renaming the in-tree
  MT8365 driver, plus Kconfig entries `SND_SOC_MT6765` and `SND_SOC_MT6765_MT6357`, `mediatek/Makefile` line and a
  local Makefile. `.config` now has `SND_SOC=y`, `SND_SOC_MEDIATEK=y`, `SND_SOC_MT6765=y`, `SND_SOC_MT6357=y`,
  `SND_SOC_MT6765_MT6357=y` (`MTK_PMIC_WRAP=y` was already set).
- `make ... sound/soc/mediatek/mt6765/` → **0 errors**; `PGTABLE_LEVELS=4` / `VA_BITS=48` still hold. Nothing was
  flashed for this: the phone still runs `cf3625ea...` (ALSA + USB audio only).
- What is still MT8365 (so it would not make sound): `mt6765-reg.h` register offsets, the clock tree (it requests
  `aud1`, `engen1`, `top_audio_sel`, `audio_i2s0_m` — MT8365 gate names, while `clk-mt6765-audio.c` exposes
  `CLK_AUDIO_AFE/ADC/DAC/22M/APLL_TUNER/I2S1-4_BCLK/TML/DAC_PREDIS`), memif count and IRQ list, and there is no
  `afe@11220000` / sound-card / `mt6357-sound` child in the DTS.
### Live test of the port (2026-11-10 21:57): card registers, playback hangs the bus — HISTORICAL

- **The card came up.** Load order on the phone: `snd-soc-mtk-common.ko` -> `snd-soc-mt6765-pcm.ko` ->
  `snd-soc-mt6357.ko` (the codec module was the missing piece; `CONFIG_SND_SOC_MT6357=m`) -> `mt6765-mt6357.ko`.
  Result: `0 [mt6765evk]: mt6765-evk - mt6765-evk` with `00-00 MultiMedia1_PLayback`, `00-01 MultiMedia2_PLayback`,
  `00-02 DL1_AWB_Record`, `00-03 MultiMedia1_Capture`.
- AFE component `11220000.afe` registers 15 DAIs: DL1, DL2, TDM_OUT, AWB, VUL, VUL2, VUL3, TDM_IN, Hostless FM DAI,
  HW_GAIN1, DMIC, INT ADDA, I2S, 2ND I2S, PCM1. Codec binds (`.../1000d000.pwrap:pmic/mt6357-sound`).
- `alsa-utils 1.2.14-1` is now installed on the phone (via the laptop socks proxy; the apt options must go to the
  *remote* command, not to ssh, or ssh rejects them as bad options).
- **Playback blocks the kernel.** `aplay -D hw:0,0 /tmp/tone.wav` leaves aplay in uninterruptible **D** state at
  `dpcm_fe_dai_open+0x4c` (and `amixer`/ctl calls in `snd_pcm_control_ioctl`), `Afe_ISR_Handle` IRQ 196 stays at 0,
  load climbs to ~6.4, and the modules cannot be unloaded ("in use"). Recovered with
  `echo 1 > /proc/sys/kernel/sysrq; echo b > /proc/sysrq-trigger` (a plain reboot; the AFE/codec modules are
  in-memory only and do not come back).
- Earlier, opening a *capture* stream also oopsed in `dpcm_be_dai_startup` -> `__soc_pcm_open` (NULL deref).
- Most likely cause of both: the AFE register block is not actually clocked. The port reports
  `set parent failed (-22), continuing` from `mt6765_afe_set_clk_parent()`, because the clock table maps every
  MT8365 name (`aud1`, `engen1`, `top_audio_sel`, `audio_i2sX_m`) onto mt6765 *gate* clocks, and gate clocks have no
  selectable parent. Real playback needs the vendor clock programming (APLL1/APLL2, aud1/aud2 dividers, engen1/2,
  top_mux_* from topckgen/apmixed) rather than gate-only clocks.
- Next concrete step: implement the mt6765 AFE clock tree from `vendor-alps/sound/soc/mediatek/mt6765/mtk-auddrv-clk.c`
  (1470 lines: APLL1 counter, APLL12 dividers, aud1/2 muxes) against `clk-mt6765-audio.c` gates, then retry
  `aplay` and watch `Afe_ISR_Handle` (IRQ 196) for real interrupts.

- Order of work from here: (1) rewrite `mt6765-reg.h` from the vendor header (base 0x11220000; DAC_CON0 0x10,
  DAC_CON1 0x14, DL1 0x40, VUL 0x80, IRQ_MCU_CON0 0x3a0, IRQ0/IRQ6 MCU count 0x300/0x304); (2) remap the clock
  driver onto the mt6765 audsys gates and APLL parents; (3) fix memif/IRQ tables; (4) add DTS nodes for the AFE,
  the sound card and the `mt6357-sound` MFD child with `io-channels = "pmic_codec", "pmic_accdet"`; (5) verify
  4-level/48-bit, flash, then `aplay` through the MT6357 codec (speaker + headphone). `alsa-utils` still needs
  installing.

## Internal Wi-Fi port: compiles and links (2026-11-10 23:13)

`make ... vmlinux` now exits 0 with the whole vendor tree built in (`vmlinux` 418,166,912 bytes,
`CONFIG_MTK_COMBO=y`): 2,003 `mtk_*`/`wlan*`/`kal_*` text symbols, including `wlanProbe`,
`mtk_cfg_add_key`, `mtk_wmt_probe`. **This image is not flashed and was not built to be flashed.**
The phone still runs `282458d0…`.

What had to change (all inside `drivers/misc/mediatek/connectivity`, plus the `mt-plat` shims):

- cfg80211 ops converted to mainline signatures: `wireless_dev` instead of `net_device` on the
  key/station/AP callbacks, new `int link_id` on key, station, TDLS, DFS and start/stop-AP ops,
  `cfg80211_ap_update` for `.change_beacon`, `radio_idx` on the tx-power/phy-param ops.
  `struct station_parameters` rate/HT/VHT members moved under `link_sta_params`.
- Removed/renamed kernel APIs: `set_fs`, `vfs_read`, `strlcpy`, `netif_rx_ni`, `del_timer(_sync)`
  (now `timer_delete(_sync)`), `from_timer` (now `timer_container_of`), `class_create(THIS_MODULE,…)`,
  `MODULE_SUPPORTED_DEVICE`, `MODULE_IMPORT_NS(VFS_internal…)`, `TTY_LDISC_MAGIC` and the old tty
  ldisc/`receive_buf`/`ioctl` shapes, `alarm_start_relative`/`alarmtimer_restart`,
  `dev->dev_addr` writes (now `dev_addr_set`), `dev->tx_dropped`/`rx_dropped`,
  `struct sched_entity.statistics`, the RPS-map clone in `gl_kal.c` (stubbed, `-EOPNOTSUPP`),
  fbdev blank notifier (`FB_EVENT_BLANK` gone), `netif_napi_add` arity, platform `.remove`
  returning void, thermal-zone-of registration (skipped), `ENUM_ACPI_STATE` vs `<acpi/actypes.h>`
  macro clash.
- Deliberately dropped, not ported: BT (`stp_btif.o`, needs the separate vendor btif driver),
  FM radio (`fmradio/`, needs `CONFIG_MTK_FM_CHIP`), UART transport kept but its tty glue updated.
- Shims: `mtk_disp_notify.h` (inert display-notify), `mtk_btif_exp.h` (types only), the existing
  `mt-plat` set, plus the vendor warning policy in the connectivity Makefile
  (`-Wno-cast-function-type-strict` etc.).

### Device tree and reserved memory (2026-11-10 23:40)

`mt6762-tcl-cruze.dts` now carries the nodes, with values taken from this board's own stock DTB
(`bpf/unpacked/dtb`, real FDT at offset 0x40):

- `consys@18002000`: `mediatek,mt6765-consys`, 13 reg windows (CONN_MCU_CONFIG 0x18002000,
  TOP_RGU 0x10007000, INFRACFG_AO 0x10001000, SPM 0x10006000, CONN_HIF_ON 0x18007000,
  CONN_TOP_MISC_OFF 0x180b1000, CONN_MCU_CFG_ON 0x180a3000, CONN_MCU_CIRQ 0x180a5000,
  CONN_TOP_MISC_ON 0x180c1000, CONN_HIF_PDMA 0x18004000, two index-sync placeholders,
  INFRACFG_REG 0x1020e000), IRQs SPI 267 (BGF) / 269 (WDT) / 270 (conn2ap), `pmic = <&mt6357_pmic>`,
  `memory-region = <&consys_mem>`.
- `wifi@18000000`: `mediatek,wifi`, AXI HIF window, SPI 268, `memory-region = <&wifi_mem>`.
- reserved-memory: `consys_mem` 4 MB (`mediatek,consys-reserve-memory`) and `wifi_mem` 3 MB
  (`shared-dma-pool`), both dynamic, 16 MB aligned, allocated in [1G, 3G).
- `conn_clk`: fixed-clock (26 MHz) used as the `"conn"` clock. Stand-in only: mainline has no
  SCP_SYS_CONN clock provider, so `clk_get()` succeeds but nothing is gated.
  TODO(port): bind to `MT6765_POWER_DOMAIN_CONN`.
- New `.../platform/mtk_wcn_consys_rmem.c`: `RESERVEDMEM_OF_DECLARE` for
  `mediatek,consys-reserve-memory`, so `of_reserved_mem_lookup()` in `mtk_wcn_consys_hw.c`
  returns a region instead of failing.

`make ... dtbs` exits 0 and the compiled dtb contains `consys@18002000`, `wifi@18000000`,
`conn-clock` and both reserved-memory nodes; `vmlinux` still links after the new file.

**Correction: Wi-Fi on MT6765 is not SDIO.** The stock DTB's `msdc@11240000` has `no-sdio`, and
the vendor build sets `CONFIG_MTK_COMBO_WIFI_HIF=axi`: the chip is reached over the AXI/PDMA HIF
(`wifi@18000000` + CONN_HIF_PDMA 0x18004000), so no msdc/SDIO host port is needed for Wi-Fi.

### MT6631 firmware and NVRAM recovered from the stock backup (2026-11-10 23:30)

The blobs the driver asks for are not in any package, but they were still in the October backup of
the stock vendor partition:

- source: `/mnt/1tb/tcl30se-backup/backup/vendor_a.img` (ext4, read-only:
  `debugfs -R "rdump /firmware /tmp/vfw" vendor_a.img`), files under `/firmware/`.
- staged in `debian/firmware/` (keep it; this is not reproducible from the phone any more):
  - `WIFI_RAM_CODE_soc1_0_1_1.bin` (637,624 B) - the connac firmware. `chips/connac/connac.c`
    builds the name as `%s_%u%s_1.bin` from `WIFI_RAM_CODE_soc1_0`, `CFG_WIFI_IP_SET=1` and an
    empty flavor, so this is exactly the name requested.
  - `WMT_SOC.cfg` (121 B) - read by `wmt_conf_read_file()` via `request_firmware()` once the chip
    type is SOC, i.e. it must be in `/lib/firmware`.
  - `soc1_0_patch_mcu_1_1_hdr.bin`, `soc1_0_ram_mcu_1_1_hdr.bin`, `soc1_0_ram_wifi_1_1_hdr.bin`
  - `txpowerctrl.cfg`
  - `WIFI` (2,050 B) and `WIFI_CUSTOM` (6 B) - this handset's calibration, read-only from
    `nvdata.img:/APCFG/APRDEB/`; the nvdata partition itself was not written.
- install (phone must be up and idle, other session owns it):
  `scp debian/firmware/* root@172.16.42.1:/lib/firmware/`
  then, for NVRAM, `mkdir -p /data/nvram/APCFG/APRDEB` and copy `WIFI`/`WIFI_CUSTOM` there.
- Caveat: gen4m does not read NVRAM from that file at probe. `g_aucNvram` is only filled by the
  test-mode handler (`wlanNvramBufHandler`, userspace write); `kalCfgDataRead16()` otherwise reads
  zeros/defaults. The calibration is staged for a future loader, not used yet.

Still required before the chip can come up: the connectivity-combo EINT node (the stock DTB and
dtbo overlays have no combo node; dtbo only patches `consys` pinctrl with an empty `consys_default`
and GPS-LNA pins), EMI MPU programming (no `mt_emi` driver exists in this tree and the vendor EMI
source is not in `vendor-alps`, so the stub stays until the register layout is recovered), and a
real CONN power domain instead of `conn_clk`. Then a hand-loaded test boot with the firmware in
`/lib/firmware`.

### Chip type now defaults to SOC in the built-in adapter (2026-11-10 23:33)

The enum order is COMBO=0, SOC=1, INVALID=2 and `g_chip_type` starts at INVALID. On Android the
`wmt_launcher` pushes `COMBO_IOCTL_SET_CHIP_ID`, and only then does `wmt_detect_set_chip_type()`
run; this port has no launcher and no combo node, so the type would have stayed INVALID and every
`== WMT_CHIP_TYPE_SOC` branch (HIF choice, `wmt_conf_read_file()`, PSM/BTM paths) would have been
skipped. `wmt_lib_init()` now forces SOC when the type is INVALID, and a missing `WMT_SOC.cfg` is
logged instead of aborting WLAN init. `vmlinux` links (exit 0, 0 errors).

### Wi-Fi port status after the firmware recovery (2026-11-10 23:35)

State of the port, all of it compile-time only: nothing is flashed, nothing probes, and the phone
has not been written to this round (SSH to `172.16.42.1` timed out; the touch session owns it).

Done and verified:

- Vendor gen4m/WMT stack builds into `vmlinux`: exit 0, 0 errors, 418,219,480 B
  (`/tmp/combo31.log`); 2,003 `mtk_*`/`wlan*`/`kal_*` symbols.
- `make dtbs` exit 0; the board DTB (31,585 B) contains `consys@18002000`, `wifi@18000000`,
  `conn-clock`, `consys_mem` and `wifi_mem`.
- Firmware names checked against the driver: `connac.c` builds
  `WIFI_RAM_CODE_soc1_0_1_1.bin` from `CFG_WIFI_IP_SET=1`; `WMT_SOC.cfg` is read through
  `request_firmware()` once the chip type is SOC. NVRAM was read-only from `nvdata.img`
  (`/APCFG/APRDEB/WIFI`, `WIFI_CUSTOM`); the partition itself was not written.
- Chip type now defaults to SOC (`wmt_lib_init()`), so the SOC-gated init paths actually run.
- Config invariants rechecked after the last build: `/tmp/an.py` exit 0, `PGTABLE_LEVELS=4`,
  `ARM64_VA_BITS=48`.

Still missing, in order:

1. Firmware install on the phone (`/lib/firmware`) — blocked on the phone being reachable and idle.
2. EINT GPIO: the stock DTB and **both** dtbo slots have no `mediatek,connectivity-combo` node;
   `dtbo_a` only patches `consys` pinctrl with an empty `consys_default` plus GPS-LNA pins. In-band
   HIF interrupts may be enough for a first bring-up.
3. EMI MPU: no `mt_emi` driver in this tree and no vendor EMI source in `vendor-alps`, so the stub
   stays until the register layout is recovered; a guessed layout can lock the AP out of DRAM.
4. Power/clocks: replace the `conn_clk` fixed-clock with `power-domains = <&spm MT6765_POWER_DOMAIN_CONN>`.
   This is ready to do: the domain exists in mainline (`drivers/pmdomain/mediatek/mt6765-pm-domains.h`) and is
   live on the phone (`conn` in `/sys/kernel/debug/pm_genpd`). The consys probe powering an unpowered CONN block
   is a likely cause of the 23:42 boot loop.
5. NVRAM loader decision: gen4m never reads `g_aucNvram` from a file (only the test-mode userspace
   write fills it), so either add a loader or accept defaults for bring-up.
6. First hand-load test (never `fastboot boot`): consys probe logs, non-zero EMI base, chip ID
   readback, then firmware download.

Limitation: `vendor_b.img` does not exist in the backup set, so the only vendor reference is
`vendor_a`; no cross-slot hash check was possible.

## Boot-loop incident, 2026-11-10 23:42 (Wi-Fi session)

**Cause.** A `deploy.sh --no-build` at 23:40 wrote `boot-b-mainline.img`, built 23:40:18 from the
shared tree. That image is the first one that links the vendor Wi-Fi stack, and its DTB carries
`consys@18002000`. The consys probe powers the chip on at boot and hangs; `panic=5` in the forced
cmdline then turns it into a ~3 s reset loop. USB log: `0e8d:0003` (MTK preloader) every 2-3 s
from 23:42, no `0525:a4a2` gadget, no SSH.

- `boot_b` was recovered via fastbootd and now holds `boot-b-good-desktop.img`
  (md5 `fe19b3b4...`, kernel `#29` built 06:43), so the 09:56 touch fix is missing.
- Ready but **not flashed**: `debian/out/boot-b-recovery-noconsys.img`
  (md5 `ba5cadc996c270017c940cac71a95def`). Same 23:40 kernel, but its shim DTB has
  `consys@18002000` and `wifi@18000000` set to `disabled` (verified with fdtget); keeps the
  touch fix and USB Wi-Fi. Its `initcall_blacklist` is inert because `CONFIG_CMDLINE_FORCE=y`
  (the kernel ignores bootloader/DTS cmdlines), so the disabled nodes are the protection.
- **Do not run `deploy.sh` from this tree**: it rebuilds the consys-enabled DTB and will hang
  again. The bad image was renamed to `boot-b-mainline.consys-hang.img`.

## Sxmo crash and recovery, 2026-11-10 23:27-23:53 (Sxmo session)

Follow-up to the 23:42 incident above: the verified recovery and the state it left.

**Crash chain.** The audio session was loading the new AFE/codec modules; touching the
audio register block while its power domain was off hung the AXI bus and the watchdog
reset the phone. A `sync; reboot` at 23:27 then hung in shutdown, so SSH was gone and
only the first-stage rescue telnetd on 2323 answered. Sxmo itself was not the fault.

**Why the next boot looped.** At 23:40 the audio session ran `mkboot.sh` + `deploy.sh
--no-build`. `mkboot.sh` rebuilds `Image` from the shared tree, which by then had
`CONFIG_MTK_COMBO=y`, so the 16.5 MB image carried the unproven MT6631 Wi-Fi stack. The
phone reset before the USB gadget came up; `deploy.sh` hung on the dead SSH write and was
killed (pid 795152).

**Recovery.** The phone fell back to slot A and exposed fastbootd/LK fastboot. From the
laptop:

    fastboot flash boot_b debian/out/boot-b-good-desktop.img   # md5 fe19b3b4...
    fastboot --set-active=b
    fastboot reboot

Re-added `172.16.42.2/24` on the laptop NIC afterwards (NetworkManager drops it on every
USB re-enumeration). Sxmo came back on the 07:07 image.

**Verified at 23:51-23:53** over SSH: uptime growing, `systemctl is-system-running` =
`running`, 0 failed units, `sway` + 30 Sxmo processes (swaybar, sxmo_status, sxmo_menumode,
lisgd), `/run/user/1000/wayland-1` owned by siddh, `getty@tty1` session open. The audio
blacklist `/etc/modprobe.d/blacklist-snd-mt6765.conf` survived; no audio modules loaded.

**Caveat left by the recovery.** `boot-b-good-desktop.img` is the 07:07 image (kernel #29,
built 06:43): it predates the 09:56 touch fix and ALSA, so touch has the 62-byte read bug
and `/proc/asound` does not exist. The ready fallback for that is
`debian/out/boot-b-recovery-noconsys.img` (md5 `ba5cadc9...`, 23:49): same 23:40 kernel,
but its shim DTB has `consys@18002000` and `wifi@18000000` disabled (verified with fdtget),
so it keeps the touch fix and USB Wi-Fi without probing the Wi-Fi stack. Its
`initcall_blacklist` is inert (`CONFIG_CMDLINE_FORCE=y`); the disabled nodes are the
protection. It was not flashed in this recovery. The image the phone ran before the crash
(md5 `282458d0...`, 22:20, USB Wi-Fi + touch fix + ALSA) was not found on disk.

**Coordination.** State and the "do not flash a CONFIG_MTK_COMBO image from the shared
tree" warning were posted to `debian/AGENT-CHAT.md`. The audio session later built
`out/boot-b-mainline.img` md5 `070ab18c` with MTK_COMBO disabled (00:55 addendum); the
audio card-registration resets described there are a separate, still-open issue.

**Later state, verified on the phone just now:** the running kernel is `#53`, built
2026-11-10 23:53:28 - the `boot-b-mainline.img` `070ab18c` build with MTK_COMBO disabled,
the touch fix and ALSA. Sxmo is up on it: uptime 20,324 s, `systemctl is-system-running`
= `running`, 0 failed units, sway + 35 Sxmo processes. `/proc/asound/cards` exists and
says `--- no soundcards ---`; that is the still-open audio work, not a recovery failure.
The touch/ALSA caveat above applies to the 07:07 fallback only, not to the current boot.

## Hard rules from the user (keep)

- NEVER play audio from the laptop. The sink is muted; do not unmute.
- Do not paste secrets into chat (see `~/CLAUDE.md`).
- No Claude/AI co-author or session trailers on commits or PRs.
- Use only the Logitech webcam (/dev/video2) if a photo of the phone is needed; it was not attached today.
- Never write preloader/mmcblk0boot*, lk_a, nvram, nvdata, nvcfg, seccfg, proinfo, persist, md1img or super.
  Ask before writing lk_b or para (casper-bootok already rewrites the A/B block in para each boot; that is approved).
- Never use `fastboot boot`.
- Android's boot_a and super are kept as a rescue path. Android's userdata is gone (Debian lives there).
- Ask before risky reboots; routine deploy.sh cycles have been approved repeatedly.
- Do not use `pkill -f`/`pgrep -f` with a pattern that also appears in your own command line (it kills your shell;
  this happened three times).

## Housekeeping not yet done

- Phone-side changes (packages, user, services, Sxmo config, udev rule) exist only on the phone, not in
  `/mnt/1tb/android/linux/casperos-root.img`. A reflash loses them.
- Rescue telnetd on port 2323 (started by the first-stage init) is still enabled; remove once things are stable.
- Laptop NAT for the phone is not persistent.
- `debian/initramfs/init` still contains the stock-kernel touch test block (only runs when /lib/ft8756.ko exists).
- Test logs on the phone in /root (usbtest.log, hosttest*.log, roletest.log) can be deleted.
- Kernel tree changes are uncommitted.

## Audio bring-up, 2026-11-11 00:00 (session 2)

> Historical log. The authoritative, current summary is the last section,
> "Audio status, 2026-11-11 01:10 (session 2)".

Audio still does not work, but three real defects are fixed and the phone is
stable again.  Vendor truth used throughout:
`vendor-alps/sound/soc/mediatek/mt6765/` (`mtk-auddrv-clk.c`, `mtk-auddrv-afe.h`)
compared against upstream `sound/soc/mediatek/mt8365/`.

### Fixed

1. **AFE clock table** (`sound/soc/mediatek/mt6765/mt6765-afe-clk.c`).  The scaffold
   inherited MT8365 *names* but pointed them at MT6765 **gate** clocks
   (`TOP_AUD_SEL = "aud_afe"`, `CLK26M = "aud_tml"`).  Gates have a single parent, so
   `clk_set_parent()` returned `-EINVAL` — the `set parent failed (-22), continuing`
   line — and the AFE bus mux was left on whatever topckgen defaulted to.  Now maps to
   the topckgen **muxes** exactly like upstream MT8365:
   `top_audio_sel`, `top_aud_1_sel`, `top_aud_engen1_sel`, `clk26m`
   (MT6765 has no APLL2, so the AUD2/ENGEN2 slots reuse the APLL1 muxes).
   The `-22` warning is gone from a live probe.
2. **Machine driver drvdata bug** (`mt6765-mt6357.c`).  `int_adda_startup/shutdown` and
   `gpio_probe` read `snd_soc_card_get_drvdata()` as `mt6765_mt6357_priv`, but the
   mtk-soc-card framework stores `struct mtk_soc_card_data` there.  That was a hard oops
   (`mt6765_mt6357_int_adda_startup+0x6c`) and, because this board has no pinctrl node,
   it also reached `pinctrl_select_state(NULL)`.  Now uses `soc_card_data->mach_priv`
   and guards every pin-state switch with `!priv->pinctrl`.

### Build/deploy notes

- `make ... <dir>/<name>.ko` fails modpost (`mtk_adda_dl_rate_transform` /
  `mtk_adda_ul_rate_transform` undefined — they live in `snd-soc-mtk-common`).
  Use a full `make ... modules` (incremental, ~30 s here).
- The rebuilt `.ko`s were copied into
  `/lib/modules/7.1.0-mt6765+/kernel/sound/soc/{mediatek/mt6765,codecs}/` on the phone.
- udev autoloads these modules from the DT modalias at boot and PipeWire immediately
  probes the card, which reboots the phone.  `/etc/modprobe.d/blacklist-snd-mt6765.conf`
  now blacklists all four (`snd_soc_mt6765_pcm`, `mt6765_mt6357`, `snd_soc_mt6357`,
  `snd_soc_mtk_common`) — **delete that file only once audio is proven stable**.
- Reboots seen during bring-up left *no* pstore/ramoops record, so they look like
  watchdog resets rather than an oops path.

### Current blocker

Loading `snd-soc-mt6357.ko` fails at probe:

    mt6357-sound mt6357-sound: Looking up vaud28-supply ... failed
    devm_regulator_get_enable(&pdev->dev, "vaud28") -> -ENODEV

so the codec never registers, no component binds, and `aplay -l` still shows no card
(`/proc/asound/cards` empty even with the three AFE modules loaded).

Next steps, in order:

1. Add `vaud28-supply = <&mt6357_vaud28_reg>` to the `mediatek,mt6357-sound` node in
   `mt6762-tcl-cruze.dts` (check the mt6357 regulator driver actually exposes that
   regulator), rebuild the DTB + boot image, flash.
2. With PipeWire stopped, load `snd-soc-mtk-common` -> `snd-soc-mt6765-pcm` ->
   `snd-soc-mt6357` -> `mt6765-mt6357`, then `aplay -D hw:0,0` and watch for D-state /
   load climbing (the previous failure mode).
3. Then chase the remaining AFE register map: `mt6765-reg.h` offsets vs the vendor
   header (DAC_CON0 0x10, DAC_CON1 0x14, DL1 0x40, VUL 0x80, IRQ_MCU_CON0 0x3a0,
   IRQ0/IRQ6 MCU count 0x300/0x304), the memif/IRQ tables, and the DT interrupt
   (`<0 0x99 8>` = GIC_SPI 153 from the stock FDT vs the 196 seen in earlier logs).

Correction (2026-11-11): there **is** an MT6765 power-controller driver, in
`drivers/pmdomain/mediatek/` (`mt6765-pm-domains.h`, commit `c633ae97d`,
`CONFIG_MTK_SCPSYS_PM_DOMAINS=y`), and the phone lists the domains live in
`/sys/kernel/debug/pm_genpd`: md1, conn, dpy, disp, mfg, mfg_core0, mfg_async, isp, ifr, cam, vcodec.
It has **no audio domain**, so this does not change the audio work.

### Addendum 00:55 — which image is on boot_b, and what actually reboots

- **Do not flash an image built with `CONFIG_MTK_COMBO=y` from this tree.** The
  23:40 image (md5 `2f20df62`) did not boot: the phone fell back to the MediaTek
  preloader (`0e8d:0003`) and went off the bus. Recovered with plain fastboot
  (`fastboot flash boot_b out/boot-b-good-desktop.img`) plus a power cycle; no
  storage was harmed. That image carried the unproven MT6631 Wi-Fi port in vmlinux.
- `out/boot-b-good-desktop.img` (07:07) **predates ALSA** — `/proc/asound` does not
  exist there. Do not use it as an audio baseline.
- `out/boot-b-mainline.img` is now md5 **`070ab18c`**: built with
  `CONFIG_MTK_COMBO{,_WIFI,_BT,_GPS,_FM,_CHIP_CONSYS_6765}` disabled (the shared
  `out/.config` was backed up and restored immediately afterwards, so the other
  session's tree is unchanged). It boots, has ALSA, and carries the new DTB.
- With that DTB the audio LDO comes up at boot: `vaud28: 2800 mV ... enabled`, so
  the codec's `devm_regulator_get_enable("vaud28")` now succeeds.
- **Still broken:** the moment `snd-soc-mt6357` + `mt6765-mt6357` are insmod'ed the
  phone reboots within seconds (3/3 attempts, no pstore record => watchdog, not an
  oops). This is during card/component registration, *before* any `aplay`.
  `aplay -l` therefore still reports no card.
- `/etc/modprobe.d/blacklist-snd-mt6765.conf` is intact on the rootfs and keeps the
  modules from autoloading at boot (the reboots were all my manual insmods).

Next: bisect the registration instead of loading all four at once —
(1) `snd-soc-mtk-common` + `snd-soc-mt6765-pcm` (known safe, AFE probes),
(2) add `snd-soc-mt6357` alone (no card): if that reboots, the PMIC codec probe
    (first regmap writes to MT6357_DCXO_CW14 / AUDDEC_ANA_CON0) is the trigger;
(3) only then add `mt6765-mt6357`.  Also confirm the AFE IRQ: the board DTS uses
`<0 0x99 8>` (GIC_SPI 153) from the stock FDT, while earlier logs mention 196.

## Audio status, 2026-11-11 01:10 (session 2) — does not work yet

**Answer to "does audio work": no.** `/proc/asound/cards` is empty (`--- no soundcards ---`)
even with every audio module loaded, `aplay -l` lists nothing, and there is no PCM device
to open, so no playback has ever got through. A USB-C DAC still works
(`SND_USB_AUDIO=y`); nothing internal does.

### Three real defects found by diffing the vendor driver, and fixed

Porting reference: `vendor-alps/sound/soc/mediatek/mt6765/` (5,835 lines —
`mtk-auddrv-clk.c`, `mtk-auddrv-afe.h`, `mt6765-sound.c`) read against the in-tree
mainline scaffold `sound/soc/mediatek/mt6765/`. Vendor code was read, not dropped in.

| # | Defect | Symptom before | Fix |
| --- | --- | --- | --- |
| 1 | `mt6765-afe-clk.c` clock table named **gate** clocks where the AFE needs topckgen **muxes** | `set parent failed (-22), continuing` on every probe, so the AFE stayed on the wrong parent | map `TOP_AUD_SEL` -> `top_audio_sel`, `TOP_AUD1_SEL` -> `top_aud_1_sel`, `TOP_AUD_ENGEN1_SEL` -> `top_aud_engen1_sel`, `CLK26M` -> `clk26m`, matching MT8365. Verified: the `-22` no longer appears in a live probe |
| 2 | `mt6765-mt6357.c` cast `snd_soc_card_get_drvdata()` (actually `mtk_soc_card_data`) to `mt6765_mt6357_priv`, then called `pinctrl_select_state()` on a NULL pinctrl | hard oops `mt6765_mt6357_int_adda_startup+0x6c` on the first PCM open | read `soc_card_data->mach_priv` and guard every pinctrl call with `!priv->pinctrl` (4 sites) |
| 3 | the `mediatek,mt6357-sound` node had no `vaud28-supply`, while the codec probes with `devm_regulator_get_enable("vaud28")` | `Looking up vaud28-supply ... failed` / `-ENODEV`, so the codec never registered | add `vaud28-supply = <&mt6357_vaud28_reg>`; boot now logs `vaud28: 2800 mV ... enabled` |

### Current blocker: card registration hard-resets the SoC

Bisected on image `070ab18c` with PipeWire stopped, checking `/proc/uptime` and
`/proc/modules` after each stage:

| Stage | Modules insmod'ed | Result |
| --- | --- | --- |
| A | `snd-soc-mtk-common`, `snd-soc-mt6765-pcm` | survives; AFE probes `num of dai 15` |
| B | + `snd-soc-mt6357` (codec alone) | survives; `vaud28-supply` resolves, no probe failure |
| C | + `mt6765-mt6357` (card registration) | **hard reset within ~4 s** — uptime back to 31 s, 0 modules |

So the fault is inside `snd_soc_register_card()` / the DAI-link + DAPM binding. It is
**not** the AFE probe, **not** the codec probe, and **not** `aplay`.

### Why there is no trace (dead ends already tried)

- `pstore`/ramoops is registered and the DTS has `console-size = 0x40000`, but
  `/sys/fs/pstore/` is empty after every reset — no record is ever written.
- Teed `/dev/kmsg` into `/dev/pmsg0` (the persistent pmsg region) from userspace, then
  reproduced the reset: the region came back empty too, so printk never reaches
  persistence.
- `CONFIG_NETCONSOLE` is unset and no `netconsole.ko` is built, so there is no
  out-of-band console either.
- Conclusion: this looks like a bus/SError-level reset rather than an oops (`panic=5`
  would still have left a record).

### Next step (needs one of these from the owner)

1. **`CONFIG_NETCONSOLE=m`** in the image — cheapest: one build + flash, then capture
   the reset over `usb0` with `nc -u -l 6666` on the laptop.
2. **UART console** on the board, if pads are exposed.
3. **Blind source bisect** of `snd_soc_register_card()`: drop DAI links and the DAPM
   tables one at a time, one build + insmod + recovery cycle each (~1 min per reset).

Once the card survives registration: `aplay -D hw:0,0 /tmp/tone.wav` (3 s 48 kHz stereo
440 Hz tone, already on the phone), watch for D-state/load climb, and check
`grep -i afe /proc/interrupts`. Then the AFE register map (`mt6765-reg.h` vs vendor:
DAC_CON0 0x10, DAC_CON1 0x14, DL1 0x40, VUL 0x80, IRQ_MCU_CON0 0x3a0, IRQ0/IRQ6 count
0x300/0x304), the memif/IRQ tables, and the DT interrupt (`<0 0x99 8>` = GIC_SPI 153
from the stock FDT vs 196 in older logs), then speaker and headphone.

### Phone state at hand-off

- Running `out/boot-b-mainline.img` md5 **`070ab18c86082f4158ceb9f7d1fdd972`**, built with
  `CONFIG_MTK_COMBO{,_WIFI,_BT,_GPS,_FM,_CHIP_CONSYS_6765}` disabled (`out/.config` was
  backed up and restored immediately, so the other session's tree is unchanged).
- `/etc/modprobe.d/blacklist-snd-mt6765.conf` keeps all four audio modules from
  autoloading, so the phone boots clean: uptime climbing, ALSA present, touch works.
- Files touched: `sound/soc/mediatek/mt6765/mt6765-afe-clk.c`,
  `sound/soc/mediatek/mt6765/mt6765-mt6357.c`,
  `arch/arm64/boot/dts/mediatek/mt6762-tcl-cruze.dts`.
- (Corrected 2026-11-11.) An MT6765 power controller does exist (`drivers/pmdomain/mediatek/`)
  but has no audio domain, so the AFE node still gets no `power-domains`. The cmdline
  carries `pd_ignore_unused`.

## Built drivers collected, 2026-11-11 (Claude session)

`debian/drivers-built/` (see its `README.md` and `SHA256SUMS`) holds the built driver artifacts, gathered from
disk and the phone. No Freebuff agent was prompted to do this; their chat logs were only read from disk.

- `audio/`: the four internal-audio modules exactly as installed on the phone (with the clock/drvdata/vaud28
  fixes), vermagic `7.1.0-mt6765+ SMP`. Still blacklisted on the phone; loading `mt6765-mt6357` resets the SoC.
- `usb-wifi/`: the 32 USB Wi-Fi modules from `out/` (mt76, rtw88, rtl8xxxu, ath9k_htc, carl9170, zd1211rw, mt7601u).
  Not verified to load on kernel `#53`.
- `internal-wifi-vmlinux-builtin.DO-NOT-FLASH.img`: the internal MT6631 stack is built into vmlinux, not a
  module, so the only artifact is the 23:40 boot image. It boot-loops. Kept for reference only.

### Check of the Sxmo session's fallback image, 2026-11-11 (Claude session)

- `out/boot-b-recovery-noconsys.img` (md5 `ba5cadc9...`): the mainline DTB embedded at shim offset 0x1000
  really has `/consys@18002000` and `/wifi@18000000` at `status = "disabled"` (fdtget on the extracted DTB).
  The DTB in the boot image header is the stock one for LK and has no status property; that is expected.
- The vendor stack looks DT-gated: `initWlan()` only registers the AXI platform driver (`of_match_table`),
  `wmt_detect` is a platform driver too, and `CONFIG_WLAN_MTK_EMI` is unset, so the boot-time EMI MPU write
  does not run. `mtk_wcn_wmt_mpu_lock_release()` still runs unconditionally.
- The image has **never been booted**. Prefer `backups/boot_b-full-2026-11-11-kernel53.img` (the running,
  known-good #53) as the fallback; use noconsys only if the vendor Wi-Fi stack must be in the kernel.
- The Sxmo session's "audio touched the register block with its power domain off" crash cause is a guess:
  no log was captured (no pstore record).
- Leftovers on the laptop from that session: `/tmp/phone.py` (telnet 2323 helper) and
  `/tmp/recover-fastboot.sh` (+ `.log`). The fastboot watcher is no longer running.

## Audio WORKS (driver level), 2026-11-11 06:45 (Claude session)

Status: **NOT WORKING BY EAR (2026-11-11 06:50): the owner heard only STATIC when a song was played.** DMA/IRQ/amp path runs; sample data is wrong. Playback runs end-to-end through ALSA and PipeWire. `aplay -D hw:0,0` of a 3 s tone takes 3.1 s and
raises 24-25 AFE interrupts; `pw-play` to "Built-in Audio Stereo" plays through DL1. **Nobody has listened yet**
(the owner was not near the phone), so speaker/headphone output is unconfirmed by ear. Test:
`su siddh -c 'XDG_RUNTIME_DIR=/run/user/1000 pw-play /usr/local/share/sounds/test-tone.wav'`.

Root causes found and fixed (kernel tree, `sound/soc/mediatek/mt6765/`):
1. **The card-registration hard reset.** The port had replaced upstream's
   `devm_regmap_init_mmio_clk(dev, "top_audio_sel", ...)` with plain `devm_regmap_init_mmio()`. The AFE probe turns
   `audio_sel` on, then off (count 0, so really gated: clk_summary showed hardware enable `N`), and DAPM's first
   register read during card registration hit the unclocked bus and hung the SoC. Found by streaming
   `trace_pipe` + `dmesg -w` over SSH during the crash: the insmod CPU went silent right after "no pinctrl node",
   and regmap only logs a read after it completes. Fix: clocked regmap again (`mt6765-afe-pcm.c`).
2. **No period interrupts (aplay hung forever).** `irq_data[]` had the MT8365 layout. MT6765 (vendor
   `mIRQCtrlRegs`) uses: on = `AFE_IRQ_MCU_CON0` (0x3a0) bit n, fs = `AFE_IRQ_MCU_CON1` (0x2e4) bits 4n, count =
   `AFE_IRQ_MCU_CNTn`, clear/status = bit n. IRQ1-7 rewritten; IRQ8-10 still MT8365 layout (no vendor fs/count
   regs; they only matter for VUL3/TDM_IN capture). Added `AFE_IRQ_MCU_CON0/CON1/CNT0/CNT6` to `mt6765-reg.h`.
3. **Routing.** DL1 -> `O03 I05` + `O04 I06`, DL2 -> `O03 I07` + `O04 I08`, `INT ADDA O03_O04` on; codec
   `Headphone L/R Source = DAC`, `Line Out Source = Playback`, `Handset Source = Playback`, volumes 10 (= 0 dB;
   range -10..+8 dB). Saved in `/var/lib/alsa/asound.state`.
4. **Speaker amp.** Stock dtbo `fragment@34`: Awinic `aw87xxx_pa` on **i2c0 0x58**, chip id reg 0x00 = `0x59`,
   fed from the MT6357 line-out. Mainline has no driver for it. `&i2c0` is now enabled in the board DTS (image `#54`)
   and `/usr/local/bin/aw87-spk on|off` writes the stock register tables
   (`vendor_a:/firmware/aw87xxx_pid_59_music_0.bin` / `_off_0.bin`, copies in `drivers-built/audio/amp/`).
   The AFE-to-codec pins (GPIO136-143) were already in mode 1 from the bootloader, so no pinctrl was added.

Boot integration (phone): `casper-audio.service` -> `/usr/local/sbin/casper-audio-up` modprobes the four modules,
restores the mixer, switches the amp on. Guard: it touches `/var/lib/casper-audio.trying` first and skips audio on
the next boot if that file survives (i.e. the load crashed the phone), so a regression cannot boot-loop.
`/etc/modprobe.d/blacklist-snd-mt6765.conf` is kept on purpose so udev does not load the modules early.
Boot is ~60 s to `multi-user.target` because `wifi-up.service` scans first; audio comes up after that.

Not done / known limits:
- Not verified by ear (speaker, headphone jack, earpiece).
- The amp stays on while idle (`ponytail` note in the script); switch it per stream if there is hiss or drain.
- No capture/mic testing; no headphone-jack detection (accdet); no UCM profile (alsactl logs a harmless
  `failed to import hw:0 use case configuration`).
- Card name is still `mt6765-evk` (inherited from the MT8365 EVK machine driver).
- Files: everything phone-side plus the fixed `.ko` are in `drivers-built/audio/`.

### Audio follow-up, 2026-11-11 06:50-07:00: static fixed, speaker possibly damaged
- **Static cause:** PipeWire opens DL1 as `S32_LE`, but HD mode was written to MT8365's `AFE_MEMIF_PBUF_SIZE` bits,
  so `AFE_MEMIF_HD_MODE` (0x3f8) stayed 0 (16-bit). Fixed: memif `hd_reg = AFE_MEMIF_HD_MODE`,
  `hd_align_reg = AFE_MEMIF_HDALIGN` with vendor shifts (DL1 0/0, DL2 4/2, AWB 8/4, VUL 10/5, VUL2 14/7; others
  `hd_align_reg = -1`). Prepare: S16 -> hd 0; S32 -> hd 1, align **0** (vendor uses
  `AFE_WLEN_32_BIT_ALIGN_8BIT_0_24BIT_DATA` for S32_LE; align 1 sounded harsh); S24 -> hd 1, align 0.
- After the fix the owner heard the song, but it sounds **"like the speaker got blown"**. Before the fix, full-scale
  static went through the aw87xxx at the stock music gain several times, with no vmax limiter. The speaker may be
  physically damaged, or the gain stack may simply clip. **The amp is OFF and no longer enabled at boot**
  (`casper-audio-up` line commented out). Next: a quiet sine test (PipeWire ~10 %, Lineout -10 dB), plus an
  earpiece or headphone test (no amp). Ask the owner before playing anything.
- `AFE_MEMIF_PBUF_SIZE` (0x3d8) was left at `0xfbfff` by the old S32 code (it was `0xcbfff`); a reboot resets it.

## USB host fixed again (2026-11-21)

- Symptom: `casper-usb host` left musb in `a_wait_vrise` with `VBUS_ERROR ... <VBusValid` (3 retries, then the driver turns the charger boost off), nothing enumerated, with the hub AND with the original dongle. Charger side was fine (VBUS 5.0 V, REG0B=0xe2).
- Cause: PHY register U3P_U2PHYDTM1 (0x11cc086c) had lost its force bits: it read 0x626 in device mode, working value is 0x3F2F (device) / 0x3F2D (host). Not a DT or driver-source change (musb/mediatek.c, wusb3801.c untouched since Nov 10; DT has no USB changes since Nov 11).
- Fix (on the phone only, NOT in rootfs-overlay): `/usr/local/bin/casper-usb host` now writes 0x00003F2F to 0x11cc086c first (original at casper-usb.bak). Verified by hand (write, then host); the patched script itself was not re-run end to end. Proper fix would be forcing those bits in drivers/usb/musb/mediatek.c before starting the host session (needs a boot_b flash). Unchecked: whether casper-usb-auto calls this script.
- (2026-11-24: this hub DOES have a PD-only power-in port; in `host-ext` with a charger on it the phone charges and both receivers (UGREEN + Compx 25a7:fa70) enumerate.)
- Hub (Type-C to 3x USB + Ethernet) works: Genesys 05e3:0610 hub, ASIX AX88772B Ethernet (asix, 100 Mbit), UGREEN 2b89:0043 and Compx 25a7:fa70 receivers both give keyboard+mouse. No power-in port, so no charging in host mode (SY6970 cannot boost and charge).
- Ethernet: NM profile `usb-eth` (any en* iface, DHCP, route-metric 700 so Wi-Fi stays default); got <lan-ip>. Remove with `nmcli con delete usb-eth`.
- Wi-Fi (wlan0) briefly drops while the hub is active; SSH needs retries. Wi-Fi is NOT dead: -21..-26 dBm near the router, so the snapped connector may be another lead (cellular/GPS); not confirmed.
- Charge current is capped at 512 mA (constant_charge_current_max read-only); raising it needs a DT change (`ti,charge-current`) + boot_b flash. Not done.
- Battery %: no fuel gauge; the UI number is a voltage mapping and reads high while the charger is plugged in.

### Permanent USB host fix: kernel #60 (2026-11-21 09:10 MST)
- `drivers/phy/mediatek/phy-mtk-tphy.c`: `u2_phy_instance_set_mode()` now ORs `P2C_MT6765_USB_EXTRA` (BIT0|BIT3|GENMASK(13,8)) into U3P_U2PHYDTM1 for DEVICE and HOST, giving 0x3F2F / 0x3F2D. Original saved as `backups/phy-mtk-tphy.c.orig-2026-11-21`.
- Image `out/boot-b-mainline.img` md5 a07b970a69454ed89aade5153a688ad9 flashed to boot_b with deploy.sh --no-build (verified). ROLLBACK: `backups/boot-b-mainline-2026-11-13-kernel59-before-phyfix.img` (md5 660da087..., was the live boot_b before).
- Verified on the phone with the ORIGINAL script (casper-usb.bak, no PHY write): simulated loss (DTM1=0x626) -> host gives DTM1=0x3F2D devctl=0x19, back to device gives 0x3F2F devctl=0x98. NOT yet verified with the hub plugged in on build #60 (the hub worked with the manual PHY write on #59). The PHY value also survives a cold boot (0x3F2F at boot).
- The casper-usb PHY write is now redundant but harmless.
- Known: after host -> device the laptop does not see the gadget again until the cable is replugged (or the phone reboots); soft_connect toggling does not help.

## Kernel #61 (2026-11-21 evening): experimental USB host-start variants + GPU module changes
- Flashed `out/boot-b-mainline.img` md5 ed2520272d851358d2205e8d8c741d7e (rollback: `backups/boot-b-mainline-2026-11-21-kernel60-good.img`, md5 a07b970a69454ed89aade5153a688ad9; flash with deploy.sh after copying it to out/).
- `drivers/usb/musb/mediatek.c`: new runtime parameter `/sys/module/mediatek/parameters/host_seq` (default 0 = original behaviour). Bitmask of host-session start variants:
  1 drop gadget pull-up first, 2 show the controller "VBUS low" via the PHY while the session starts, 4 pulse ID high->low, 8 longer settle, 16 retry x3 while DEVCTL still says B-device.
  Each host start with host_seq != 0 logs `host start seq=N try=M: DEVCTL=xx state=...` to dmesg.
- `drivers/phy/mediatek/phy-mtk-tphy.c`: `PHY_MODE_USB_HOST` submode `MTK_TPHY_SUBMODE_VBUS_LOW` (SESSEND=1, VBUSVALID/AVALID=0); plain host/device set_mode now also restore VBUSVALID/AVALID/SESSEND. Originals in `backups/*.orig-2026-11-21`, `backups/phy-mtk-tphy.c.pre-vbuslow-2026-11-21`.
- PHY always forces VBUS valid (DTM1 0x3F2F device / 0x3F2D host), so the controller does not track real VBUS; the jam (DEVCTL 0x99, a_wait_vrise) happens when a device is already attached at session start.
- TEST PLAN (needs the user to plug things): stop casper-usb-auto; unplug the laptop cable; plug the hub (device mode, boost off); over Wi-Fi turn the boost on (i2cset -f -y 5 0x6a 0x03 0x2a),
  then for N in 1 2 3 5 9 17 ...: `echo N > host_seq; echo device > role; sleep 2; echo host > role; dmesg | tail` and see which makes DEVCTL 0x5d / lsusb show the hub.
  Then charger-on-hub: same with host_seq variants but boost off (usbtoggle ext style) and the charger plugged in. The winning value becomes the default in mediatek.c.
- GPU: casper-gpu-dt.ko now sets casper,mfgpll-hz = 520 MHz (verified after this boot: mfgpll rate 520000000).

## Charge current (2026-11-21)
Charger cap was 512 mA (driver clamps sysfs to the bootloader value read back at probe; DT has linux,read-back-settings and no ti,charge-current). Raised live and at boot by `/usr/local/bin/casper-charge` (service `casper-charge`, enabled): REG04=0x1f (~2 A, cell is 5000 mAh = 0.4C) via i2cset, input limit 2 A via sysfs. sysfs constant_charge_current still shows 512000 (regmap cache). Real current not yet measured: battery was Full (4.38 V). Check REG12 (x50 mA) or current_now while charging below ~4.3 V.

## No-debug kernel (prepared 2026-11-21 night, NOT flashed yet)
The running kernel #61 (7.1.0-mt6765+) has CONFIG_PROVE_LOCKING/LOCKDEP/LOCK_STAT/DEBUG_SPINLOCK/DEBUG_MUTEXES/DEBUG_RWSEMS/DEBUG_ATOMIC_SLEEP/DEBUG_IRQFLAGS/PROVE_RCU/
LATENCYTOP/DEBUG_PAGE_REF on (perf shows lock_acquire/lock_release; the browser UI process spends ~54% in the kernel). Release build:
- `debian/build-nodebug.sh` -> `/mnt/1tb/android/linux/out-nd` (those options off, DEBUG_INFO off, LOCALVERSION `-mt6765nd` => release `7.1.0-mt6765nd+`), modules in `out-nd/modroot`.
- Out-of-tree modules rebuilt against it (lockdep changes struct sizes, old binaries must NOT be loaded): `debian/build-modules-nd.sh` (casper-gpu-dt/ion/ashmem/mt6765-dvfs, mt6357-jack),
  `gpu/build-pvr-nd.sh` (pvrsrvkm; objects had to be deleted to force a real recompile: old binary had 8 lockdep refs, new 0), Wi-Fi via `out-wifi-nd`
  (`make O=out-wifi-nd M=drivers/misc/mediatek/connectivity modules`, config = out-nd + MTK_COMBO*). Results in `debian/drivers-nd/`.
- Boot image: `O=/mnt/1tb/android/linux/out-nd OUTIMG=$PWD/out/boot-b-nodebug.img debian/mkboot.sh` (md5 3a562ff8...). Rollback image of the current kernel:
  `debian/backups/boot-b-mainline-2026-11-21-kernel61-debug.img` (md5 ed252027...). Old pvrsrvkm: `backups/pvrsrvkm.ko.kernel61-2026-11-21`.
- Staged on the phone (new files only, old paths untouched): `/lib/modules/7.1.0-mt6765nd+/` (depmod done, mt6357-jack in extra/), `/root/ko-nd/` (gpu-dt, pvrsrvkm, ion, ashmem),
  `/usr/local/lib/casper/nd/casper-mt6765-dvfs.ko`, `/root/wifi-mods-nd/`. Loader scripts now pick the dir by `uname -r` (`*nd+` => new): `/root/android-up.sh`, `/usr/local/sbin/casper-dvfs-up`,
  `/root/wifi-mods/wifi-stage.py` (backups `*.bak-nd`).
- To flash: `IMG=$PWD/out/boot-b-nodebug.img debian/deploy.sh --no-build` (verifies boot_b md5, reboots). To go back: `IMG=debian/backups/boot-b-mainline-2026-11-21-kernel61-debug.img debian/deploy.sh --no-build`.

### Flashed 2026-11-21 22:10: no-debug kernel 7.1.0-mt6765nd+ is LIVE on boot_b (md5 3a562ff85c6258ac0d997bafeff0324a)
Booted cleanly on the first try: systemd running, no failed units, all out-of-tree modules loaded from the nd dirs (GPU, ashmem, ion, gpu-dt, Wi-Fi stack, audio, jack), wlan0 <lan-ip>, charger REG04=0x1f, no Oops/BUG/WARNING.
Measured: Firefox animation page 34-37 fps (was 30-34 sandbox-on / 20-30 sandbox-off on kernel #61); GL gralloc readback unchanged (131 fps, GPU-bound). Modest gain (~10-15%) on that test; not yet profiled elsewhere.
NOTE: /tmp is cleared at every boot (test profiles/pages there are gone). Rollback image: backups/boot-b-mainline-2026-11-21-kernel61-debug.img (that kernel needs /root/ko etc. - the loader scripts already pick dirs by uname -r).

## Hardware video decoder (started 2026-11-21/22): stage 1 + 2 DONE (H.264 decodes bit-exact)
Goal: use the MT6765 VDEC through the stock 32-bit vendor decoder stack (libvcodecdrv + libh264dec_*.ca7 / HEVC / VP8 / VP9 in /vendor/lib).
- **Stack facts:** stock kernel had `CONFIG_MTK_VIDEOCODEC_DRIVER=y` (/dev/Vcodec, no VPU/VCU firmware). The vendor library programs the decoder registers itself via mmap of
  0x17000000-0x1704ffff; the kernel does power, a HW lock, and "decode done" IRQs. Decoding ran in the 32-bit media service (Codec2 `media.c2@1.2-mediatek` -> libcodec2_mtk_vdec -> libvcodecdrv).
  The 64-bit libcodec2_mtk_vdec uses V4L2 (/dev/video, vdec-fmt) - not this SoC's path.
- **Reference source:** MediaTek's GPL MT6765 driver from the public ZTE MT6761 kernel release (github deadman96385/android_kernel_zte_mt6761, drivers/misc/mediatek/videocodec/mt6765),
  API header `vdec_drv_if_public.h` from AOSP kernel/mediatek android-mtk-3.18. Copies in `debian/src/casper-vcodec/mtk-ref/`. ioctl numbers verified against the vendor binary
  (eVideoLockHW -> ioctl 0x40044d0d, VAL_HW_LOCK_T 0x38 bytes).
- **Stage 1 (DONE): `debian/src/casper-vcodec/casper-vcodec.c`** (~470 lines): /dev/Vcodec (misc, 0666), same ioctl ABI incl. 32-bit compat, HW lock / WAITISR / IRQ ack exactly as the
  original (decoder only), mmap limited to the original windows, VDEC IRQ = sysirq SPI 233 (Linux irq ~202) enabled only while powered. Power = SMI common (mmsys) + vcodecsys
  LARB/VENC/VDEC clocks directly. **Do NOT runtime-resume SMI larb1:** mainline `mtk_smi_larb_resume` oopses (NULL in config_port) with the IOMMU disabled (first attempt wedged the module; needed a reboot).
  Test: `cat /sys/module/casper_vcodec/parameters/probe_regs` (gcon[0]=0x1111 = all vcodec clocks on), `src/casper-vcodec/vctest.c` (lock/mmap/waitisr-timeout/unlock OK).
  Module at /root/ko-nd/casper-vcodec.ko, NOT loaded at boot yet (insmod by hand).
- **casper-ion extended** (backup `src/casper-ion/casper-ion.c.pre-vcodec-2026-11-22`, live at /root/ko-nd/casper-ion.ko, loaded at boot): camera heap (mask 0x1000, `ion_alloc_camera`) =
  physically contiguous below 4 GB (dma_alloc_pages = CMA first, so >4 MB blocks work; mmap via remap_pfn_range); ION_SYS_CACHE_SYNC = clean+invalidate whole buffer; ION_CMD_SYSTEM/ION_SYS_GET_PHYS returns the physical address; ION_CMD_MULTIMEDIA (M4U config) accepted;
  real 32-bit compat (ALLOC 0xc0144900, CUSTOM 0xc0084906). 32-bit ion_sys_data: union is 8-byte aligned (sys_cmd @0, handle @8, phy @12, len @16). Live swap: `/root/ion-swap.sh`
  (stops the allocator service = only /dev/ion user, reloads, restarts it). GPU GL/Vulkan re-tested OK after the swap.
- **32-bit Android runtime in the chroot:** stock `system/lib` (492 libs) extracted with debugfs from `/mnt/1tb/tcl30se-backup/backup/logical/system_a.img` -> /srv/android/system/lib;
  stock vendor `bin` copied (only new files) into /srv/android/vendor/bin; `/srv/android/system/bin/linker` -> apex runtime 32-bit linker.
  Run 32-bit programs: `env -i LD_LIBRARY_PATH=/vendor/lib:/system/lib:/apex/com.android.vndk.v31/lib:/apex/com.android.runtime/lib/bionic:/vendor/lib/hw /usr/sbin/chroot /srv/android /data/<bin>`
  (no env in the chroot; set it outside). NDK r27c at `/mnt/1tb/android/ndk-dl/android-ndk-r27c` (armv7a-linux-androideabi31-clang).
- **Stage 2 (DONE 2026-11-22): `src/casper-vcodec/stage2/vdtest.c`** (32-bit, NDK, `-D__EARLY_PORTING__`) decodes Annex-B H.264 with libvcodecdrv: Create -> Init(SPS) -> Decode per
  access unit with a free frame buffer -> GET_TYPE_DISP_FRAME_BUFFER / FREE_FRAME_BUFFER -> Decode(NULL) flush. Results: 320x240 baseline 30/30 frames and 720p High (B-frames)
  59/60 bit-exact vs ffmpeg (60th = truncated stream); **720p High: 300 frames in 2.86 s = 105 fps** (SW VP9 720p was ~12 fps).
  `stage2/liblogshim.so` (LD_PRELOAD) prints vendor liblog to stderr; `stage2/blk2yuv.py` = MTK block format -> I420 + PSNR (Y in 16x32 tiles, CbCr NV12 in 16x16 tiles,
  chroma starts at bufW*seqH, e.g. 1280*736 - NOT the 64-aligned height).
  What it took: (1) **larb1 is gen2:** bootloader leaves all 11 `NONSEC_CON` (larb+0x380+4*port) bit0 = IOMMU -> "Fail to Fetch Barrel shifter"; casper-vcodec clears them at power-up
  (mainline's mt8167-style MMU_EN @0xfc0 is wrong for this SoC). (2) **VCODEC_SEL:** shared vdec/venc core - vendor LOCKHW writes gcon+0x20 = 2 (vdec) and gcon+0x10 = 1;
  reset gcon+0x20 = 0 at power-off. (3) Vendor struct sizes: VAL_MEMORY_T 96 bytes (__EARLY_PORTING__), VDEC_DRV_FRAMEBUF_T **176** bytes (header says 120 - pad it).
  (4) decoder wants a 5 MB contiguous bitstream buffer -> CMA (32 MB total; 720p run fits, raise cma= for bigger). Debug knob: `echo "OFF [VAL]" > /sys/module/casper_vcodec/parameters/regs; cat` (16 words in the 0x17000000 window).
- **Stage 3 (player, WORKING 2026-11-22):** `stage2/vdpipe.c` -> /srv/android/data/vdpipe (Annex-B stdin -> NV12 stdout, display order, cropped; vendor libs printf to stdout so
  frames go to a dup of fd 1 and fd 1 -> stderr). `/usr/local/bin/vdpipe` (= `src/casper-vcodec/vdpipe.sh`, chroot wrapper; sudoers `/etc/sudoers.d/vdpipe` lets siddh run it).
  `/usr/local/bin/hwplay URL|FILE [mpv opts]` (= `src/casper-vcodec/hwplay`): yt-dlp picks avc1 <=720p30 + m4a, ffmpeg -c:v copy -bsf h264_mp4toannexb | vdpipe | gpuwin mpv rawvideo
  + --audio-file. Live YouTube (Sintel 1280x546@24, muted): 0 dropped frames, avsync 0, CPU vdpipe ~33% of one core, mpv 8%, ffmpeg 8% (total ~7% of the SoC).
  Gotchas: run from the user session (su siddh keeps XDG_RUNTIME_DIR=/run/user/0 -> "Failed initializing any suitable GPU context"); --start doesn't seek (pipe).
  Boot: /root/android-up.sh (casper-gpu.service, repo gpu/android-up.sh) insmods $KO/casper-vcodec.ko + chmod 666 /dev/Vcodec (added 2026-11-22; reboot-tested OK: modules load, vdpipe output md5 == ffmpeg).
  **Seeking (2026-11-22):** `hwplay ... --start=M:SS`; in mpv LEFT/RIGHT +-10 s, DOWN/UP +-60 s, double-tap left/right third +-10 s, middle = pause, `o` = real position
  (`src/casper-vcodec/hwplay-seek.lua` -> /usr/local/share/hwplay/). A seek writes the target to a temp file and quits mpv; hwplay restarts: ffprobe finds the keyframe <= target,
  `ffmpeg -ss K` (copy), audio via `--audio-file=edl://%len%URL,K` (EDL segment = real seek in the audio stream). Verified: start 7.3 s -> first frame == ffmpeg frame at 6.0 s keyframe;
  live YouTube seeks land on keyframes, 0 drops, avsync 0.
  **Seek speed:** the ~10 s per seek was mpv opening a new Wayland window (~4 s until sway configures it + ~6 s to first presented frame);
  ffprobe+ffmpeg+vdpipe to first decoded frame is ~1.5 s. hwplay now keeps ONE mpv (`--idle --force-window`, IPC socket in $XDG_RUNTIME_DIR/hwplay.*/ipc) and
  loadfile's a new fifo per seek (audio via set_property options/audio-files = EDL, offset via change-list script-opts). Interstellar (local 720p H.264, muted):
  seek 3.4-3.7 s, 60 s playback 2 drops, avsync 0, vdpipe ~33-50% of one core. Gotcha: under `set -e`, `wait`/`pkill` on the killed pipeline return non-zero -> `|| true`.
  **VP9 (2026-11-22): no hardware on MT6765.** libvcodecdrv's VP9 path is a wrapper to libvp9dec_sa.ca7.so = MediaTek's ARM32 *software* decoder
  (VP9SwDec* symbols; refuses our stream with "NOT_SUPPORT video" anyway). Debian ffmpeg VP9 720p: 43 fps 1 thread, 90 fps 4 threads.
  hwplay now prefers avc1 and falls back to plain mpv (software, --vd-lavc-threads=4, native seeking) for anything else; HWPLAY_FORMAT overrides yt-dlp -f.
  vdpipe has `vdpipe vp9` (IVF input) for completeness. YouTube Sintel VP9 720p via fallback: 2 drops, avsync 0.
  **vdpipe CPU fixes:** (1) the vendor lib trims trailing zeros by scanning backwards from rBase.u4Size byte by byte -> advertise only the unit
  (512-aligned, zero padded) instead of the 4 MB buffer: was 61% of vdpipe; (2) NEON tile copy. Sintel 720p H.264 total CPU 60% -> 25% of one core
  (vdpipe 13, mpv 11, ffmpeg 0.5); software VP9 same clip 46%. Remaining vdpipe cost: kernel cache clean/inval (casper-ion CACHE_SYNC ignores range/type
  and does clean+inval of the whole buffer) and the tile copy. Bit-exact re-verified (seek720.mp4 600/600, 7 runs). One unreproduced flake: 4 runs right
  after the NEON rebuild gave differing whole-file md5s; 13 runs since all exact.
  Testing note: don't stream raw frames over Wi-Fi (830 MB starved ssh for minutes) - compare with `ffmpeg -f framemd5` on the phone (-map 0:v:0, -y).
  **Home-screen app (2026-11-22):** "Video Player" = `/usr/local/share/applications/hwplay.desktop` (repo `src/casper-vcodec/hwplay.desktop`, Icon=mpv,
  default handler for video/* via xdg-mime) -> `/usr/local/bin/hwplay-launch` (repo copy): bemenu touch menu = clipboard link (wl-paste) + ~/Videos,
  ~/Downloads videos + "Type or paste a link"; with an argument (Open With) plays it directly; failures -> mako notification with the yt-dlp/hwplay error.
  pocketshell scans apps only at startup (restart it to pick up new .desktop files). yt-dlp warns: no JS runtime (deno/node) -> some YouTube formats
  may be missing; install one if extraction starts failing.
  Still TODO: VP9 (libvp9dec in /vendor/lib) for non-avc1 videos; Firefox integration is a separate, larger job.
  **Next (stage 3):** a player path - e.g. small wrapper daemon/lib exposing decode to a 64-bit player (mpv/ffmpeg) or a GStreamer element; load casper-vcodec at boot; raise CMA.
- Also seen: `irq 182: nobody cared` (unrelated, Firefox era) and a pvrsrvkm WARN in pvr_counting_fence_timeline_force_complete (WaylandProxy) - both logged in out/oops/.

## USB host working again (2026-11-24 ~01:00)
- Recipe: after a fresh boot, with NOTHING plugged in: `systemctl stop casper-usb-auto; casper-usb host` (expect DEVCTL 0x19 via `busybox-rescue devmem 0x11200060 8`), THEN plug the hub. Verified: Genesys hub, UGREEN receiver (keyboard+mouse, user confirmed working), USB flash drive. A 2nd receiver on hub port 3 failed with error -32 (likely power from the phone boost).
- Any device<->host cycling, or a device already attached at host start, leaves the controller stuck as B-device (DEVCTL 0x99) until reboot; host_seq variants and PHY value changes did not help.
- Unbinding the glue (`echo 11200000.usb > /sys/bus/platform/drivers/musb-mtk/unbind`) OOPSes in usb_put_phy and removes the role switch: never do it.
- boot_b is now kernel #61 (debug, md5 ed252027). The no-debug image is saved as `backups/boot-b-mainline-2026-11-21-nodebug-3a562ff8.img`; not yet retested with the plug-after-host recipe.
- 2026-11-24 01:18: `casper-usb-boot.service` (mode file /etc/casper-usb-mode = host) VERIFIED with the hub plugged in across a reboot: hub + UGREEN receiver enumerated, DEVCTL 0x5D. casper-usb-auto disabled (backup /root/casper-usb-auto.service.bak-2026-11-24). Switch modes with `casper-usb-mode host|host-ext|device` + reboot.
- 2026-11-24: Inland 5-in-1 USB-C hub (Micro Center 748731, HDMI + PD IN 100 W) WORKS in `host-ext` with a charger on its PD IN: phone charging (online=1), Genesys hub 05e3:0610 + UGREEN receiver enumerate, plus a "GsCooLink USB BillBoard" (3679:0001) = the hub announcing its DisplayPort alt mode was not entered. HDMI will not work (no DP alt mode / PD on this phone). In plain `host` (phone boost) the Inland stays silent.

## USB hot-swap (2026-11-24, kernel nd #3, boot image md5 599412f2, copy backups/boot-b-mainline-2026-11-24-hotswap-599412f2.img)
- Cause: wusb3801 set the USB role (and boost) on every CC event: host only for a sink partner, device otherwise. Unplugging a dock (or a powered dock = source) flipped musb to device, and device->host re-entry jams (DEVCTL 0x99).
- Fix: `wusb3801.hold_role` module param (built-in, /sys/module/wusb3801/parameters/hold_role); when 1 the driver leaves role and VBUS alone. casper-usb-boot sets it before applying /etc/casper-usb-mode. Also musb `host_seq` bit 32 = PHY power cycle before a host session (for live device->host switching; NOT tested yet).
- Verified: host-ext + old hub with charger: unplug -> role stays host, DEVCTL 0x19; replug -> hub + both receivers back in ~1 s, charging resumes. No reboot. Patch: kernel/0003-usb-hold-role-hotswap.patch.
- Rollback: backups/boot-b-mainline-2026-11-21-nodebug-3a562ff8.img (the casper-usb-boot hold_role line is a no-op on it).
- 2026-11-24: live mode switching (casper-usb device -> host-ext) with host_seq=32 (PHY power cycle) does NOT work: DEVCTL 0x99, the attached keyboard died a few seconds later, reboot needed. Mode changes stay reboot-only.
