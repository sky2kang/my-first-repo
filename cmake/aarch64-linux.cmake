# CMake toolchain file for cross-compiling to the Zynq UltraScale+ MPSoC
# (aarch64 Cortex-A53) target running embedded Linux.
#
# Usage:
#   cmake -S . -B build/target -DORU_TARGET=ON \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux.cmake
#
# Prefer sourcing the PetaLinux/Yocto SDK environment instead; this file is
# a minimal fallback when using a standalone Linaro/GCC aarch64 toolchain.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Adjust the prefix to match your installed toolchain.
set(CROSS_PREFIX "aarch64-linux-gnu-" CACHE STRING "cross toolchain prefix")

set(CMAKE_C_COMPILER   "${CROSS_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${CROSS_PREFIX}g++")

# If you have a target sysroot, point to it here:
# set(CMAKE_SYSROOT /path/to/sysroot)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
