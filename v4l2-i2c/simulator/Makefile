#Makefile for building the GStreamer file source simulator with cross-compilation
#The cross-compiler prefix. Adjust this to match your toolchain.
CROSS_COMPILE = arm-linux-gnueabihf-
CC = $(CROSS_COMPILE)gcc

#The path to the target system's root filesystem (sysroot).
#This is crucial for finding the GStreamer libraries and headers.
#You will need to set this to the correct path on your system.
SYSROOT = /home/htr/rpi-rootfs/rootfs

#Environment variables for pkg-config to work in a cross-compilation environment.
#PKG_CONFIG_PATH points to the location of .pc files in the sysroot.
#PKG_CONFIG_SYSROOT_DIR tells pkg-config to prepend the sysroot path.
PKG_CONFIG_PATH := (SYSROOT)/usr/lib/pkgconfig:(SYSROOT)/usr/share/pkgconfig
PKG_CONFIG_SYSROOT_DIR := $(SYSROOT)

Use pkg-config to get the necessary compiler and linker flags for the GStreamer libraries.
The 'env' command ensures pkg-config uses the correct paths.
GSTREAMER_CFLAGS := (shell env PKG_CONFIG_PATH=(PKG_CONFIG_PATH) PKG_CONFIG_SYSROOT_DIR=(PKG_CONFIG_SYSROOT_DIR) pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0)
GSTREAMER_LIBS := $(shell env PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) PKG_CONFIG_SYSROOT_DIR=(PKG_CONFIG_SYSROOT_DIR) pkg-config --libs gstreamer-1.0 gstreamer-app-1.0)

The target executable to be built
TARGET = sim_filesrc

Combine all compiler and linker flags
CFLAGS = -Wall -g $(GSTREAMER_CFLAGS)
LDFLAGS = $(GSTREAMER_LIBS)

The 'all' target is the default. It builds the simulator executable.
all: $(TARGET)

Rule to build the executable from the source file.
$< is the first prerequisite (simulator.c).
$@ is the target name (simulator).
$(TARGET): simulator.c
$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

The 'clean' target removes the compiled executable.
clean:
rm -f $(TARGET)

Declare phony targets to prevent conflicts with file names.
.PHONY: all clean
