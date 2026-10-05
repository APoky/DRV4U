#before run make to run in terminal the following env:
# export ARCH=arm
# export CROSS_COMPILE=arm-linux-gnueabihf-

# Define the path to the Raspberry Pi kernel source tree
KSRC = /home/htr/rpi-rootfs/rootfs/kernelPI3plus/linux

# Specify the target architecture for cross-compilation
#ARCH = $(ARCH)

# Specify the cross-compilation toolchain prefix
#CROSS_COMPILE = $(CROSS_COMPILE) #arm-linux-gnueabihf-

# Define the object file for the kernel module
obj-m := v4l2-i2c-real.o

# The all target for building the kernel module
# The -C option changes the directory to $(KSRC)
# The M=$(PWD) option tells the kernel build system where to find your module source
all:
	@echo "Building kernel module for $(ARCH) architecture..."
	$(MAKE) -C $(KSRC) M=$(PWD) modules

# The clean target for removing build artifacts
clean:
	@echo "Cleaning up..."
	$(MAKE) -C $(KSRC) M=$(PWD) clean
	@rm -f Module.symvers modules.order
	@rm -f *.o *.ko *.mod.c

