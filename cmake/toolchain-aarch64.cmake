# cmake/toolchain-aarch64.cmake
# Cross-compilation toolchain file for AArch64 (e.g., Raspberry Pi 4 / Cortex-A72).
#
# Prerequisites on the build host (Ubuntu/Debian):
#   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#
# Usage:
#   cmake -B build-aarch64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64.cmake \
#     -DCMAKE_BUILD_TYPE=Release \
#     -DBENCH_ENABLE_NEON=ON \
#     -DBENCH_ENABLE_PERF=ON
#
# To run on the target board, copy the build-aarch64/ binaries and run them
# there directly. perf counters require the ARMv8 PMU to be accessible
# (see docs/hardware_notes/arm_cortex_a72.md).

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Compiler binaries -- adjust if your cross-toolchain has a different prefix
set(CMAKE_C_COMPILER   aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR           aarch64-linux-gnu-ar)
set(CMAKE_RANLIB       aarch64-linux-gnu-ranlib)

# Optional: point at a sysroot for the target distribution
# set(CMAKE_SYSROOT /path/to/aarch64-sysroot)
# set(CMAKE_FIND_ROOT_PATH /path/to/aarch64-sysroot)

# Search for programs in the build host directories, but search for headers
# and libraries in the sysroot only.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# AArch64-specific compiler flags.
# Set via CMAKE_*_FLAGS_INIT rather than add_compile_options() because
# add_compile_options() in a toolchain file runs before the compiler is
# probed and can corrupt the CMake try_compile checks.
# -mcpu=cortex-a72 targets the RPi4; change for other boards (e.g. cortex-a55).
# On AArch64, NEON and hard-float ABI are always enabled; no extra flags needed.
set(ARCH_FLAGS "-mcpu=cortex-a72")
set(CMAKE_C_FLAGS_INIT   "${ARCH_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARCH_FLAGS}")
