#!/bin/bash
# Gradle-less build. Order matters: zipalign BEFORE apksigner (aligning a signed APK kills the v2 sig).
set -euo pipefail
. /mnt/1tb/android/env.sh
cd "$(dirname "$0")"
J=$ANDROID_HOME/platforms/android-35/android.jar
rm -rf out && mkdir -p out/cls out/dex
aapt2 link -o out/base.apk -I "$J" --manifest AndroidManifest.xml
javac --release 8 -cp "$J" -d out/cls src/local/home/*.java
d8 --lib "$J" --output out/dex $(find out/cls -name '*.class')
(cd out/dex && zip -q ../base.apk classes.dex)
zipalign -f 4 out/base.apk out/aligned.apk
if [ ! -f ../killbg/ks.jks ]; then   # password lives in ../killbg/ks.pass (chmod 600), never printed
  head -c 18 /dev/urandom | base64 > ../killbg/ks.pass; chmod 600 ../killbg/ks.pass
  keytool -genkeypair -keystore ../killbg/ks.jks -alias k -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=home" -storepass:file ../killbg/ks.pass -keypass:file ../killbg/ks.pass
fi
apksigner sign --ks ../killbg/ks.jks --ks-pass file:../killbg/ks.pass --out home.apk out/aligned.apk
apksigner verify home.apk && echo BUILD OK
