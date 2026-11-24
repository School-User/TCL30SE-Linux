#!/bin/sh
# Hardware H.264 decode: Annex-B on stdin -> NV12 frames on stdout (runs the 32-bit vendor decoder in the Android chroot).
exec env -i LD_LIBRARY_PATH=/vendor/lib:/system/lib:/apex/com.android.vndk.v31/lib:/apex/com.android.runtime/lib/bionic:/vendor/lib/hw /usr/sbin/chroot /srv/android /data/vdpipe
