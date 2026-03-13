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
option(ENABLE_LEGACY_SINGLETON_COMPAT "Build deprecated TheServer and BluezAdapter::getInstance compatibility APIs" ON)
option(ENABLE_LEGACY_RAW_GLIB_COMPAT "Build deprecated raw GLib callback/method compatibility APIs" ON)
set(BZP_COMPILED_LOG_LEVEL "TRACE" CACHE STRING "Minimum compiled log level: TRACE, DEBUG, INFO, STATUS, WARN, ERROR, FATAL, ALWAYS")
set_property(CACHE BZP_COMPILED_LOG_LEVEL PROPERTY STRINGS TRACE DEBUG INFO STATUS WARN ERROR FATAL ALWAYS)
set(BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE "AUTOMATIC" CACHE STRING "Default GLib log capture mode: AUTOMATIC, DISABLED, HOST_MANAGED, or STARTUP_AND_SHUTDOWN")
set_property(CACHE BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE PROPERTY STRINGS AUTOMATIC DISABLED HOST_MANAGED STARTUP_AND_SHUTDOWN)
set(BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS "ALL" CACHE STRING "Default GLib log capture targets: ALL or a comma-separated subset of PRINT, PRINTERR, LOG")
set(BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS "ALL" CACHE STRING "Default GLib log capture domains: ALL or a comma-separated subset of DEFAULT, GLIB, GIO, BLUEZ, OTHER")
set(BZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION "ON" CACHE STRING "Default systemd PrepareForSleep integration: ON or OFF")
set_property(CACHE BZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION PROPERTY STRINGS ON OFF)

string(TOUPPER "${BZP_COMPILED_LOG_LEVEL}" BZP_COMPILED_LOG_LEVEL_NORMALIZED)
if(BZP_COMPILED_LOG_LEVEL_NORMALIZED STREQUAL "TRACE")
    set(BZP_COMPILED_LOG_LEVEL_VALUE 0)
elseif(BZP_COMPILED_LOG_LEVEL_NORMALIZED STREQUAL "DEBUG")
    set(BZP_COMPILED_LOG_LEVEL_VALUE 1)
elseif(BZP_COMPILED_LOG_LEVEL_NORMALIZED STREQUAL "INFO")
    set(BZP_COMPILED_LOG_LEVEL_VALUE 2)
elseif(BZP_COMPILED_LOG_LEVEL_NORMALIZED STREQUAL "STATUS")
    set(BZP_COMPILED_LOG_LEVEL_VALUE 3)
elseif(BZP_COMPILED_LOG_LEVEL_NORMALIZED STREQUAL "WARN")
    set(BZP_COMPILED_LOG_LEVEL_VALUE 4)
elseif(BZP_COMPILED_LOG_LEVEL_NORMALIZED STREQUAL "ERROR")
    set(BZP_COMPILED_LOG_LEVEL_VALUE 5)
elseif(BZP_COMPILED_LOG_LEVEL_NORMALIZED STREQUAL "FATAL")
    set(BZP_COMPILED_LOG_LEVEL_VALUE 6)
elseif(BZP_COMPILED_LOG_LEVEL_NORMALIZED STREQUAL "ALWAYS")
    set(BZP_COMPILED_LOG_LEVEL_VALUE 7)
else()
    message(FATAL_ERROR "BZP_COMPILED_LOG_LEVEL must be TRACE, DEBUG, INFO, STATUS, WARN, ERROR, FATAL, or ALWAYS")
endif()

if(BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE STREQUAL "AUTOMATIC")
    set(BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE_VALUE 0)
elseif(BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE STREQUAL "DISABLED")
    set(BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE_VALUE 1)
elseif(BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE STREQUAL "HOST_MANAGED")
    set(BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE_VALUE 2)
elseif(BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE STREQUAL "STARTUP_AND_SHUTDOWN")
    set(BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE_VALUE 3)
