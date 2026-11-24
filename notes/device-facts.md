# TCL 30 SE (6165S) — verified device facts

Source: `adb shell getprop` / root shell, 2026-10-05, serial (redacted).

| Fact | Value | Consequence |
|---|---|---|
| Model / device | 6165S / Cruze_Pro | |
| Android | 12 (SP1A.210812.016), patch 2023-08-01 | |
| first_api_level | 31 | launched on 12 → Treble/VNDK 31 |
| ro.treble.enabled | true | GSI OK |
| ro.vndk.version | 31 | vendor frozen at 12 |
| ro.build.ab_update | true | **A/B GSI** (`*_ab` / `*-ab`) |
| ro.virtual_ab.enabled | true | Virtual A/B: flash from fastbootd |
| ro.boot.dynamic_partitions | true | `fastboot reboot fastboot` before flashing system |
| abilist | arm64-v8a,armeabi-v7a,armeabi | **arm64** (not a64) |
| platform / hardware | mt6765 / mt6762 (Helio G25) | |
| kernel | 4.19.188 | |
| slot | _a (no `_b` for system/vendor; super is shared) | |
| ro.boot.flash.locked | **0** | **bootloader already unlocked** → Step 3 skipped |
| verifiedbootstate | orange | unlocked |
| root | Magisk (`su 0 id` → uid 0, context magisk) | backup via root dd, no BROM needed |
| Battery | 63% at start | |
| userdata | 104G, 31G used | **user data present: back up before `fastboot -w`** |

## Partition layout notes
- `super` = mmcblk0p51 (11.35 GB). p52–p57 (odm_a, product_a, vendor_a, system_a,
  preload_a, oem_a) sit inside super's sector range but are stale scatter-file aliases:
  their sizes do NOT match the real logical partitions. Use backup/logical/ or super.img.
- Preloader is in mmcblk0boot0 (4 MiB). There's no `preloader` by-name entry.
- GPT: primary = first 64 sectors, secondary = last 64 sectors.
- Full sizes: `partition-sizes.txt`.
- Real logical sizes (mapper): system_a 2999812096, product_a 2150813696, vendor_a 584916992, odm_a 52428800, oem_a 1082130432, preload_a 1803993088. These are what `fastboot flash system` targets.
