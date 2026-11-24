# 2026-11-11 23:15: TESTED. Two reboots: Wi-Fi up from boot, wlan0 activated at ~27 s, phone reachable by
SSH over Wi-Fi (root@<lan-ip>). MAC was random per boot (IP drifted .78/.79/.80); fixed by pinning
802-11-wireless.cloned-mac-address=xx:xx:xx:xx:xx:xx in the NM profile <home-ssid> (permaddr is
xx:xx:xx:xx:xx:xx). Results: HTTPS 3/3 ~0.12 s, 60 s ping to router 0% loss (avg 9 ms), LAN 30 MB at
~0.98 MB/s, internet ~1.5 MB/s. Throughput is low vs 72-86 Mbit link: open item.

# 2026-11-11 22:10: AUTO AT BOOT. casper-wifi.service (oneshot, Before=NetworkManager) runs
/usr/local/sbin/casper-wifi-up: stages 1-3, starts casper-wmt-launcher.service, stage 4, echo 1.
Crash guard: /var/lib/casper-wifi-boot survives a crashed boot -> renamed .crashed and Wi-Fi is skipped
until someone removes it. Verified by reboot: wlan0 rejoined <home-ssid> ~13 s after boot. DNS/HTTPS
lookups timed out for the first ~minute after connecting (2 of 3), then 5/5 OK; not reproduced since.
Unit files + script copies: drivers-built/internal-wifi/. Old USB-dongle wifi-up.service still enabled
(scan-only, no /root/wifi.conf).

# 2026-11-11 22:00: CONNECTED. NetworkManager profile "<home-ssid>" on wlan0, DHCP <lan-ip>, HTTPS works
(9.6 MB at ~570 kB/s; link 72/86 Mbit/s, -28 dBm). Phone fixes made with it: clock set + chrony enabled
(RTC was April); usb0 gateway demoted to [Route] Metric=1000 in /etc/systemd/network/10-usb0.network
(backup /root/10-usb0.network.bak) so Wi-Fi is the default route. Driver still loaded by hand: after a
reboot rerun the sequence below; NM then reconnects by itself.

# 2026-11-11 night: INTERNAL WI-FI SCANS. wlan0 up, `iw dev wlan0 scan` lists ~11 APs (home "<home-ssid>" -26 dBm)

BTIF solved without the vendor driver: `common/common_main/linux/btif_shim.c` (in wmt_drv.ko) registers
BTIF (0x1100c000, GIC SPI 112 via mt6577-sysirq, clock infracfg CLK_IFR_BTIF) as a mainline 8250
PORT_MTK_BTIF port (ttyS1), opens it in-kernel, puts an N_DEVELOPMENT ldisc on it that feeds STP, and
implements the 12 mtk_wcn_btif_* calls. stp_btif.o is back in the build. CRUCIAL FIX: BTIF THRE means
"TX FIFO <= threshold", so tx_loadsz must be 16-8=8 (vendor: write FIFO_SIZE - THRE); with 8250's
default 16 the PDA patch download silently lost bytes and the MCU never answered the WMT reset.
Also FAKELCR=0 and TRI_LVL = TX 8 | RX 1<<4 after termios (vendor hal_btif_hw_init).
Debug knobs (module params of wmt_drv): rxdump/txdump (hex of BTIF bytes), wmt_dbg, stp_dbg.
Launcher stand-in now sends SET_STP_MODE 0x23 at start.

Full bring-up (from a fresh boot), on the phone:
    for s in 1 2 3 4; do /root/wifi-mods/wifi-stage.py $s; done   # 4 = insmod wlan_6765_axi
    nohup /root/wifi-mods/wmt-launcher.py > /root/wifi-mods/launcher.log 2>&1 &
    echo 1 > /dev/wmtWifi        # -> wlan0, p2p0, swlan0
(order used in the working run: 1,2,3, launcher, then 4, then echo 1.)
Known: at insmod of wlan_6765_axi a first wlanProbe hits halEnableSlpProt timeout ->
glResetTrigger queues on a not-yet-created workqueue (WARN at workqueue.c:2377). Harmless so far.
wmt_drv cannot be rmmod'ed after BTIF use (ldisc ref) -> reboot to reload.
Not done: connecting (needs PSK), boot automation, DTS cleanup (power-domains, consys_mem @0xbe000000,
btif node), MPU, BT.

# 2026-11-11 evening: MODULE route, CONNSYS powers on and the MCU boots (supersedes "Next" below)

