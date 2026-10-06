cmake_minimum_required(VERSION 3.10)

set(POKY_SYSROOT "/home/htr/poky-projects/Sources/tmp/work/cortexa57-poky-linux/bluez5/5.65-r0/recipe-sysroot")
set(POKY_TOOLCHAIN_DIR "/home/htr/poky-projects/Sources/tmp/work/cortexa57-poky-linux/bluez5/5.65-r0/recipe-sysroot-native/usr/bin/aarch64-poky-linux")
set(POKY_CXX_COMPILER "${POKY_TOOLCHAIN_DIR}/aarch64-poky-linux-g++")

# Use the Yocto cross-compiler when it is present. If you intentionally want a native
# host build, pass -DCMAKE_CXX_COMPILER=/usr/bin/c++ to CMake instead.
if(EXISTS "${POKY_CXX_COMPILER}")
    set(CMAKE_CXX_COMPILER "${POKY_CXX_COMPILER}" CACHE FILEPATH "C++ compiler" FORCE)
    set(CMAKE_SYSROOT "${POKY_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH "${POKY_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
    set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
    set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
endif()

project(ble-client VERSION 1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DBus includes can come from the cross sysroot or the host system.
if(EXISTS "${POKY_SYSROOT}/usr/include/dbus-1.0")
    include_directories(
        ${POKY_SYSROOT}/usr/include/dbus-1.0
        ${POKY_SYSROOT}/usr/lib/dbus-1.0/include
    )
endif()

# Executable
add_executable(ble-client
    main.cpp
    ble_client.cpp
    ble-client.hpp
)

# Link DBus from the active toolchain environment.
if(EXISTS "${POKY_SYSROOT}/usr/lib/libdbus-1.so")
    target_link_libraries(ble-client PRIVATE "${POKY_SYSROOT}/usr/lib/libdbus-1.so")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DBUS REQUIRED dbus-1)
    target_include_directories(ble-client PRIVATE ${DBUS_INCLUDE_DIRS})
    target_link_libraries(ble-client PRIVATE ${DBUS_LIBRARIES})
endif()
