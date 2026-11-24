#!/bin/bash
# mod2 = mod1 + 3-button nav default. Run: sudo -E bash apply2.sh (expects mnt/ mounted from a copy of mod1)
set -euo pipefail
. /mnt/1tb/android/env.sh
G=/mnt/1tb/android/workspace/gsi; S=$G/mnt/system; B=$ANDROID_HOME/build-tools/35.0.0; W=$G/mods/build2
rm -rf $W; mkdir -p $W/res/values
KS=$W/ks.jks; keytool -genkeypair -keystore $KS -storepass android -keypass android -alias k -keyalg RSA -keysize 2048 -validity 10000 -dname CN=gsi >/dev/null 2>&1
echo '<resources><integer name="config_navBarInteractionMode">0</integer></resources>' > $W/res/values/v.xml
cat > $W/AndroidManifest.xml <<M
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="gsi.mod.nav3">
  <overlay android:targetPackage="android" android:priority="999" android:isStatic="true"/>
  <application android:hasCode="false"/>
</manifest>
M
$B/aapt2 compile --dir $W/res -o $W/res.zip
$B/aapt2 link -o $W/u.apk -I $ANDROID_HOME/platforms/android-35/android.jar --manifest $W/AndroidManifest.xml $W/res.zip
$B/zipalign -f -p 4 $W/u.apk $W/a.apk
$B/apksigner sign --v1-signing-enabled true --ks $KS --ks-pass pass:android --key-pass pass:android --out $S/product/overlay/GsiMod_nav3.apk $W/a.apk
rm -f $S/product/overlay/*.idsig; chmod 644 $S/product/overlay/GsiMod_nav3.apk
setfattr -n security.selinux -v 'u:object_r:system_file:s0' -h $S/product/overlay/GsiMod_nav3.apk
echo nav3 added
