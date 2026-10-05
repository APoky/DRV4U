# Path to the kernel build directory
KDIR := /home/htr/poky-projects/Sources/tmp/work/qemuarm64-poky-linux/linux-yocto/5.15.179+gitAUTOINC+896128cbf5_2bf9cba52f-r0/linux-qemuarm64-standard-build

# Target architecture and cross-compiler
ARCH := arm64
CROSS_COMPILE := aarch64-poky-linux-

# Module to build
obj-m := dma-sim-drv-qemu.o
#EXTRA_CFLAGS := -Wno-error=incompatible-pointer-types -Wno-pointer-sign
# Build target
all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE)  modules

# Clean target
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
    
