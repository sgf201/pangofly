# Toolchain file for cross-compiling to ARM (RV1106)
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=toolchain-arm.cmake ..

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain path
set(TOOLCHAIN_PATH /home/sgf/ws/luckfox_pangofly/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf)
set(CROSS_PREFIX ${TOOLCHAIN_PATH}/bin/arm-rockchip830-linux-uclibcgnueabihf-)

set(CMAKE_C_COMPILER ${CROSS_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${CROSS_PREFIX}g++)

set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_PATH})

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Don't look for programs in the sysroot
set(CMAKE_SKIP_RPATH TRUE)

# Specify sysroot if needed
set(SYSROOT_PATH ${TOOLCHAIN_PATH}/arm-rockchip830-linux-uclibcgnueabihf/sysroot)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --sysroot=${SYSROOT_PATH} -muclibc -O2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --sysroot=${SYSROOT_PATH} -muclibc -O2 -std=c++17")

# Add sysroot to link flags
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --sysroot=${SYSROOT_PATH}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} --sysroot=${SYSROOT_PATH}")

message(STATUS "Cross-compiling for ARM (RV1106)")
message(STATUS "Toolchain: ${TOOLCHAIN_PATH}")
message(STATUS "Sysroot: ${SYSROOT_PATH}")