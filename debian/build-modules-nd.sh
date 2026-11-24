#!/bin/bash
# Rebuild the small out-of-tree modules against the no-debug kernel (out-nd) in scratch copies; results in debian/drivers-nd/.
set -euo pipefail
D=/home/casper/Projects/tcl30se-gsi/debian; K=/mnt/1tb/android/linux/out-nd; OUT=$D/drivers-nd
mkdir -p $OUT
build() {  # name srcdir objname
  local t; t=$(mktemp -d); cp $2/*.c $t/; cp $2/*.h $t/ 2>/dev/null || true; echo "obj-m := $3.o" > $t/Makefile
  [ -f $2/Makefile ] && grep -q "obj-m" $2/Makefile && cp $2/Makefile $t/Makefile
  make -C $K M=$t ARCH=arm64 LLVM=1 CROSS_COMPILE=aarch64-linux-gnu- modules 2>&1 | grep -E "error|Error|LD \[M\]" || true
  cp $t/$3.ko $OUT/ && echo "ok $3 -> $(modinfo -F vermagic $OUT/$3.ko)"; rm -rf $t
}
build gpu-dt  $D/src/casper-gpu-dt   casper-gpu-dt
build ion     $D/src/casper-ion      casper-ion
build ashmem  $D/src/casper-ashmem   casper-ashmem
build dvfs    $D/src/casper-dvfs     casper-mt6765-dvfs
build jack    $D/src/mt6357-jack     mt6357-jack
