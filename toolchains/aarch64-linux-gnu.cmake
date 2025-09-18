# CMake toolchain file for cross-compiling to ARM64 (aarch64) on Linux
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=toolchains/aarch64-linux-gnu.cmake ..

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross-compilation tools
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR aarch64-linux-gnu-ar)
set(CMAKE_STRIP aarch64-linux-gnu-strip)
set(CMAKE_RANLIB aarch64-linux-gnu-ranlib)

# PKG-CONFIG setup for cross-compilation
set(ENV{PKG_CONFIG_LIBDIR} /usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig)
set(ENV{PKG_CONFIG_SYSROOT_DIR} /)
set(CMAKE_PKG_CONFIG_EXECUTABLE /usr/bin/pkg-config)

# Search paths for libraries and headers
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Additional flags for cross-compilation stability
set(CMAKE_C_FLAGS_INIT "-fPIC")
set(CMAKE_CXX_FLAGS_INIT "-fPIC")

# Explicitly set architecture for CPack
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE arm64)
set(BZPERI_DEB_ARCH arm64)