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

# Optional: override version from CI/tag
set(BZPERI_VERSION_OVERRIDE "" CACHE STRING "Override project/package version (e.g., from git tag)")
set(BZPERI_EFFECTIVE_VERSION ${PROJECT_VERSION})
if(BZPERI_VERSION_OVERRIDE)
    set(BZPERI_EFFECTIVE_VERSION ${BZPERI_VERSION_OVERRIDE})
endif()

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
include_directories(samples)

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
    src/BluezPeripheral.cpp
    src/Init.cpp
    src/Logger.cpp
    src/FormatCompat.cpp
    src/ServerTypes.cpp
    src/Server.cpp
    src/ServiceRegistry.cpp
    samples/SampleServices.cpp
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
        VERSION ${BZPERI_EFFECTIVE_VERSION}
        SOVERSION 1
    )
else()
    add_library(bzperi STATIC ${BZPERI_SOURCES})
endif()

# Library properties
set_target_properties(bzperi PROPERTIES
    OUTPUT_NAME bzp
    PUBLIC_HEADER "include/BzPeri.h;include/Gobbledegook.h;include/BzPeriConfigurator.h"
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
    add_executable(standalone samples/standalone.cpp)
    target_link_libraries(standalone bzperi)
    set_target_properties(standalone PROPERTIES
        OUTPUT_NAME bzp-standalone
    )
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS bzperi
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT libraries
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT libraries
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT libraries
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT headers
)

# Install bzp/ subdirectory headers
install(DIRECTORY include/bzp/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/bzp COMPONENT headers
    FILES_MATCHING PATTERN "*.h"
)

if(TARGET standalone)
    install(TARGETS standalone
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT tools
    )
endif()

# Create pkg-config file
configure_file(
    "${CMAKE_SOURCE_DIR}/bzperi.pc.in"
    "${CMAKE_BINARY_DIR}/bzperi.pc"
    @ONLY
)

install(FILES "${CMAKE_BINARY_DIR}/bzperi.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig COMPONENT headers
)

# Install D-Bus policy file
if(LINUX)
    # Check for modern D-Bus policy directory
    if(EXISTS "/usr/share/dbus-1/system.d")
        set(DBUS_POLICY_DIR "/usr/share/dbus-1/system.d")
        message(STATUS "Using modern D-Bus policy directory: ${DBUS_POLICY_DIR}")
    else()
        set(DBUS_POLICY_DIR "/etc/dbus-1/system.d")
        message(STATUS "Using legacy D-Bus policy directory: ${DBUS_POLICY_DIR}")
    endif()

    install(FILES "${CMAKE_SOURCE_DIR}/dbus/com.bzperi.conf"
        DESTINATION ${DBUS_POLICY_DIR} COMPONENT libraries
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )

    # Install BlueZ configuration script
    install(PROGRAMS "${CMAKE_SOURCE_DIR}/scripts/configure-bluez-experimental.sh"
        DESTINATION /usr/share/bzperi COMPONENT libraries
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
endif()

# CPack configuration for Debian packaging
set(CPACK_PACKAGE_NAME "bzperi")
set(CPACK_PACKAGE_VERSION ${BZPERI_EFFECTIVE_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modern C++20 Bluetooth LE GATT server library")
set(CPACK_PACKAGE_DESCRIPTION "BzPeri is a modern C++20 Bluetooth LE GATT server library for Linux systems using BlueZ over D-Bus. It provides an elegant DSL-style interface for creating and managing Bluetooth LE services.")
set(CPACK_PACKAGE_CONTACT "Jy <daunfun@gmail.com>")
set(CPACK_PACKAGE_VENDOR "BzPeri Project")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/jy1655/BzPeri")

# Debian package configuration
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Jy <daunfun@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# Auto-detect or override architecture
set(BZPERI_DEB_ARCH "" CACHE STRING "Override Debian package architecture (e.g., amd64, arm64)")
if(NOT BZPERI_DEB_ARCH)
    # Auto-detect architecture based on CMAKE_SYSTEM_PROCESSOR
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(BZPERI_DEB_ARCH "arm64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(BZPERI_DEB_ARCH "amd64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|armv7l)$")
        set(BZPERI_DEB_ARCH "armhf")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i386|i686)$")
        set(BZPERI_DEB_ARCH "i386")
    else()
        # Fallback: try to detect using dpkg-architecture if available
        find_program(DPKG_ARCH_CMD dpkg-architecture)
        if(DPKG_ARCH_CMD)
            execute_process(
                COMMAND ${DPKG_ARCH_CMD} -qDEB_HOST_ARCH
                OUTPUT_VARIABLE DETECTED_DEB_ARCH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
            )
            if(DETECTED_DEB_ARCH)
                set(BZPERI_DEB_ARCH "${DETECTED_DEB_ARCH}")
                message(STATUS "Auto-detected Debian architecture via dpkg-architecture: ${BZPERI_DEB_ARCH}")
            else()
                set(BZPERI_DEB_ARCH "amd64")
                message(WARNING "Failed to auto-detect architecture, defaulting to amd64")
            endif()
        else()
            set(BZPERI_DEB_ARCH "amd64")
            message(WARNING "dpkg-architecture not found, defaulting to amd64")
        endif()
    endif()
    message(STATUS "Auto-detected Debian architecture: ${BZPERI_DEB_ARCH} (CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR})")
endif()
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${BZPERI_DEB_ARCH}")

# Dependencies
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.34), libgcc-s1 (>= 3.0), libstdc++6 (>= 11), libglib2.0-0 (>= 2.58), bluez (>= 5.42)")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "bluez (>= 5.77)")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "bluez-tools")

# Development package
set(CPACK_DEBIAN_PACKAGE_BUILDS_DEPENDS "build-essential, cmake (>= 3.16), pkg-config, libglib2.0-dev (>= 2.58), libbluetooth-dev, bluez")

# Package file naming
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

# Create separate packages for library and development files
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL libraries headers)