else()
    message(FATAL_ERROR "BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE must be AUTOMATIC, DISABLED, HOST_MANAGED, or STARTUP_AND_SHUTDOWN")
endif()

string(TOUPPER "${BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS}" BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_NORMALIZED)
if(BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_NORMALIZED STREQUAL "ALL")
    set(BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_VALUE 7)
else()
    set(BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_VALUE 0)
    string(REPLACE "," ";" BZP_GLIB_LOG_CAPTURE_TARGET_LIST "${BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_NORMALIZED}")
    foreach(BZP_GLIB_LOG_CAPTURE_TARGET IN LISTS BZP_GLIB_LOG_CAPTURE_TARGET_LIST)
        string(STRIP "${BZP_GLIB_LOG_CAPTURE_TARGET}" BZP_GLIB_LOG_CAPTURE_TARGET)
        if(BZP_GLIB_LOG_CAPTURE_TARGET STREQUAL "PRINT")
            math(EXPR BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_VALUE "${BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_VALUE} | 1")
        elseif(BZP_GLIB_LOG_CAPTURE_TARGET STREQUAL "PRINTERR")
            math(EXPR BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_VALUE "${BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_VALUE} | 2")
        elseif(BZP_GLIB_LOG_CAPTURE_TARGET STREQUAL "LOG")
            math(EXPR BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_VALUE "${BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_VALUE} | 4")
        elseif(BZP_GLIB_LOG_CAPTURE_TARGET STREQUAL "")
        else()
            message(FATAL_ERROR "BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS must be ALL or a comma-separated subset of PRINT, PRINTERR, LOG")
        endif()
    endforeach()
    if(BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_VALUE EQUAL 0)
        message(FATAL_ERROR "BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS must not resolve to an empty capture mask")
    endif()
endif()

string(TOUPPER "${BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS}" BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_NORMALIZED)
if(BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_NORMALIZED STREQUAL "ALL")
    set(BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE 31)
else()
    set(BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE 0)
    string(REPLACE "," ";" BZP_GLIB_LOG_CAPTURE_DOMAIN_LIST "${BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_NORMALIZED}")
    foreach(BZP_GLIB_LOG_CAPTURE_DOMAIN IN LISTS BZP_GLIB_LOG_CAPTURE_DOMAIN_LIST)
        string(STRIP "${BZP_GLIB_LOG_CAPTURE_DOMAIN}" BZP_GLIB_LOG_CAPTURE_DOMAIN)
        if(BZP_GLIB_LOG_CAPTURE_DOMAIN STREQUAL "DEFAULT")
            math(EXPR BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE "${BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE} | 1")
        elseif(BZP_GLIB_LOG_CAPTURE_DOMAIN STREQUAL "GLIB")
            math(EXPR BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE "${BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE} | 2")
        elseif(BZP_GLIB_LOG_CAPTURE_DOMAIN STREQUAL "GIO")
            math(EXPR BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE "${BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE} | 4")
        elseif(BZP_GLIB_LOG_CAPTURE_DOMAIN STREQUAL "BLUEZ")
            math(EXPR BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE "${BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE} | 8")
        elseif(BZP_GLIB_LOG_CAPTURE_DOMAIN STREQUAL "OTHER")
            math(EXPR BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE "${BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE} | 16")
        elseif(BZP_GLIB_LOG_CAPTURE_DOMAIN STREQUAL "")
        else()
            message(FATAL_ERROR "BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS must be ALL or a comma-separated subset of DEFAULT, GLIB, GIO, BLUEZ, OTHER")
        endif()
    endforeach()
    if(BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE EQUAL 0)
        message(FATAL_ERROR "BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS must not resolve to an empty domain mask")
    endif()
endif()

string(TOUPPER "${BZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION}" BZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION_NORMALIZED)
if(BZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION_NORMALIZED STREQUAL "ON")
    set(BZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION_VALUE 1)
elseif(BZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION_NORMALIZED STREQUAL "OFF")
    set(BZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION_VALUE 0)
