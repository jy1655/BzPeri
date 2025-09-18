# BzPeri Debian Package Build and Deployment Guide

This guide explains how to build BzPeri as Debian packages (.deb) and make them installable from APT repositories.

## 📋 Table of Contents

1. [Package Structure](#package-structure)
2. [Build Methods](#build-methods)
3. [Local APT Repository Setup](#local-apt-repository-setup)
4. [Package Installation and Usage](#package-installation-and-usage)
5. [Official Repository Distribution](#official-repository-distribution)

## 📦 Package Structure

BzPeri is split into 3 Debian packages, currently supporting **amd64** architecture (**arm64** is in development):

### `bzperi` (Runtime Library)
- **Description**: BzPeri runtime library
- **Included Files**: `libbzp.so.*`
- **Dependencies**: `libglib2.0-0`, `libgio-2.0-0`, `libgobject-2.0-0`, `bluez`

### `bzperi-dev` (Development Files)
- **Description**: BzPeri development headers and static libraries
- **Included Files**: Header files, `libbzp.so`, `bzperi.pc`
- **Dependencies**: `bzperi`, development libraries

### `bzperi-tools` (Command-line Tools)
- **Description**: BzPeri testing and demo tools
- **Included Files**: `bzp-standalone`
- **Dependencies**: `bzperi`

## 🔨 Build Methods

### 1. System Requirements

```bash
# On Ubuntu/Debian systems
sudo apt update
sudo apt install build-essential cmake pkg-config debhelper \
    libglib2.0-dev libgio-2.0-dev libgobject-2.0-dev \
    libbluetooth-dev bluez bluez-tools
```

### 2. Automated Build (Recommended)

Use the convenient build script:

```bash
# Grant execution permission
chmod +x scripts/build-deb.sh

# Build using CPack (default - amd64)
./scripts/build-deb.sh

# Build for specific architecture
./scripts/build-deb.sh --arch amd64    # For x86_64 systems (supported)
./scripts/build-deb.sh --arch arm64    # ARM64 cross-compilation (experimental)

# Build using native Debian tools
./scripts/build-deb.sh --native

# Build and test installation (requires sudo)
./scripts/build-deb.sh --test-install
```

### 3. Manual Build

#### Method A: CMake + CPack

```bash
# Create build directory
mkdir build-deb && cd build-deb

# Configure
cmake .. \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_SHARED_LIBS=ON \
    -DBUILD_STANDALONE=ON \
    -DENABLE_BLUEZ_ADVANCED=ON \
    -DENABLE_PERFORMANCE_OPTIMIZATION=ON

# Build
make -j$(nproc)

# Create packages
cpack -G DEB
```

#### Method B: Debian Native Tools

```bash
# Grant execution permission to debian/rules
chmod +x debian/rules

# Build source package
dpkg-source -b .

# Build binary packages
dpkg-buildpackage -us -uc -b
```

### 4. Verify Build Results

After building, the following files will be created in the `packages/` directory:

```
packages/
├── bzperi_1.0.0-1_amd64.deb              # Runtime library
├── bzperi-dev_1.0.0-1_amd64.deb          # Development files
├── bzperi-tools_1.0.0-1_amd64.deb        # Command-line tools
├── bzperi_1.0.0-1_amd64.changes          # Changes (native build only)
└── bzperi_1.0.0-1_amd64.buildinfo        # Build info (native build only)
```

## 🏪 Local APT Repository Setup

Create a local APT repository for installation via `apt install`.

### 1. Automated Setup (Recommended)

```bash
# Grant execution permission
chmod +x scripts/setup-apt-repo.sh

# Complete APT repository setup (with GPG signing)
sudo ./scripts/setup-apt-repo.sh

# Setup without GPG signing (for development)
sudo ./scripts/setup-apt-repo.sh --skip-gpg

# Create repository only (no APT configuration)
sudo ./scripts/setup-apt-repo.sh --no-configure
```

### 2. Manual Setup

```bash
# Create repository directories
sudo mkdir -p /var/local/bzperi-repo/{pool/main,dists/stable/main/binary-amd64}

# Copy packages
sudo cp packages/*.deb /var/local/bzperi-repo/pool/main/

# Generate Packages files
cd /var/local/bzperi-repo
sudo dpkg-scanpackages pool/main /dev/null | gzip -9c > dists/stable/main/binary-amd64/Packages.gz
sudo dpkg-scanpackages pool/main /dev/null > dists/stable/main/binary-amd64/Packages

# Create Release file
cd dists/stable
sudo apt-ftparchive release . > Release

# Add APT source
echo "deb [trusted=yes] file:///var/local/bzperi-repo stable main" | sudo tee /etc/apt/sources.list.d/bzperi-local.list

# Update APT cache
sudo apt update
```

## 💾 Package Installation and Usage

### 1. Installation via APT

```bash
# Update APT cache
sudo apt update

# Install all packages
sudo apt install bzperi bzperi-dev bzperi-tools

# Or install individually
sudo apt install bzperi          # Runtime only
sudo apt install bzperi-dev      # Development files (includes runtime)
sudo apt install bzperi-tools    # Tools (includes runtime)
```

### 2. Direct Installation

```bash
# Install in dependency order
sudo dpkg -i packages/bzperi_*.deb
sudo dpkg -i packages/bzperi-dev_*.deb
sudo dpkg -i packages/bzperi-tools_*.deb

# Fix dependency issues if needed
sudo apt-get install -f
```

### 3. Installation Verification

```bash
# Check library
ldconfig -p | grep bzp

# Check header files
ls /usr/include/BzPeri.h

# Check tools
which bzp-standalone
bzp-standalone --help

# Check pkg-config
pkg-config --cflags --libs bzperi

# Check D-Bus policy file
ls /etc/dbus-1/system.d/com.bzperi.conf

# Check BlueZ configuration helper script
ls /usr/share/bzperi/configure-bluez-experimental.sh

# Configure BlueZ experimental mode (recommended)
sudo /usr/share/bzperi/configure-bluez-experimental.sh enable

# Verify D-Bus policy application (automatically applied after installation)
sudo systemctl status dbus
```

### 4. Development Usage

```bash
# Compile using pkg-config
gcc $(pkg-config --cflags bzperi) main.c $(pkg-config --libs bzperi) -o main

# Use in CMake projects
find_package(PkgConfig REQUIRED)
pkg_check_modules(BZPERI REQUIRED bzperi)

target_link_libraries(your_app ${BZPERI_LIBRARIES})
target_include_directories(your_app PRIVATE ${BZPERI_INCLUDE_DIRS})
```

### 5. Tools Usage

```bash
# Check available BlueZ adapters
sudo bzp-standalone --list-adapters

# Run demo server
sudo bzp-standalone -d

# Use specific adapter
sudo bzp-standalone --adapter=hci1 -d
```

## 🌐 Official Repository Distribution

### GitHub Pages as APT Repository

This repository includes a GitHub Actions workflow (`.github/workflows/apt-publish.yml`) that automatically creates and deploys an APT repository to GitHub Pages when tags/releases are created.

1) Enable GitHub Pages: Settings → Pages → Source set to "GitHub Actions"

2) Register GPG private key (optional but recommended)
- Settings → Secrets and variables → Actions → New repository secret
- `APT_GPG_PRIVATE_KEY`: ASCII-armored private key (e.g., `gpg --armor --export-secret-keys KEYID`)
- `APT_GPG_PASSPHRASE`: Key passphrase (leave empty if none)

3) Trigger release
- Create tag: `git tag -a v1.0.0 -m "v1.0.0" && git push origin v1.0.0`
- Or publish Release

4) User installation guide
```bash
# Register public key (GitHub Pages path)
curl -fsSL https://<USER>.github.io/<REPO>/repo/repo.key | sudo gpg --dearmor -o /usr/share/keyrings/bzperi-archive-keyring.gpg

# Add APT source
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bzperi-archive-keyring.gpg] https://<USER>.github.io/<REPO>/repo stable main" | \
  sudo tee /etc/apt/sources.list.d/bzperi.list

sudo apt update
sudo apt install bzperi bzperi-dev bzperi-tools
```

### 1. GitHub Releases

```bash
# Create release tag
git tag -a v1.0.0 -m "Release version 1.0.0"
git push origin v1.0.0

# Create release using GitHub CLI
gh release create v1.0.0 packages/*.deb \
    --title "BzPeri v1.0.0" \
    --notes "First stable release of BzPeri"
```

### 2. PPA (Personal Package Archive) Creation

For distribution via Ubuntu PPA:

```bash
# After creating PPA on Launchpad
dput ppa:your-username/bzperi ../libbzperi_1.0.0-1_source.changes
```

### 3. Official Debian/Ubuntu Repositories

Steps for official repository registration:

1. **Debian**: Upload package to [debian-mentors](https://mentors.debian.net/)
2. **Ubuntu**: Request review through REVU process
3. **ITP (Intent To Package)** bug report submission

## 🔧 Troubleshooting

### Build Errors

```bash
# Missing dependencies
sudo apt install build-essential cmake pkg-config debhelper

# Missing GLib development files
sudo apt install libglib2.0-dev libgio-2.0-dev libgobject-2.0-dev

# Missing BlueZ development files
sudo apt install libbluetooth-dev bluez
```

### Package Installation Errors

```bash
# Fix dependency issues
sudo apt-get install -f

# Force installation (not recommended)
sudo dpkg -i --force-depends package.deb
```

### D-Bus Permission Issues

Generally, D-Bus policies are automatically applied, but if there are issues:

```bash
# Check D-Bus policy file
ls -la /etc/dbus-1/system.d/com.bzperi.conf

# Manual D-Bus reload (for troubleshooting, generally unnecessary)
sudo systemctl reload dbus

# Or full restart (last resort)
sudo systemctl restart dbus

# Test permissions
sudo bzp-standalone --list-adapters
```

### Repository Issues

```bash
# Clean APT cache
sudo apt clean && sudo apt update

# Remove repository
sudo rm /etc/apt/sources.list.d/bzperi-local.list
sudo apt update
```

## 📝 Additional Information

- **License**: MIT License (original Gobbledegook is BSD-style)
- **Supported Platforms**: Linux (BlueZ 5.42+, recommended: 5.77+)
- **C++ Standard**: C++20
- **GitHub**: https://github.com/jy1655/BzPeri

For package-related inquiries, please file issues on GitHub.