#!/bin/sh
# Build the libhybris "casper" EGL platform on the phone (the build tree /root/build has the libhybris sources and Android headers).
# usage on the phone: sh build-on-phone.sh eglplatform_casper.cpp /tmp/out.so   then install over BOTH names atomically:
#   cp out.so lib/x.new; ln -f x.new y.new; mv x.new eglplatform_casper.so; mv y.new eglplatform_wayland.so  (gpuwin loads the wayland name)
cd /tmp
INC="-I/root/build/hdr -I/root/build/libhybris/hybris/include -I/usr/local/hybris/include/hybris/eglplatformcommon -I/usr/local/hybris/include/hybris/platformcommon -I/usr/local/hybris/include/hybris -I/usr/local/hybris/include"
DEFS="-include /root/build/hdr/android-config.h -D_Nullable= -D_Nonnull= -D_Null_unspecified= -D__INTRODUCED_IN(x)= -D__INTRODUCED_IN_32(x)= -D__INTRODUCED_IN_64(x)= -D__ANDROID_API__=30"
g++ -std=gnu++17 -O2 -fPIC -shared $DEFS $INC ${1:-eglplatform_casper.cpp} -o ${2:-/tmp/casper_orig_test.so} -L/usr/local/hybris/lib -lhybris-common -lhybris-platformcommon -lhybris-eglplatformcommon -lgralloc -lsync -lwayland-client -lpthread 2>&1 | grep -E "error|undefined|Error" | head -8
ls -l ${2:-/tmp/casper_orig_test.so} 2>&1 | tail -1