else()
    message(FATAL_ERROR "BZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION must be ON or OFF")
endif()

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

# Force disable std::format for maximum compatibility with Debian 12 (GCC 12)
# std::format requires GLIBCXX_3.4.31+ (GCC 13+) which is not available in Debian 12
# Using snprintf fallback for broader compatibility across Debian/Ubuntu LTS versions
add_compile_definitions(HAS_STD_FORMAT=0)
message(STATUS "std::format disabled for Debian 12 compatibility (using snprintf fallback)")

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
    src/ServerRuntime.cpp
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
        src/BluezAdapterRuntime.cpp
        src/BluezAdvertisingSupport.cpp
        src/BluezTypes.cpp
        src/BluezAdvertisement.cpp
    )
endif()

if(ENABLE_LEGACY_SINGLETON_COMPAT)
    list(APPEND BZPERI_SOURCES
        src/ServerCompat.cpp
    )
    if(LINUX)
        list(APPEND BZPERI_SOURCES
            src/BluezAdapterCompat.cpp
        )
    endif()
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

target_compile_definitions(bzperi PUBLIC
    BZP_COMPILED_LOG_LEVEL_VALUE=${BZP_COMPILED_LOG_LEVEL_VALUE}
    BZP_ENABLE_LEGACY_SINGLETON_COMPAT=$<IF:$<BOOL:${ENABLE_LEGACY_SINGLETON_COMPAT}>,1,0>
    BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT=$<IF:$<BOOL:${ENABLE_LEGACY_RAW_GLIB_COMPAT}>,1,0>
)

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

# Tests
if(BUILD_TESTING)
    enable_testing()

    if(LINUX)
        add_executable(bzperi-tests
            tests/bzperi_tests.cpp
        )
        target_link_libraries(bzperi-tests PRIVATE
            bzperi
            ${GLIB_LIBRARIES}
            ${GIO_LIBRARIES}
            ${GOBJECT_LIBRARIES}
        )
        target_include_directories(bzperi-tests PRIVATE
            ${GLIB_INCLUDE_DIRS}
            ${GIO_INCLUDE_DIRS}
            ${GOBJECT_INCLUDE_DIRS}
        )
        target_compile_options(bzperi-tests PRIVATE
            ${GLIB_CFLAGS_OTHER}
            ${GIO_CFLAGS_OTHER}
            ${GOBJECT_CFLAGS_OTHER}
        )
        add_test(NAME bzperi-unit COMMAND bzperi-tests)
    else()
        message(WARNING "BUILD_TESTING is enabled, but tests are only available on Linux")
    endif()
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS bzperi
    EXPORT bzperi-targets
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

# Generate CMake package config files for find_package() support
include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/bzperi-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/bzperi-config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bzperi"
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/bzperi-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/bzperi-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/bzperi-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bzperi"
    COMPONENT headers
)

install(EXPORT bzperi-targets
    FILE bzperi-targets.cmake
    NAMESPACE bzp::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/bzperi"
    COMPONENT headers
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

# Dependencies - Compatible with Debian 12 bookworm (GCC 12, GLIBCXX_3.4.30)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.34), libgcc-s1 (>= 3.0), libstdc++6 (>= 12), libglib2.0-0 (>= 2.58), bluez (>= 5.42)")
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
message(STATUS "  Legacy singleton compat: ${ENABLE_LEGACY_SINGLETON_COMPAT}")
message(STATUS "  Legacy raw GLib compat: ${ENABLE_LEGACY_RAW_GLIB_COMPAT}")
message(STATUS "  Compiled log level: ${BZP_COMPILED_LOG_LEVEL}")
message(STATUS "  Default GLib capture: ${BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE}")
message(STATUS "  Default GLib capture targets: ${BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS}")
message(STATUS "  Default GLib capture domains: ${BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS}")
message(STATUS "  Default sleep integration: ${BZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION}")
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
