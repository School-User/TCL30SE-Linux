#!/bin/bash
# Build a release kernel (no lock/IRQ/page debugging) into a SEPARATE output dir so the current kernel + modules stay as rollback.
# Release string becomes 7.1.0-mt6765nd+ (own /lib/modules dir). Usage: build-nodebug.sh   (log: debian/out/build-nd.log)
set -euo pipefail
K=/mnt/1tb/android/linux/linux-mt6765; SRC=/mnt/1tb/android/linux/out; O=/mnt/1tb/android/linux/out-nd
mkdir -p $O
[ -f $O/.config ] || cp $SRC/.config $O/.config
S=$K/scripts/config
for o in PROVE_LOCKING PROVE_RAW_LOCK_NESTING PROVE_RCU LOCK_STAT DEBUG_RT_MUTEXES DEBUG_SPINLOCK DEBUG_MUTEXES DEBUG_WW_MUTEX_SLOWPATH \
         DEBUG_RWSEMS DEBUG_LOCK_ALLOC LOCKDEP DEBUG_LOCKDEP DEBUG_ATOMIC_SLEEP DEBUG_IRQFLAGS LATENCYTOP DEBUG_PAGE_REF DEBUG_MEMORY_INIT \
         SOFTLOCKUP_DETECTOR WQ_WATCHDOG PM_DEBUG DEBUG_INFO DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT; do $S --file $O/.config -d $o; done
$S --file $O/.config -e DEBUG_INFO_NONE --set-str LOCALVERSION "-mt6765nd"
make -C $K O=$O ARCH=arm64 LLVM=1 olddefconfig
grep -E "^CONFIG_(PROVE_LOCKING|LOCKDEP|DEBUG_SPINLOCK|DEBUG_MUTEXES|DEBUG_ATOMIC_SLEEP|LOCALVERSION)[= ]" $O/.config || true
# initramfs is built into the kernel (CONFIG_INITRAMFS_SOURCE): refresh it like mkboot.sh does
D=/home/casper/Projects/tcl30se-gsi/debian
( cd $D/initramfs && find . | cpio -o -H newc -R 0:0 --quiet ) > $D/out/initramfs.cpio
make -C $K O=$O ARCH=arm64 LLVM=1 -j"$(nproc)" Image modules mediatek/mt6762-tcl-cruze.dtb
make -C $K O=$O ARCH=arm64 LLVM=1 INSTALL_MOD_PATH=$O/modroot INSTALL_MOD_STRIP=1 modules_install
echo "BUILD DONE: $(cat $O/include/config/kernel.release)"