Built as out-of-tree-style modules against the RUNNING kernel #54 (no reflash): separate dir
`/mnt/1tb/android/linux/out-wifi` = phone's /proc/config.gz + `MTK_COMBO=m, MTK_COMBO_COMM=m,
MTK_COMBO_WIFI=m, CONSYS_6765=y` (only vendor symbols differ; `select WIRELESS_EXT/WEXT_PRIV` removed
from the vendor Kconfig because WIRELESS_EXT changes struct net_device). Build:
`make O=../out-wifi ARCH=arm64 LLVM=1 M=drivers/misc/mediatek/connectivity modules` (objects land in
the source dir). Modules + helper scripts: `debian/drivers-built/internal-wifi/` (stripped copies in
`stripped/`), on the phone in `/root/wifi-mods/` (NEVER /lib/modules: the DT nodes are enabled, so
autoload would recreate the boot hang).

Port fixes made for module mode (all marked `port:`):
- `obj-y` -> `obj-$(CONFIG_...)` in common/, wlan/adaptor/, wlan/core/gen4m/, gps/ Makefiles.
- common/Makefile: `MTK_PLATFORM` derived from MTK_COMBO_CHIP, and the `"CONSYS_%"` filter unquoted
  (7.x auto.conf strings have no quotes). BEFORE THIS, mt6765.c WAS NEVER COMPILED: the earlier
  "builds" used the empty __weak consys_ic_ops_mt6765, which is also why probe returned -1.
- `-Dioremap_nocache=ioremap`; saved_command_line (Samsung board-id) dropped; module_init/exit for
  wmt_cdev_wifi.c and gl_init.c under `#ifdef MODULE`.
- mtk_wcn_consys_hw.c: `emi_phys=` module param (Android put CONNSYS EMI at 0xbe000000; our DT
  allocates 0xbd000000); idempotent platform_driver register/unregister + unregister on wmt_drv exit.
- mt6765.c: "conn" clock was the vendor scpsys CONN power domain. Now genpd
  MT6765_POWER_DOMAIN_CONN attached with of_genpd_add_device() + runtime PM (DT has no
  power-domains yet). vcn33 regulator names fixed to mainline "vcn33-bt"/"vcn33-wifi".

Bring-up sequence (wifi-stage.py 1/2/3 then launcher, then `echo 1 > /dev/wmtWifi`):
1 insmod connadp, wmt_drv emi_phys=0xbe000000  2 /dev/wmtdetect ioctls in Android wmt_loader order:
CONNSYS_SOC_HW_INIT(10), GET_SOC_CHIP_ID(3)=0x6765, SET_CHIP_ID(1), MODULE_CLEANUP(5),
DO_MODULE_INIT(4) (returns -2 = BT/GPS absent, fine)  3 insmod wmt_wifi_chrdev  then
`wmt-launcher.py` (stand-in for /vendor/bin/wmt_launcher, reverse-engineered: answers srh_rom_patch /
srh_patch on /dev/stpwmt; ROM patch info = 8 bytes at file offset 24, addr bytes 0..3 with byte0
zeroed, type = byte 7).

RESULT: CONN domain on, chip id 0x10020300 / HW 0x8a00, ROM patches (ram_bt type0 @0x80000,
ram_mcu type4 @0, ram_wifi type3 @0x140000) copied into EMI, MCU runs (0x18002600 = 0x1d1e),
CONSYS-HW-PWR-ON finish(0). No bus hang, no reboot.
NEXT BLOCKER: "WMT-CORE: no hif info!" -> launcher must send WMT_IOCTL_SET_STP_MODE (0x4004a005)
arg 0x23 (STP_BTIF_FULL | FM_COMM<<4). After that STP opens over BTIF, and there is NO BTIF driver
(stp_btif.o excluded). Need the vendor drivers/misc/mediatek/btif (4.19, MT6765 plat) ported as a
module. Not on disk; get it from a public MT6765 4.19 kernel tree.

# Internal Wi-Fi (MT6631 / CONNSYS) mainline port — status 2026-11-10 23:40

**State: the vendor stack compiles and links into `vmlinux`.** `make ... vmlinux` exits 0
(418,166,912 bytes, `CONFIG_MTK_COMBO=y`) with 2,003 `mtk_*`/`wlan*`/`kal_*` text symbols,
including `wlanProbe`, `mtk_cfg_add_key` and `mtk_wmt_probe`. The board DTS now carries the
`consys@18002000`, `wifi@18000000` and reserved-memory nodes, and `make ... dtbs` builds clean.
Nothing is flashed; the phone still runs md5 `282458d0…`. Boot-time probing is NOT tested.

