#!/bin/sh
# Bring up the minimal Android userspace chroot (/srv/android) used for the PowerVR vendor GPU libraries.
R=/srv/android
KO=/root; case "$(uname -r)" in *nd+) KO=/root/ko-nd;; esac
for m in casper_gpu_dt pvrsrvkm casper_ion; do lsmod | grep -q "^$m " || true; done
lsmod | grep -q casper_gpu_dt || insmod $KO/casper-gpu-dt.ko
lsmod | grep -q pvrsrvkm || insmod $KO/pvrsrvkm.ko
lsmod | grep -q casper_ion || insmod $KO/casper-ion.ko
lsmod | grep -q casper_ashmem || insmod $KO/casper-ashmem.ko
lsmod | grep -q casper_vcodec || insmod $KO/casper-vcodec.ko   # hardware video decoder (/dev/Vcodec), used by vdpipe/hwplay
chmod 666 /dev/Vcodec 2>/dev/null
for m in dev proc sys; do mountpoint -q $R/$m || mount --bind /$m $R/$m; done
mkdir -p /dev/binderfs; mountpoint -q /dev/binderfs || mount -t binder binder /dev/binderfs
python3 - <<PY
import fcntl, os, struct
fd = os.open("/dev/binderfs/binder-control", os.O_RDONLY)
for n in ("binder", "hwbinder", "vndbinder"):
    if not os.path.exists("/dev/binderfs/" + n):
        buf = struct.pack("256sII", n.encode(), 0, 0)
        fcntl.ioctl(fd, 0xc1086201, buf)   # BINDER_CTL_ADD = _IOWR('b', 1, struct binderfs_device)
PY
for n in binder hwbinder vndbinder; do ln -sf binderfs/$n /dev/$n; done
chmod 666 /dev/binderfs/* 2>/dev/null
chmod 666 /dev/pvr_sync 2>/dev/null
[ -e /srv/android/system/vendor ] || ln -s /vendor /srv/android/system/vendor   # stock Android has /system/vendor -> /vendor; without it libIMGegl cannot find libGLESv*_mtk and crashes in eglGetConfigs
pgrep -f logd.py >/dev/null || setsid -f python3 /root/logd.py >/dev/null 2>&1 </dev/null
mkdir -p $R/dev/socket
run() { setsid -f chroot $R /data/run "$@" >>/tmp/android-svc.log 2>&1 </dev/null; }
for p in $(ps -eo pid,args | grep -E "[l]inker64 /(system|vendor)/bin" | awk "{print \$1}"); do kill -9 $p; done
sleep 1
run /system/bin/servicemanager
run /system/bin/hwservicemanager
sleep 2
run /vendor/bin/hw/android.hardware.graphics.allocator@4.0-service-mediatek
sleep 2
echo "android chroot up"
