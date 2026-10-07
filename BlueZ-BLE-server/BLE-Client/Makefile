# Raspberry Pi BLE Client Makefile (64-bit Aarch64)

# Cross-compiler from Poky (using the one in recipe-sysroot-native which has all tools)
POKY_BIN_DIR = /home/htr/poky-projects/Sources/tmp/work/cortexa57-poky-linux/bluez5/5.65-r0/recipe-sysroot-native/usr/bin/aarch64-poky-linux
CXX = $(POKY_BIN_DIR)/aarch64-poky-linux-g++

# Sysroot from Poky
SYSROOT = /home/htr/poky-projects/Sources/tmp/work/cortexa57-poky-linux/bluez5/5.65-r0/recipe-sysroot

# Compiler and linker flags
CXXFLAGS = -Wall -Wextra -std=c++17 --sysroot=$(SYSROOT)
LDFLAGS = --sysroot=$(SYSROOT) -ldbus-1

# DBus include paths within the sysroot
DBUS_INCLUDES = -I$(SYSROOT)/usr/include/dbus-1.0 \
                -I$(SYSROOT)/usr/lib/dbus-1.0/include

TARGET = ble-client
SRCS = main.cpp ble_client.cpp
OBJS = $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $(TARGET)..."
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "Built $(TARGET) for aarch64"

%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) $(DBUS_INCLUDES) -c $< -o $@

clean:
	@echo "Cleaning up..."
	rm -f $(OBJS) $(TARGET)