Vendor source: `/mnt/1tb/android/linux/mtk-connectivity` (MediaTek external module tree,
imported from SM-A055F, 4.19.x only, MT6765+MT6768 supported).
Installed into the kernel as `drivers/misc/mediatek/connectivity` (its Makefile expects
`$(srctree)/drivers/misc/mediatek/connectivity`).

## Architecture (what has to exist)

| Layer | Vendor path | State |
|---|---|---|
| gen4m WLAN driver (cfg80211, 220 .c / 289 .h) | `wlan/core/gen4m` | builds |
| WLAN adaptor (power-on + `/dev/wmtWifi`) | `wlan/adaptor` | builds |
| WMT core (chip control, firmware download) | `common/common_main` | builds (BT/UART extras below) |
| CONNSYS per-SoC power-on (MT6765) | `common/common_main/platform/mt6765.c` (1376 lines) | builds |
| HIF: SDIO (not used on this SoC) | `wlan/core/gen4m/os/linux/hif/sdio` | builds, unused |
| HIF: AXI/PDMA | `wlan/core/gen4m/os/linux/hif/axi` | builds |
| EMI / MPU (chip DMA into reserved DRAM) | `common/common_main/platform/mtk_wcn_consys_hw.c` | shim only, NOT functional |
| clkbuf / PMIC / EMI-MPU / mt-plat | vendor kernel internals | shims (`.../include/mt-plat/`) |

## Compile work — done

- **cfg80211 op conversion** (the bulk): all ops now use the mainline signatures —
  `struct wireless_dev *` instead of `struct net_device *` for keys/stations/AP,
  `int link_id` on key, station, TDLS-mgmt, start-radar, stop-AP, set-bitrate-mask,
  `cfg80211_ap_update` for `.change_beacon` (`&info->beacon`), `radio_idx` on
  `set_tx_power`/`get_tx_power`/`set_wiphy_params`, `unsigned int link_id` on
  `del_station`/`set_bitrate_mask`. `struct station_parameters`' `ht_capa`, `vht_capa`,
  `supported_rates[_len]` moved under `link_sta_params`. Callbacks taking `wdev` derive
  `struct net_device *ndev = wdev ? wdev->netdev : NULL;` at the top of the body.
- **Removed/renamed kernel APIs** adapted: `set_fs()`/`vfs_read()` → `kernel_read()`;
  `strlcpy` → `strscpy`; `netif_rx_ni` → `netif_rx`; `del_timer(_sync)` → `timer_delete(_sync)`;
  `from_timer` → `timer_container_of` (compat macro in `osal.h`); `class_create(THIS_MODULE,…)`;
  `MODULE_SUPPORTED_DEVICE`; `MODULE_IMPORT_NS(VFS_internal…)`; `dev->dev_addr` writes →
  `dev_addr_set()`; `dev->tx_dropped`/`rx_dropped` → `stats`; `sched_entity.statistics`
  (zeroed with TODO); `netif_napi_add` arity; platform `.remove` must return void
  (`mtk_wmt_remove`, `mtk_axi_remove`); `n_tty_ioctl_helper`, `tty_register_ldisc`,
  `tty_ldisc_ops.{num,magic}`, `receive_buf`/`write_wakeup`/`ioctl`, `tty_port.low_latency`;
  `alarm_start_relative` → `alarm_start_timer(…, true)`, `alarmtimer_restart` → `void`;
  `FB_EVENT_BLANK` (gone); `thermal_zone_of_device_ops` registration skipped;
  `ENUM_ACPI_STATE` vs `<acpi/actypes.h>` macro clash; `RPS_MAP_SIZE`/`struct rps_map`
  internals (helper stubbed to `-EOPNOTSUPP`); `cfg80211_ch_switch_notify(dev, chandef, 0)`;
  `cfg80211_new_sta`/`del_sta` now take a `wireless_dev`; `list_sort` comparator `const`.
- **Deliberately not ported** (each documented in place):
  BT — `common_main/linux/stp_btif.o` is out of the build because it needs the separate
  vendor `btif` driver; the STP/BTM core keeps its weak stubs, and `mtk_btif_exp.h` provides
  the types. FM — `fmradio/` is out of the build (needs `CONFIG_MTK_FM_CHIP`).
  UART transport is kept but its tty glue is updated (unused on this SOC part).
- **Shims** (`drivers/misc/mediatek/include/mt-plat/`): `mtk_wcn_cmb_stub.h`,
  `mtk_connectivity_compat.h` (pm_callback_t, msdc SDIO ops, `enum COMBO_IF`,
  `mt_combo_plt_*_deep_idle` prototypes), `mtk_disp_notify.h` (inert), `mtk_btif_exp.h`
  (types only), `mtk-clkbuf-bridge.h`, `mtk_clkbuf_ctl.h`, `mt_emi_api.h`, `cpu_ctrl.h`,
  `topo_ctrl.h`, `upmu_common.h`, plus `include/linux/of_gpio.h`,
  `include/linux/soc/mediatek/mtk-pm-qos.h`.
