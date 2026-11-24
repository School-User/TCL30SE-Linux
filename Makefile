GSI ?= images/aosp-arm64-ab-vanilla-15.0-20241108.img
.PHONY: backup flash rollback rollback-hard

# Root dd dump + sha256 verify of every partition except userdata.
backup:
	./backup.sh

# Bootloader is already unlocked (ro.boot.flash.locked=0), so there's no unlock target.
# Virtual A/B + dynamic partitions: vbmeta from the bootloader, system from fastbootd.
flash:
	adb reboot bootloader
	fastboot --disable-verity --disable-verification flash vbmeta backup/vbmeta_a.img
	fastboot reboot fastboot
	fastboot flash system $(GSI)
	fastboot reboot bootloader
	fastboot format:ext4 md_udc
	fastboot -w
	fastboot reboot

# Soft: restore the stock logical system + vbmeta, then wipe.
rollback:
	fastboot flash vbmeta backup/vbmeta_a.img
	fastboot reboot fastboot
	fastboot flash system backup/logical/system_a.img
	fastboot reboot bootloader
	fastboot format:ext4 md_udc
	fastboot -w
	fastboot reboot

# Hard: write the raw super dump back from the bootloader (super is a physical partition).
rollback-hard:
	fastboot flash super backup/super.img
	fastboot flash boot_a backup/boot_a.img
	fastboot flash vbmeta_a backup/vbmeta_a.img
	fastboot reboot bootloader
	fastboot format:ext4 md_udc
	fastboot -w
	fastboot reboot
