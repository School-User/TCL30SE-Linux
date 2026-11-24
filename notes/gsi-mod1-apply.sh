#!/bin/bash
# Apply GSI mods to the mounted working image. Idempotent. Run: sudo -E bash apply.sh
set -euo pipefail
. /mnt/1tb/android/env.sh
G=/mnt/1tb/android/workspace/gsi; S=$G/mnt/system; B=$ANDROID_HOME/build-tools/35.0.0
LAWN=$HOME/Projects/tcl30se-gsi/images/Lawnchair.15.0.0.Beta.3.0.apk
W=$G/mods/build; rm -rf $W; mkdir -p $W

# --- 1. Lawnchair as priv-app (replaces Trebuchet/Launcher3QuickStep)
rm -rf $S/system_ext/priv-app/TrebuchetQuickStep $S/product/overlay/GsiLauncher3Overlay.apk
install -d -m 755 $S/system_ext/priv-app/Lawnchair
install -m 644 $LAWN $S/system_ext/priv-app/Lawnchair/Lawnchair.apk  # no zipalign: it breaks the v2 signature
{ echo '<?xml version="1.0" encoding="utf-8"?><permissions><privapp-permissions package="app.lawnchair">'
  $B/aapt2 dump permissions $LAWN | sed -n "s/^uses-permission: name='\(.*\)'.*/\1/p" | sort -u | sed 's|.*|<permission name="&"/>|'
  echo '</privapp-permissions></permissions>'; } > $S/system_ext/etc/permissions/privapp-permissions-lawnchair.xml
cat $S/system_ext/etc/permissions/com.android.launcher3.xml >/dev/null  # left in place, harmless for absent pkg

# --- 2. RRO helper: mk_rro <name> <target> <res xml file contents dir>
KS=$W/ks.jks; keytool -genkeypair -keystore $KS -storepass android -keypass android -alias k -keyalg RSA -keysize 2048 -validity 10000 -dname CN=gsi >/dev/null 2>&1
mk_rro() { # name target values.xml
  local n=$1 t=$2 d=$W/$1; mkdir -p $d/res/values
  cp $3 $d/res/values/v.xml
  cat > $d/AndroidManifest.xml <<M
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="gsi.mod.$n">
  <overlay android:targetPackage="$t" android:priority="999" android:isStatic="true"/>
  <application android:hasCode="false"/>
</manifest>
M
  $B/aapt2 compile --dir $d/res -o $d/res.zip
  $B/aapt2 link -o $d/u.apk -I $ANDROID_HOME/platforms/android-35/android.jar --manifest $d/AndroidManifest.xml $d/res.zip
  $B/zipalign -f -p 4 $d/u.apk $d/a.apk
  $B/apksigner sign --ks $KS --ks-pass pass:android --key-pass pass:android --out $S/product/overlay/GsiMod_$n.apk $d/a.apk
  chmod 644 $S/product/overlay/GsiMod_$n.apk
}
cat > $W/fw.xml <<X
<resources><string name="config_recentsComponentName">app.lawnchair/com.android.quickstep.RecentsActivity</string></resources>
X
cat > $W/sp.xml <<X
<resources><fraction name="def_window_animation_scale">50%</fraction><fraction name="def_window_transition_scale">50%</fraction></resources>
X
mk_rro recents android $W/fw.xml
mk_rro anim com.android.providers.settings $W/sp.xml

# --- 3. Debloat
for a in priv-app/DeviceAsWebcam priv-app/BuiltInPrintService priv-app/MusicFX priv-app/LiveWallpapersPicker priv-app/E2eeContactKeysProvider \
         priv-app/LocalTransport priv-app/SharedStorageBackup priv-app/CallLogBackup priv-app/AvatarPicker \
         app/PrintSpooler app/PrintRecommendationService app/EasterEgg app/Traceur app/BookmarkProvider app/PartnerBookmarksProvider \
         app/WallpaperBackup app/HTMLViewer app/BasicDreams app/CameraExtensionsProxy; do
  rm -rf $S/$a
done
for a in Music PhotoTable Calendar Browser2 ThemesStub; do rm -rf $S/product/app/$a; done
rm -rf $S/system_ext/app/SystemUIClocks-{BigNum,Calligraphy,Flex,Growth,Inflate,Metro,NumOverlap,Weather}
# unused themed overlays (fonts/icon packs/cutout emulation)
rm -rf $S/product/overlay/{Font*,IconPack*,DisplayCutoutEmulation*}
echo applied
