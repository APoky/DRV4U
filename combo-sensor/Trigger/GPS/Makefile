#To build set PATH: for compiler path
#export PATH='/home/htr/poky-projects/Sources/tmp/work/qemuarm64-poky-linux/linux-yocto/5.15.179+gitAUTOINC+896128cbf5_2bf9cba52f-r0/recipe-sysroot-native/usr/bin/aarch64-poky-linux':$PATH

# Sets the name of the kernel module object file to build.
# NOTE: This assumes your source file is named cs-driver.c
##obj-m := iio-gps-drv.o # i2c-envcombo-simu.o   
iio-gps-drv-y := iio_triggered_buffer.o
# Define the path to the Yocto kernel source directory (V5.15)
# This path points to the build directory created by BitBake for qemuarm64.
KDIR := /home/htr/poky-projects/Sources/tmp/work/qemuarm64-poky-linux/linux-yocto/5.15.179+gitAUTOINC+896128cbf5_2bf9cba52f-r0/linux-qemuarm64-standard-build

#it's important other it will be not compiled
#cp ${KBUILD_DIR}/Module.symvers .

# Define the cross-compilation settings for your target (QEMU AARCH64)
ARCH := arm64
CROSS_COMPILE := aarch64-poky-linux-

# The 'all' target uses the explicit Yocto paths and cross-compiler settings
all:
	make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	make -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean

.PHONY: all clean

