#!/bin/bash

# BzPeri APT Repository Setup Script
# This script helps set up a local APT repository for BzPeri packages

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
REPO_DIR="/var/local/bzperi-repo"
PACKAGES_DIR="$PROJECT_ROOT/packages"

# Advanced options (overridable via CLI)
OUTPUT_DIR="$REPO_DIR"
GENERATE_INRELEASE=true
ARCH="amd64"
CODENAME="stable"
COMPONENT="main"

# Function to print colored output
print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Function to check if running as root
check_root() {
    if [ "$EUID" -ne 0 ]; then
        print_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

# Function to check dependencies
check_dependencies() {
    print_info "Checking repository dependencies..."

    local missing_deps=()

    # Check for required packages
    for dep in dpkg-dev apt-utils gnupg apt-utils apt-transport-https; do
        if ! command -v "$dep" >/dev/null 2>&1 && ! dpkg -l | grep -q "^ii  $dep "; then
            missing_deps+=("$dep")
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

# Function to create repository structure
create_repo_structure() {
    print_info "Creating repository structure at $OUTPUT_DIR..."

    # Create directories
    mkdir -p "$OUTPUT_DIR"
    mkdir -p "$OUTPUT_DIR/pool/$COMPONENT"
    mkdir -p "$OUTPUT_DIR/dists/$CODENAME/$COMPONENT/binary-$ARCH"

    print_success "Repository structure created"
}

# Function to copy packages to repository
copy_packages() {
    print_info "Copying packages to repository..."

    if [ ! -d "$PACKAGES_DIR" ]; then
        print_error "Packages directory not found: $PACKAGES_DIR"
        print_info "Please run build-deb.sh first to generate packages"
        exit 1
    fi

    local package_count=0
    for deb in "$PACKAGES_DIR"/*.deb; do
        if [ -f "$deb" ]; then
            cp "$deb" "$OUTPUT_DIR/pool/$COMPONENT/"
            print_info "Copied: $(basename "$deb")"
            ((package_count++))
        fi
    done

    if [ "$package_count" -eq 0 ]; then
        print_error "No .deb packages found in $PACKAGES_DIR"
        print_info "Please run build-deb.sh first to generate packages"
        exit 1
    fi

    print_success "Copied $package_count packages"
}

# Function to generate Packages file
generate_packages_file() {
    print_info "Generating Packages file..."

    cd "$OUTPUT_DIR"
    dpkg-scanpackages "pool/$COMPONENT" /dev/null | gzip -9c > "dists/$CODENAME/$COMPONENT/binary-$ARCH/Packages.gz"
    dpkg-scanpackages "pool/$COMPONENT" /dev/null > "dists/$CODENAME/$COMPONENT/binary-$ARCH/Packages"

    print_success "Packages file generated"
}

# Function to generate Release file
generate_release_file() {
    print_info "Generating Release file..."

    cd "$OUTPUT_DIR/dists/$CODENAME"

    cat > Release << EOF
Origin: BzPeri Local Repository
Label: BzPeri
Suite: $CODENAME
Codename: $CODENAME
Version: 1.0
Architectures: $ARCH
Components: $COMPONENT
Description: Local APT repository for BzPeri packages
Date: $(date -Ru)
EOF

    # Add file hashes
    apt-ftparchive release . >> Release

    print_success "Release file generated"
}

# Function to set up GPG key (optional)
setup_gpg_key() {
    print_info "Setting up GPG key for repository signing..."

    local key_name="bzperi-repo-key"
    local key_email="bzperi-repo@local"

    # Check if key already exists
    if gpg --list-keys "$key_email" >/dev/null 2>&1; then
        print_info "GPG key already exists for $key_email"
        return
    fi

    # Generate key
    print_info "Generating GPG key..."
    cat > /tmp/gpg-batch << EOF
%echo Generating BzPeri repository key
Key-Type: RSA
Key-Length: 4096
Subkey-Type: RSA
Subkey-Length: 4096
Name-Real: BzPeri Repository
Name-Email: $key_email
Expire-Date: 1y
%no-protection
%commit
%echo done
EOF

    gpg --batch --generate-key /tmp/gpg-batch
    rm /tmp/gpg-batch

    # Export public key
    gpg --armor --export "$key_email" > "$OUTPUT_DIR/repository.key"

    print_success "GPG key generated and exported to $REPO_DIR/repository.key"
}

# Function to sign repository
sign_repository() {
    if [ "$1" = "--skip-gpg" ]; then
        print_info "Skipping GPG signing as requested"
        return
    fi

    print_info "Signing repository..."

    cd "$OUTPUT_DIR/dists/$CODENAME"

    # Sign Release file
    gpg --default-key bzperi-repo@local --detach-sign --armor -o Release.gpg Release
    if [ "$GENERATE_INRELEASE" = true ]; then
        gpg --default-key bzperi-repo@local --clearsign -o InRelease Release
    fi

    print_success "Repository signed"
}

# Function to configure local APT
configure_apt() {
    print_info "Configuring local APT to use repository..."

    # Create sources.list entry
    cat > /etc/apt/sources.list.d/bzperi-local.list << EOF
# BzPeri Local Repository
deb [trusted=yes] file://$OUTPUT_DIR $CODENAME $COMPONENT
EOF

    # Update APT cache
    apt update

    print_success "APT configured for local BzPeri repository"
    print_info "Repository added to: /etc/apt/sources.list.d/bzperi-local.list"
}

# Function to test repository
test_repository() {
    print_info "Testing repository..."

    # Check if packages are available
    if apt-cache search bzperi | grep -q bzperi; then
        print_success "Repository test passed - packages are available"
        print_info "Available packages:"
        apt-cache search bzperi | sed 's/^/  /'
    else
        print_warning "Repository test failed - packages not found in APT cache"
    fi
}

# Function to show installation instructions
show_install_instructions() {
    print_info "Repository setup completed!"
    print_info ""
    print_info "🚀 Installation Options:"
    print_info ""
    print_info "1. Standard installation:"
    print_info "   sudo apt update"
    print_info "   sudo apt install bzperi bzperi-dev bzperi-tools"
    print_info ""
    print_info "2. Auto-configure BlueZ experimental mode:"
    print_info "   export BZPERI_AUTO_EXPERIMENTAL=1"
    print_info "   sudo -E apt install bzperi"
    print_info ""
    print_info "3. Install individual packages:"
    print_info "   sudo apt install bzperi          # Runtime library"
    print_info "   sudo apt install bzperi-dev      # Development files"
    print_info "   sudo apt install bzperi-tools    # Command-line tools"
    print_info ""
    print_info "🔧 Post-installation (if not auto-configured):"
    print_info "   sudo /usr/share/bzperi/configure-bluez-experimental.sh enable"
    print_info ""
    print_info "🗑️ To remove the repository:"
    print_info "   sudo rm /etc/apt/sources.list.d/bzperi-local.list"
    print_info "   sudo apt update"
}

# Function to show usage
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --skip-gpg          Skip GPG key generation and signing"
    echo "  --no-configure      Don't configure APT (only create repository)"
    echo "  --output-dir PATH   Output directory for repo (default: $OUTPUT_DIR)"
    echo "  --no-inrelease      Do not generate InRelease (clearsigned) file"
    echo "  --arch ARCH         Architecture (default: $ARCH)"
    echo "  --codename NAME     Codename/Suite (default: $CODENAME)"
    echo "  --component NAME    Component (default: $COMPONENT)"
    echo "  -h, --help          Show this help message"
    echo ""
    echo "This script must be run as root (use sudo)"
    echo ""
    echo "Examples:"
    echo "  sudo $0                    # Full setup with GPG signing"
    echo "  sudo $0 --skip-gpg         # Setup without GPG signing"
    echo "  sudo $0 --no-configure     # Create repository only"
}

# Main function
main() {
    local skip_gpg=false
    local configure_apt_flag=true

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --skip-gpg)
                skip_gpg=true
                shift
                ;;
            --no-configure)
                configure_apt_flag=false
                shift
                ;;
            --output-dir)
                OUTPUT_DIR="$2"; shift 2 ;;
            --no-inrelease)
                GENERATE_INRELEASE=false; shift ;;
            --arch)
                ARCH="$2"; shift 2 ;;
            --codename)
                CODENAME="$2"; shift 2 ;;
            --component)
                COMPONENT="$2"; shift 2 ;;
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

    print_info "Starting BzPeri APT repository setup..."

    # Check requirements
    check_root
    check_dependencies

    # Create repository
    create_repo_structure
    copy_packages
    generate_packages_file
    generate_release_file

    # Handle GPG
    if [ "$skip_gpg" = false ]; then
        setup_gpg_key
        sign_repository
    else
        sign_repository --skip-gpg
    fi

    # Configure APT
    if [ "$configure_apt_flag" = true ]; then
        configure_apt
        test_repository
    fi

    # Show instructions
    show_install_instructions
}

# Run main function with all arguments
main "$@"