- Vendor warning policy lives in the connectivity Makefile
  (`-Wno-missing-prototypes -Wno-implicit-fallthrough -Wno-strict-prototypes
  -Wno-null-pointer-subtraction -Wno-unused-variable -Wno-array-bounds -Wno-format
  -Wno-unneeded-internal-declaration -Wno-cast-function-type-strict`).

## Still non-functional on purpose

- Clock buffer, PMIC voltage control, EMI MPU programming, CPU/DDR boost, thermal zone,
  legacy GPIO-number API, display-notify power saving, deep-idle handshake (no-op),
  RPS tuning, BT. Each is marked `TODO(port)` at the site.

## Next: hard problems, in order

0. **DTS — done (2026-11-10 23:40).** `mt6762-tcl-cruze.dts` has `consys@18002000`
   (`mediatek,mt6765-consys`, 13 reg windows, IRQs SPI 267/269/270, `pmic`, `memory-region`),
   `wifi@18000000` (`mediatek,wifi`, AXI window, SPI 268), `consys_mem` (4 MB,
   `mediatek,consys-reserve-memory`) and `wifi_mem` (3 MB, `shared-dma-pool`). Values are this
   board's stock DTB. `conn_clk` is a fixed-clock stand-in for the missing SCP_SYS_CONN clock.
   `.../platform/mtk_wcn_consys_rmem.c` registers the reserved-memory compatible so
   `of_reserved_mem_lookup()` works.
   **Wi-Fi here is AXI/PDMA, not SDIO** (stock `msdc@11240000` is `no-sdio`; vendor HIF=axi),
   so no msdc port is needed.
1. **Firmware — recovered (2026-11-10 23:30).** The stock vendor partition backup
   (`/mnt/1tb/tcl30se-backup/backup/vendor_a.img`, ext4) still has the connac set; it is staged in
   `debian/firmware/`: `WIFI_RAM_CODE_soc1_0_1_1.bin` (the exact name `connac.c` builds),
   `WMT_SOC.cfg`, the MCU/Wi-Fi RAM headers and `txpowerctrl.cfg`. Copy them to `/lib/firmware` on
   the phone (`request_firmware()` path). This handset's NVRAM (`WIFI`, `WIFI_CUSTOM`) came from
   `nvdata.img:/APCFG/APRDEB` read-only and is staged too, but gen4m only consumes it through a
   test-mode userspace write, so it is optional for bring-up.
2. **EINT / connectivity-combo**: the stock DTB has no `mediatek,connectivity-combo` node, so the
   Wi-Fi EINT GPIO number is unknown. Need it from a vendor board dts (or by tracing the stock
   kernel) before the out-of-band wakeup path works; in-band HIF interrupts may be enough for
   bring-up.
3. **EMI MPU**: connsys DMA needs its reserved region unprotected, or firmware download dies with
   a bus error even when everything else works.
4. **Power/clocks**: replace `conn_clk` with the real `MT6765_POWER_DOMAIN_CONN` power domain and
   the AP2CONN gates; the vendor sequence also pokes SPM/TOP_RGU directly (ported, untested).
5. **Chip type**: `wmt_lib_init()` forces `WMT_CHIP_TYPE_SOC` when detection never ran (no
   launcher/combo node); without it the SOC-gated init paths are skipped. Missing `WMT_SOC.cfg` is
   now a warning, not a failure.
6. **First hand-load test**: boot with the nodes, confirm the consys probe logs, the EMI base
   address is non-zero, and the chip ID reads back before touching firmware download.

## Build commands

    make -C /mnt/1tb/android/linux/linux-mt6765 O=/mnt/1tb/android/linux/out \
         ARCH=arm64 LLVM=1 -j16 vmlinux        # exits 0 as of 2026-11-10 23:13

Config verified after the build: `PGTABLE_LEVELS=4`, `ARM64_VA_BITS=48`, `MTK_COMBO=y`,
`MACH_MT6765=y`, `CFG80211=y`, `CFG80211_WEXT=y`, `WLAN=y`.

## Do not

- Do not flash an image built with this tree yet: `vmlinux` links, but CONNSYS power-on writes
  SPM/EMI registers and a wrong sequence can hang boot. Load and test the path by hand first
  (the same rule as the AFE port).
