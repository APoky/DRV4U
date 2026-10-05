cmake_minimum_required(VERSION 3.10)
project(ble-client VERSION 1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Cross-compilation settings (can also be provided via a toolchain file)
set(POKY_SYSROOT "/home/htr/poky-projects/Sources/tmp/work/cortexa57-poky-linux/bluez5/5.65-r0/recipe-sysroot")
set(CMAKE_SYSROOT ${POKY_SYSROOT})

# DBus Includes
# Note: In a full system, you'd use find_package(PkgConfig) and pkg_check_modules(DBUS dbus-1)
# but here we manually point to the sysroot paths for reliability.
include_directories(
    ${POKY_SYSROOT}/usr/include/dbus-1.0
    ${POKY_SYSROOT}/usr/lib/dbus-1.0/include
)

# Executable
add_executable(ble-client 
    main.cpp 
    ble_client.cpp 
    ble-client.hpp
)

# Libraries
# Manually link the dbus library from the sysroot
target_link_libraries(ble-client 
    PRIVATE 
    ${POKY_SYSROOT}/usr/lib/libdbus-1.so
)

# Optional: Set the compiler if not provided by environment or toolchain file
if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "/home/htr/poky-projects/Sources/tmp/work/cortexa57-poky-linux/bluez5/5.65-r0/recipe-sysroot-native/usr/bin/aarch64-poky-linux/aarch64-poky-linux-g++")
endif()
