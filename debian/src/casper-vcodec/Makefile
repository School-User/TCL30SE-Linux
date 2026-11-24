obj-m := casper-vcodec.o
K ?= /mnt/1tb/android/linux/out-nd
all:
	make -C $(K) M=$(PWD) ARCH=arm64 LLVM=1 CROSS_COMPILE=aarch64-linux-gnu- modules
