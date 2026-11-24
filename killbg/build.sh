#!/bin/bash
# Gradle-less build. Order matters: zipalign BEFORE apksigner (aligning a signed APK kills the v2 sig).
set -euo pipefail
. /mnt/1tb/android/env.sh
cd "$(dirname "$0")"
J=$ANDROID_HOME/platforms/android-35/android.jar
rm -rf out && mkdir -p out/cls out/dex
aapt2 compile --dir res -o out/res.zip
aapt2 link -o out/base.apk -I "$J" --manifest AndroidManifest.xml out/res.zip
javac --release 8 -cp "$J" -d out/cls $(find src -name '*.java')
d8 --lib "$J" --output out/dex $(find out/cls -name '*.class')
(cd out/dex && zip -q ../base.apk classes.dex)
zipalign -f 4 out/base.apk out/aligned.apk
if [ ! -f ks.jks ]; then   # password lives in ks.pass (chmod 600), never printed
  head -c 18 /dev/urandom | base64 > ks.pass; chmod 600 ks.pass
  keytool -genkeypair -keystore ks.jks -alias k -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=killbg" -storepass:file ks.pass -keypass:file ks.pass
fi
apksigner sign --ks ks.jks --ks-pass file:ks.pass --out killbg.apk out/aligned.apk
apksigner verify killbg.apk && echo BUILD OK
