# Gobbledegook Build Guide (2025)

This guide covers building Gobbledegook for modern systems with support for the latest BlueZ versions (5.77+).

## System Requirements

### Supported Platforms
- **Linux**: Full BLE GATT server functionality (primary target)
- **macOS**: Development and testing support (compatibility layer)
- **Other platforms**: Compile-time compatibility, runtime support varies

### Dependencies

#### Linux (Full BLE Support)
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential cmake pkg-config \
    libglib2.0-dev libgio-2.0-dev libgobject-2.0-dev \
    libbluetooth-dev bluez bluez-tools

# Fedora/CentOS/RHEL
sudo dnf install gcc-c++ cmake pkgconfig \
    glib2-devel gio-2.0-devel gobject-2.0-devel \
    bluez-libs-devel bluez bluez-tools

# Arch Linux
sudo pacman -S base-devel cmake pkgconfig \
    glib2 bluez bluez-utils
```

#### macOS (Development/Testing)
```bash
# Install Xcode Command Line Tools
xcode-select --install

# Install Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake pkg-config glib
```

### BlueZ Version Support
- **Minimum**: BlueZ 5.42+ (original compatibility)
- **Recommended**: BlueZ 5.77+ (latest stable with bug fixes)
- **Tested**: BlueZ 5.79 (current latest as of 2025)

## Build Methods

### Option 1: CMake (Recommended)

Modern CMake-based build system with cross-platform support:

```bash
# Clone and setup
git clone <repository-url>
cd gobbledegook

# Create build directory
mkdir build && cd build

# Configure (Debug)
cmake .. -DCMAKE_BUILD_TYPE=Debug

# Configure (Release)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)

# Install (optional)
sudo make install
```

#### CMake Options
```bash
# Shared libraries (default: ON)
cmake .. -DBUILD_SHARED_LIBS=ON

# Static libraries
cmake .. -DBUILD_SHARED_LIBS=OFF

# Build standalone example (default: ON on Linux)
cmake .. -DBUILD_STANDALONE=ON

# Enable testing
cmake .. -DBUILD_TESTING=ON

# Custom install prefix
cmake .. -DCMAKE_INSTALL_PREFIX=/usr/local
```

### Option 2: Autotools (Legacy)

Traditional autotools build (Linux only):

```bash
# Generate configure script (if building from git)
autoreconf -fiv

# Configure
./configure

# Build
make -j$(nproc)

# Install
sudo make install
```

## Build Verification

### Check Build Success
```bash
# Verify library
ls -la build/libggk.*

# Verify standalone (Linux only)
ls -la build/ggk-standalone

# Check symbol exports
nm -D build/libggk.so | grep ggk
```

### Runtime Verification (Linux)
```bash
# Check BlueZ version
bluetoothctl version

# Test D-Bus connectivity (requires root)
sudo dbus-send --system --print-reply \
    --dest=org.bluez /org/bluez \
    org.freedesktop.DBus.Introspectable.Introspect

# Run standalone example (requires sudo)
sudo ./build/ggk-standalone -d
```

## Development Setup

### IDE Configuration

#### VS Code
```json
// .vscode/settings.json
{
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "cmake.buildDirectory": "${workspaceFolder}/build"
}
```

#### CLion
1. Open project directory
2. CLion will auto-detect CMakeLists.txt
3. Configure toolchain for your platform

### Debug Build
```bash
mkdir debug && cd debug
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
make -j$(nproc)

# Run with debugging
sudo gdb ./ggk-standalone
```

## Platform-Specific Notes

### Linux
- Requires root privileges for BlueZ D-Bus access
- D-Bus policy configuration may be needed:
  ```bash
  sudo cp docs/gobbledegook.conf /etc/dbus-1/system.d/
  sudo systemctl reload dbus
  ```

### macOS
- Bluetooth LE operations not supported (no BlueZ)
- Useful for code development and testing compatibility layer
- Build succeeds but runtime BLE functionality is disabled

### Cross-Compilation
```bash
# ARM64 example
cmake .. -DCMAKE_TOOLCHAIN_FILE=toolchain-arm64.cmake

# Custom toolchain
cmake .. -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++
```

## Troubleshooting

### Common Issues

#### "glib-2.0 not found"
```bash
# Install development packages
sudo apt install libglib2.0-dev pkg-config

# Verify pkg-config
pkg-config --cflags --libs glib-2.0
```

#### "BlueZ not available"
```bash
# Check BlueZ installation
systemctl status bluetooth
bluetoothctl version

# Install BlueZ
sudo apt install bluez bluez-tools
```

#### "D-Bus permission denied"
```bash
# Add D-Bus policy
sudo cp docs/gobbledegook.conf /etc/dbus-1/system.d/
sudo systemctl reload dbus

# Or run with sudo
sudo ./ggk-standalone
```

#### "std::format not available"
The build system automatically detects C++20 `std::format` support:
- **Available**: Uses standard library implementation
- **Not available**: Falls back to `snprintf`-based implementation
- No action required - compatibility layer handles this automatically

### Build Performance
```bash
# Parallel build
make -j$(nproc)

# Verbose build for debugging
make VERBOSE=1

# Clean build
make clean && make -j$(nproc)
```

## Integration Guide

### Using as Library

#### CMake Integration
```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(GGK REQUIRED gobbledegook)

target_link_libraries(your_target ${GGK_LIBRARIES})
target_include_directories(your_target PRIVATE ${GGK_INCLUDE_DIRS})
```

#### pkg-config Integration
```bash
# Compiler flags
gcc $(pkg-config --cflags gobbledegook) main.c

# Linker flags
gcc main.o $(pkg-config --libs gobbledegook) -o main
```

### Example Usage
```cpp
#include <Gobbledegook.h>

// Your application data getters/setters
int dataGetter(const char* name) { /* ... */ }
int dataSetter(const char* name, const void* data) { /* ... */ }

int main() {
    if (!ggkStart("My Device", "My Service", dataGetter, dataSetter)) {
        return 1;
    }

    // Your application logic

    ggkShutdown();
    return 0;
}
```

For more detailed examples, see the `src/standalone.cpp` file and the original README.md.