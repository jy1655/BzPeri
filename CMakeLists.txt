cmake_minimum_required(VERSION 3.16)
project(bzperi
    VERSION 1.0.0
    DESCRIPTION "BzPeri - Modern C++20 Bluetooth LE GATT server using BlueZ over D-Bus"
    LANGUAGES CXX)

# Set C++20 standard with Linux optimizations
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Require out-of-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please create a separate build directory.")
endif()

# Linux-only project validation
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(WARNING "This project is optimized for Linux. BlueZ functionality requires Linux.")
    message(WARNING "Current platform: ${CMAKE_SYSTEM_NAME}")
    message(WARNING "Proceeding with limited functionality...")
    set(CROSS_COMPILE TRUE)
else()
    set(LINUX TRUE)
    message(STATUS "Configuring for Linux (primary target platform)")
endif()

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_STANDALONE "Build standalone server example" ${LINUX})
option(BUILD_TESTING "Build tests" OFF)
option(ENABLE_BLUEZ_ADVANCED "Enable advanced BlueZ 5.77+ features" ${LINUX})
option(ENABLE_PERFORMANCE_OPTIMIZATION "Enable Linux-specific performance optimizations" ${LINUX})

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# Linux-specific dependencies (required for full functionality)
if(LINUX)
    # Core BlueZ D-Bus dependencies
    pkg_check_modules(GLIB REQUIRED glib-2.0>=2.58)
    pkg_check_modules(GIO REQUIRED gio-2.0>=2.58)
    pkg_check_modules(GOBJECT REQUIRED gobject-2.0>=2.58)

    # Check for BlueZ development headers
    find_path(BLUEZ_INCLUDE_DIR
        NAMES bluetooth/bluetooth.h bluetooth/hci.h
        PATHS /usr/include /usr/local/include
        DOC "BlueZ development headers")

    if(BLUEZ_INCLUDE_DIR)
        message(STATUS "Found BlueZ headers: ${BLUEZ_INCLUDE_DIR}")
        add_compile_definitions(HAVE_BLUEZ_HEADERS)
        include_directories(${BLUEZ_INCLUDE_DIR})
    else()
        message(WARNING "BlueZ headers not found. Install libbluetooth-dev for full functionality.")
    endif()

    # Check for modern BlueZ features (5.77+)
    if(ENABLE_BLUEZ_ADVANCED)
        # TODO: Add version-specific feature detection
        add_compile_definitions(BLUEZ_ADVANCED_FEATURES)
        message(STATUS "Advanced BlueZ features enabled")
    endif()

    # Linux performance optimizations
    if(ENABLE_PERFORMANCE_OPTIMIZATION)
        add_compile_definitions(LINUX_PERFORMANCE_OPTIMIZATION)
        message(STATUS "Linux performance optimizations enabled")
    endif()

else()
    # Cross-compilation or development mode
    message(WARNING "Non-Linux platform detected - BLE functionality will be disabled")
    add_compile_definitions(NO_GLIB NO_BLUEZ CROSS_COMPILE)
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
        add_compile_definitions(DEBUG)
    else()
        add_compile_options(-O2 -DNDEBUG)
    endif()
endif()

# Check for std::format availability
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <format>
    #include <string>
    int main() {
        std::string result = std::format(\"test {}\", 42);
        return 0;
    }
" HAS_STD_FORMAT)

if(HAS_STD_FORMAT)
    add_compile_definitions(HAS_STD_FORMAT=1)
else()
    add_compile_definitions(HAS_STD_FORMAT=0)
    message(STATUS "std::format not available, using fallback implementation")
endif()

# Include directories
include_directories(include)
include_directories(src)

# Configure header
configure_file(
    "${CMAKE_SOURCE_DIR}/src/config.h.in"
    "${CMAKE_BINARY_DIR}/config.h"
    @ONLY
)
include_directories("${CMAKE_BINARY_DIR}")

# Source files
set(BZPERI_SOURCES
    src/StructuredLogger.cpp
    src/DBusInterface.cpp
    src/DBusMethod.cpp
    src/DBusObject.cpp
    src/GattCharacteristic.cpp
    src/GattDescriptor.cpp
    src/GattInterface.cpp
    src/GattProperty.cpp
    src/GattService.cpp
    src/Gobbledegook.cpp
    src/Init.cpp
    src/Logger.cpp
    src/FormatCompat.cpp
    src/ServerTypes.cpp
    src/Server.cpp
    src/ServerUtils.cpp
    src/Utils.cpp
)

# Platform-specific sources
if(LINUX)
    list(APPEND BZPERI_SOURCES
        src/BluezAdapter.cpp
        src/BluezTypes.cpp
        src/BluezAdvertisement.cpp
    )
endif()

# Create library
if(BUILD_SHARED_LIBS)
    add_library(bzperi SHARED ${BZPERI_SOURCES})
    set_target_properties(bzperi PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
    )
else()
    add_library(bzperi STATIC ${BZPERI_SOURCES})
endif()

# Library properties
set_target_properties(bzperi PROPERTIES
    OUTPUT_NAME bzp
    PUBLIC_HEADER "include/BzPeri.h;include/Gobbledegook.h"
)

# Link libraries
target_link_libraries(bzperi PRIVATE Threads::Threads)

if(LINUX)
    target_link_libraries(bzperi PRIVATE
        ${GLIB_LIBRARIES}
        ${GIO_LIBRARIES}
        ${GOBJECT_LIBRARIES}
    )
    target_include_directories(bzperi PRIVATE
        ${GLIB_INCLUDE_DIRS}
        ${GIO_INCLUDE_DIRS}
        ${GOBJECT_INCLUDE_DIRS}
    )
    target_compile_options(bzperi PRIVATE
        ${GLIB_CFLAGS_OTHER}
        ${GIO_CFLAGS_OTHER}
        ${GOBJECT_CFLAGS_OTHER}
    )
endif()

# Standalone executable
if(BUILD_STANDALONE AND LINUX)
    add_executable(standalone src/standalone.cpp)
    target_link_libraries(standalone bzperi)
    set_target_properties(standalone PROPERTIES
        OUTPUT_NAME bzp-standalone
    )
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS bzperi
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(TARGET standalone)
    install(TARGETS standalone
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

# Create pkg-config file
configure_file(
    "${CMAKE_SOURCE_DIR}/bzperi.pc.in"
    "${CMAKE_BINARY_DIR}/bzperi.pc"
    @ONLY
)

install(FILES "${CMAKE_BINARY_DIR}/bzperi.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# Print configuration summary
message(STATUS "")
message(STATUS "BzPeri Configuration Summary:")
message(STATUS "  Version:           ${PROJECT_VERSION}")
message(STATUS "  Build type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard:      C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Shared libraries:  ${BUILD_SHARED_LIBS}")
message(STATUS "  Build standalone:  ${BUILD_STANDALONE}")
message(STATUS "  Platform:          ${CMAKE_SYSTEM_NAME}")
message(STATUS "  std::format:       ${HAS_STD_FORMAT}")
if(LINUX)
    message(STATUS "  GLib version:      ${GLIB_VERSION}")
    message(STATUS "  GIO version:       ${GIO_VERSION}")
endif()
message(STATUS "")
