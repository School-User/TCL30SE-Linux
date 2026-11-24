#!/bin/bash
# mod3 = mod1 + CasperOS naming (cosmetic only). Run: sudo -E bash gsi-mod3-rename.sh
# Expects gsi/mnt mounted read-write from a copy of mod1 (see PROJECT-STATE.md: grow, e2fsck -E unshare_blocks, mount).
# Changes: About-phone Model, Build number, flavor/description, Bluetooth/device name default (derived from model on a fresh userdata).
# Deliberately NOT touched: ro.build.fingerprint, ro.product.{name,device}, ro.build.product. Vendor code and OTA/cache logic key on
# them; they are only visible via getprop. The phh overlays, phh-*.sh and persist.sys.phh.* props are functional (MTK IMS, nav bar,
# wifi), not branding, so they stay.
set -euo pipefail
G=/mnt/1tb/android/workspace/gsi; S=$G/mnt/system
NAME="CasperOS"; BUILD="CasperOS 1.0"

sed -i -E "s|^(ro\.build\.display\.id)=.*|\1=$BUILD|; \
           s|^(ro\.build\.flavor)=.*|\1=casperos-userdebug|; \
           s|^(ro\.build\.description)=.*|\1=$BUILD release-keys|" $S/build.prop
sed -i -E "s|^(ro\.product\.product\.model)=.*|\1=$NAME|" $S/product/etc/build.prop
sed -i -E "s|^(ro\.product\.system_ext\.model)=.*|\1=$NAME|" $S/system_ext/etc/build.prop

# build.prop edits keep their SELinux label (sed -i recreates the file, so restore it)
for f in $S/build.prop $S/product/etc/build.prop $S/system_ext/etc/build.prop; do
  setfattr -n security.selinux -v 'u:object_r:system_file:s0' -h $f
done
# Boot animation baked into the image (the GSI ships none; the "android" text is the built-in default). Android looks in
# /product/media first. /system/media does not exist on this GSI and a Magisk module could not create it, so use product/media.
# Build the zip first: ~/Projects/tcl30se-gsi/bootlogo/make.sh
BOOTZIP=/home/casper/Projects/tcl30se-gsi/bootlogo/bootanimation.zip
install -d -m 755 $S/product/media
install -m 644 $BOOTZIP $S/product/media/bootanimation.zip
setfattr -n security.selinux -v 'u:object_r:system_file:s0' -h $S/product/media/bootanimation.zip

grep -E "^ro\.(build\.(display\.id|flavor|description)|product\.(product|system_ext)\.model)=" $S/build.prop $S/product/etc/build.prop $S/system_ext/etc/build.prop
ls -laZ $S/product/media/bootanimation.zip
echo "mod3 applied. After flashing: adb shell pm disable-user --user 0 me.phh.treble.app   (hides Treble Settings; undo with pm enable)"
