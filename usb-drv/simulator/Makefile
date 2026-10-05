#Makefile for building the GStreamer file source simulator with cross-compilation
#The cross-compiler prefix. Adjust this to match your toolchain.
CROSS_COMPILE = arm-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc

#The path to the target system's root filesystem (sysroot).
#This is crucial for finding the GStreamer libraries and headers.
#You will need to set this to the correct path on your system.
SYSROOT = /home/htr/rpi-rootfs/rootfs


#The target executable to be built
TARGET = simulator

#Combine all compiler and linker flags
CFLAGS = -Wall -g 

#The 'all' target is the default. It builds the simulator executable.
all: $(TARGET)

#Rule to build the executable from the source file.
#$< is the first prerequisite (simulator.c).
#$@ is the target name (simulator).
$(TARGET): simulator.c
	$(CC) $(CFLAGS) $< -o $@

#The 'clean' target removes the compiled executable.
clean:
	rm -f $(TARGET)

#Declare phony targets to prevent conflicts with file names.
.PHONY: all clean
