obj-m := casper-ashmem.o
K ?= /mnt/1tb/android/linux/out
all:
	make -C $(K) M=$(PWD) ARCH=arm64 LLVM=1 CROSS_COMPILE=aarch64-linux-gnu- modules
