#!/bin/bash
# Build debian/out/boot-b-mainline.img for the TCL 30 SE (slot B).
# LK gets the STOCK dtb (its fdt fix-ups assert on anything else, mt_boot.c:1050 etc.).
# The kernel gets OUR dtb: a stub in front of the Image loads x0 with a copy embedded in the shim.
# Layout of Image.shim (loaded by LK at 0x40080000, the only address it accepts):
#   0x000000 arm64 Image header (text_offset 0x80000) -> stub at 0x40
#   0x001000 mainline dtb
#   0x180000 real Image (= 0x40200000, 2 MB aligned as mainline needs)
set -euo pipefail
P=/home/casper/Projects/tcl30se-gsi; D=$P/debian; O=${O:-/mnt/1tb/android/linux/out}; OUTIMG=${OUTIMG:-$D/out/boot-b-mainline.img}
IMAGE=$O/arch/arm64/boot/Image
DTB=$O/arch/arm64/boot/dts/mediatek/mt6762-tcl-cruze.dtb
STOCK_DTB=$P/bpf/unpacked/dtb
T=$(mktemp -d); trap 'rm -rf $T' EXIT
K=/mnt/1tb/android/linux/linux-mt6765

# The first-stage initramfs is built INTO the kernel (CONFIG_INITRAMFS_SOURCE); the kernel never sees LK's ramdisk pointer.
( cd $D/initramfs && find . | cpio -o -H newc -R 0:0 --quiet ) > $D/out/initramfs.cpio
make -C $K O=$O ARCH=arm64 LLVM=1 -j"$(nproc)" Image mediatek/mt6762-tcl-cruze.dtb 2>&1 | grep -E "^  (LD|OBJCOPY|DTC) +(vmlinux|arch/arm64/boot)|rror|\*\*\*" || true
[ $IMAGE -nt $D/out/initramfs.cpio ]

[ "$(stat -c %s $DTB)" -lt $((0x17f000)) ]
cat > $T/shim.S <<EOF
	.text
_start:
	b	stub			/* code0 */
	nop				/* code1 */
	.quad	0x80000			/* text_offset LK insists on */
	.quad	_end - _start		/* image_size */
	.quad	0xa			/* flags: LE, 4K pages, anywhere */
	.quad	0, 0, 0
	.ascii	"ARM\x64"
	.long	0
stub:
	adr	x0, dtb			/* replace LK's dtb pointer with ours */
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	b	image
	.org	0x1000
dtb:
	.incbin	"$DTB"
	.org	0x180000
image:
	.incbin	"$IMAGE"
_end:
EOF
clang --target=aarch64-linux-gnu -c $T/shim.S -o $T/shim.o
llvm-objcopy -O binary -j .text $T/shim.o $D/out/Image.shim
# self-check: header magic, dtb magic at 0x1000, real Image header at 0x180000
python3 - $D/out/Image.shim <<'E'
import sys,struct
d=open(sys.argv[1],'rb').read()
assert d[0x38:0x3c]==b'ARM\x64' and struct.unpack('<Q',d[8:16])[0]==0x80000
assert d[0x1000:0x1004]==b'\xd0\x0d\xfe\xed', 'dtb magic'
assert d[0x180038:0x18003c]==b'ARM\x64', 'real Image'
assert struct.unpack('<I',d[0:4])[0]==0x14000010, 'b stub'
print('shim ok', hex(len(d)))
E
gzip -9 -n -c $D/out/Image.shim > $D/out/Image.shim.gz
gzip -9 -n -c $D/out/initramfs.cpio > $D/out/initramfs.cpio.gz   # only LK looks at this copy
mkbootimg --header_version 2 --os_version 12.0.0 --os_patch_level 2023-08 \
  --kernel $D/out/Image.shim.gz --ramdisk $D/out/initramfs.cpio.gz --dtb $STOCK_DTB \
  --pagesize 0x800 --base 0 --kernel_offset 0x40080000 --ramdisk_offset 0x51b00000 \
  --tags_offset 0x47880000 --dtb_offset 0x47880000 \
  --cmdline 'bootopt=64S3,32N2,64N2 buildvariant=user JRD_VERSION_MARK_B1AD4040CX00' \
  -o $OUTIMG
ls -la $OUTIMG
