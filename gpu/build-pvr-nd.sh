#!/bin/sh
# Trial build of TI's DDK 1.13@5776728 kernel module (nohw system layer) against our 7.1 tree, to count porting errors.
set -u
TOP=$HOME/Projects/tcl30se-gsi/gpu/ti-src/ti113
export PATH=/tmp/claude-1000/shim:$PATH
export KCFLAGS="-isystem /usr/lib/clang/23/include -Wno-error"
cd $TOP/build/linux/mt6765_linux
ARGS="BUILD=release RGX_BVNC=22.87.104.18 KERNELDIR=/mnt/1tb/android/linux/out-nd ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KERNEL_CROSS_COMPILE=aarch64-linux-gnu- KERNEL_CC=clang KERNEL_LD=ld.lld KERNEL_NM=llvm-nm KERNEL_OBJCOPY=llvm-objcopy LLVM=1"
make -j8 kbuild $ARGS >/tmp/claude-1000/ddk-nd-gen.log 2>&1   # first run generates the tree (fails at .NOTINTERMEDIATE/symlink step)
K=$TOP/binary_mt6765_linux_wayland_release/target_aarch64/kbuild
# pre-create the source symlinks the old implicit-rule chain used to make
( cd $TOP && find services kernel hwdefs include -name '*.c' ) | while read -r f; do
  mkdir -p "$K/$(dirname "$f")"; [ -e "$K/$f" ] || ln -s "$TOP/$f" "$K/$f"
done
find $TOP/build/linux/mt6765_linux -path '*generated*' -name '*.c' 2>/dev/null | head -0
G=$(find $TOP -type d -path '*generated/rogue' | head -1)
[ -n "$G" ] && ( cd "$G" && find . -name "*.c" ) | while read -r f; do
  mkdir -p "$K/generated/rogue/$(dirname "$f")"; [ -e "$K/generated/rogue/$f" ] || ln -s "$G/$f" "$K/generated/rogue/$f"
done
make -k -j8 kbuild $ARGS >/tmp/claude-1000/ddk-nd-build.log 2>&1
echo "rc=$? errors=$(grep -c 'error:' /tmp/claude-1000/ddk-nd-build.log)"
