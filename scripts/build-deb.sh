#!/bin/bash

# BzPeri Debian Package Build Script
# This script builds .deb packages for BzPeri library

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-deb"
PACKAGE_DIR="$PROJECT_ROOT/packages"
VERSION_OVERRIDE="${BZPERI_VERSION:-}"
ARCH_OVERRIDE="${BZPERI_ARCH:-}"

# Function to print colored output
print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Function to check if running on Linux
check_platform() {
    if [[ "$OSTYPE" != "linux-gnu"* ]]; then
        print_error "This script must be run on Linux for Debian packaging"
        print_info "Current platform: $OSTYPE"
        exit 1
    fi
}

# Function to check dependencies
check_dependencies() {
    print_info "Checking build dependencies..."

    local missing_deps=()

    # Check for required packages
    for dep in build-essential cmake pkg-config debhelper; do
        if ! command -v "$dep" >/dev/null 2>&1 && ! dpkg -l | grep -q "^ii  $dep "; then
            missing_deps+=("$dep")
        fi
    done

    # Check for library dependencies
    for lib in libglib2.0-dev libbluetooth-dev; do
        if ! dpkg -l | grep -q "^ii  $lib"; then
            missing_deps+=("$lib")
        fi
    done

    if [ ${#missing_deps[@]} -gt 0 ]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        print_info "Please install them with:"
        print_info "sudo apt update && sudo apt install ${missing_deps[*]}"
        exit 1
    fi

    print_success "All dependencies are available"
}

# Function to clean previous builds
clean_build() {
    print_info "Cleaning previous builds..."
    rm -rf "$BUILD_DIR"
    rm -rf "$PACKAGE_DIR"
    mkdir -p "$BUILD_DIR"
    mkdir -p "$PACKAGE_DIR"
}

# Function to build using CMake + CPack
build_cpack() {
    print_info "Building Debian packages using CMake + CPack..."

    cd "$BUILD_DIR"

    # Configure
    print_info "Configuring build..."

    # Set cross-compilation options if needed
    CMAKE_OPTS="-DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_SHARED_LIBS=ON -DBUILD_STANDALONE=ON -DENABLE_BLUEZ_ADVANCED=ON"

    # Use toolchain file if provided
    if [ -n "$CMAKE_TOOLCHAIN_FILE" ] && [ -f "$CMAKE_TOOLCHAIN_FILE" ]; then
        print_info "Using CMake toolchain file: $CMAKE_TOOLCHAIN_FILE"
        CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE"
    elif [ "$ARCH_OVERRIDE" = "arm64" ] && [ -n "$CC" ] && [ -n "$CXX" ]; then
        print_info "Configuring for cross-compilation to arm64..."
        CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX"
        CMAKE_OPTS="$CMAKE_OPTS -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64"
        if [ -n "$PKG_CONFIG_PATH" ]; then
            CMAKE_OPTS="$CMAKE_OPTS -DPKG_CONFIG_EXECUTABLE=/usr/bin/pkg-config"
            export PKG_CONFIG_LIBDIR="$PKG_CONFIG_PATH"
        fi
    fi

    cmake "$PROJECT_ROOT" $CMAKE_OPTS \
        -DENABLE_PERFORMANCE_OPTIMIZATION=ON \
        ${VERSION_OVERRIDE:+-DBZPERI_VERSION_OVERRIDE=${VERSION_OVERRIDE}} \
        ${ARCH_OVERRIDE:+-DBZPERI_DEB_ARCH=${ARCH_OVERRIDE}}

    # Build
    print_info "Building project..."
    make -j$(nproc)

    # Create packages
    print_info "Creating Debian packages..."
    cpack -G DEB

    # Move packages to package directory
    mv *.deb "$PACKAGE_DIR/"

    print_success "CPack build completed"
}

# Function to build using traditional debian tools
build_debian_native() {
    print_info "Building Debian packages using debian native tools..."

    cd "$PROJECT_ROOT"

    # Check if debian/rules is executable
    if [ ! -x debian/rules ]; then
        print_info "Making debian/rules executable..."
        chmod +x debian/rules
    fi

    # Build source package
    print_info "Building source package..."
    dpkg-source -b .

    # Build binary packages
    print_info "Building binary packages..."
    dpkg-buildpackage -us -uc -b

    # Move packages to package directory
    mv ../*.deb "$PACKAGE_DIR/" 2>/dev/null || true
    mv ../*.changes "$PACKAGE_DIR/" 2>/dev/null || true
    mv ../*.buildinfo "$PACKAGE_DIR/" 2>/dev/null || true

    print_success "Native Debian build completed"
}

# Function to validate packages
validate_packages() {
    print_info "Validating generated packages..."

    local packages_found=false

    for deb in "$PACKAGE_DIR"/*.deb; do
        if [ -f "$deb" ]; then
            packages_found=true
            print_info "Package: $(basename "$deb")"

            # Check package contents
            print_info "  Contents:"
            dpkg-deb -c "$deb" | head -10
            if [ $(dpkg-deb -c "$deb" | wc -l) -gt 10 ]; then
                print_info "  ... and $(( $(dpkg-deb -c "$deb" | wc -l) - 10 )) more files"
            fi

            # Check package info
            print_info "  Info:"
            dpkg-deb -I "$deb" | grep -E "Package|Version|Architecture|Description"

            # Check dependencies
            print_info "  Dependencies:"
            dpkg-deb -f "$deb" Depends | tr ',' '\n' | sed 's/^ */    /'

            echo
        fi
    done

    if [ "$packages_found" = false ]; then
        print_error "No .deb packages found in $PACKAGE_DIR"
        exit 1
    fi

    print_success "Package validation completed"
}

# Function to test installation (requires sudo)
test_install() {
    if [ "$EUID" -eq 0 ]; then
        print_warning "Running as root - testing package installation..."

        # Install the main library package
        local lib_package=$(find "$PACKAGE_DIR" -name "bzperi_*.deb" | head -1)
        if [ -f "$lib_package" ]; then
            print_info "Testing installation of $(basename "$lib_package")..."

            # Set auto-experimental mode for testing
            export BZPERI_AUTO_EXPERIMENTAL=1
            dpkg -i "$lib_package" || apt-get install -f -y

            # Test if library can be found
            if ldconfig -p | grep -q bzp; then
                print_success "Library installation test passed"
            else
                print_warning "Library may not be properly installed"
            fi

            # Test if experimental mode was configured
            if /usr/share/bzperi/configure-bluez-experimental.sh check >/dev/null 2>&1; then
                print_success "BlueZ experimental mode auto-configuration test passed"
            else
                print_info "BlueZ experimental mode not configured (expected for some environments)"
            fi

            # Remove for clean state
            print_info "Removing test installation..."
            dpkg -r bzperi || true
        fi
    else
        print_info "Skipping installation test (requires sudo)"
        print_info "To test installation manually:"
        print_info "  sudo dpkg -i $PACKAGE_DIR/bzperi_*.deb"
        print_info "  sudo dpkg -i $PACKAGE_DIR/bzperi-dev_*.deb"
        print_info "  sudo dpkg -i $PACKAGE_DIR/bzperi-tools_*.deb"
        print_info ""
        print_info "To test auto-experimental mode:"
        print_info "  export BZPERI_AUTO_EXPERIMENTAL=1"
        print_info "  sudo -E dpkg -i $PACKAGE_DIR/bzperi_*.deb"
    fi
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -c, --cpack         Use CMake + CPack (default)"
    echo "  -n, --native        Use native Debian tools"
    echo "  -t, --test-install  Test package installation (requires sudo)"
    echo "  -v, --version VER   Override package version (e.g., 1.2.3)"
    echo "  -a, --arch ARCH     Override Debian arch (amd64, arm64, ...)"
    echo "  -h, --help          Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                  # Build using CPack"
    echo "  $0 --native         # Build using native Debian tools"
    echo "  $0 --test-install   # Build and test installation"
}

# Main function
main() {
    local build_method="cpack"
    local test_install_flag=false

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -c|--cpack)
                build_method="cpack"
                shift
                ;;
            -n|--native)
                build_method="native"
                shift
                ;;
            -t|--test-install)
                test_install_flag=true
                shift
                ;;
            -v|--version)
                VERSION_OVERRIDE="$2"; shift 2 ;;
            -a|--arch)
                ARCH_OVERRIDE="$2"; shift 2 ;;
            -h|--help)
                show_usage
                exit 0
                ;;
            *)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done

    print_info "Starting BzPeri Debian package build..."
    print_info "Build method: $build_method"

    # Check platform and dependencies
    check_platform
    check_dependencies

    # Clean and prepare
    clean_build

    # Build packages
    case $build_method in
        "cpack")
            build_cpack
            ;;
        "native")
            build_debian_native
            ;;
    esac

    # Validate
    validate_packages

    # Test installation if requested
    if [ "$test_install_flag" = true ]; then
        test_install
    fi

    print_success "Debian package build completed!"
    print_info "Packages are available in: $PACKAGE_DIR"

    # Show final package list
    print_info "Generated packages:"
    ls -la "$PACKAGE_DIR"/*.deb 2>/dev/null || print_warning "No .deb files found"
}

# Run main function with all arguments
main "$@"
