# ARM Rockchip RV1106/RV1103 Cross-Compilation Toolchain
# For Luckfox Pico

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Toolchain paths - absolute path
set(TOOLCHAIN_ROOT /home/sgf/ws/luckfox_pangofly/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_ROOT}/bin/arm-rockchip830-linux-uclibcgnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_ROOT}/bin/arm-rockchip830-linux-uclibcgnueabihf-g++)
set(CMAKE_AR ${TOOLCHAIN_ROOT}/bin/arm-rockchip830-linux-uclibcgnueabihf-ar CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB ${TOOLCHAIN_ROOT}/bin/arm-rockchip830-linux-uclibcgnueabihf-ranlib CACHE FILEPATH "Ranlib")

# Don't use sysroot for testing
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Search paths
set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_ROOT})

# Don't search for programs in the build host
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target sysroot
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Cross-compilation flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard" CACHE STRING "" FORCE)