# Library package
set(CPACK_COMPONENT_LIBRARIES_DISPLAY_NAME "BzPeri Runtime Library")
set(CPACK_COMPONENT_LIBRARIES_DESCRIPTION "Runtime library for BzPeri Bluetooth LE GATT server")
set(CPACK_DEBIAN_LIBRARIES_PACKAGE_NAME "bzperi")
set(CPACK_DEBIAN_LIBRARIES_PACKAGE_SECTION "libs")

# Development package
set(CPACK_COMPONENT_HEADERS_DISPLAY_NAME "BzPeri Development Files")
set(CPACK_COMPONENT_HEADERS_DESCRIPTION "Development files for BzPeri Bluetooth LE GATT server library")
set(CPACK_DEBIAN_HEADERS_PACKAGE_NAME "bzperi-dev")
set(CPACK_DEBIAN_HEADERS_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_HEADERS_PACKAGE_DEPENDS "bzperi (= ${BZPERI_EFFECTIVE_VERSION})")

# Tools package
if(BUILD_STANDALONE)
    set(CPACK_COMPONENTS_ALL ${CPACK_COMPONENTS_ALL} tools)
    set(CPACK_COMPONENT_TOOLS_DISPLAY_NAME "BzPeri Tools")
    set(CPACK_COMPONENT_TOOLS_DESCRIPTION "Command-line tools for BzPeri")
    set(CPACK_DEBIAN_TOOLS_PACKAGE_NAME "bzperi-tools")
    set(CPACK_DEBIAN_TOOLS_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_TOOLS_PACKAGE_DEPENDS "bzperi (= ${BZPERI_EFFECTIVE_VERSION})")
endif()

# Print configuration summary
message(STATUS "")
message(STATUS "BzPeri Configuration Summary:")
message(STATUS "  Version (declared): ${PROJECT_VERSION}")
message(STATUS "  Version (effective): ${BZPERI_EFFECTIVE_VERSION}")
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
message(STATUS "  Package generator: ${CPACK_GENERATOR}")
message(STATUS "  Debian arch:       ${BZPERI_DEB_ARCH}")
message(STATUS "")

# Include CPack after all configuration is complete
include(CPack)
