# BzPeri - Modern Bluetooth LE GATT Server

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![BlueZ](https://img.shields.io/badge/BlueZ-5.77%2B-brightgreen.svg)](http://www.bluez.org/)

**BzPeri** (BlueZ Peripheral) is a modern C++20 Bluetooth LE GATT server library for Linux systems. It provides an elegant DSL-style interface for creating and managing Bluetooth LE services using BlueZ over D-Bus.

## 🚀 Quick Start

### Requirements
- **Linux** with BlueZ ≥ 5.77 (tested up to 5.79)
- **C++20** compatible compiler (GCC 10+ or Clang 12+)
- **GLib/GIO** ≥ 2.58, D-Bus system access
- **Root privileges** or proper D-Bus/BlueZ permissions

### Installation from APT Repository

**Recommended**: Install directly from our APT repository:

```bash
# Add BzPeri repository
curl -fsSL https://jy1655.github.io/BzPeri/repo/repo.key | sudo gpg --dearmor -o /usr/share/keyrings/bzperi-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/bzperi-archive-keyring.gpg] https://jy1655.github.io/BzPeri/repo stable main" | sudo tee /etc/apt/sources.list.d/bzperi.list

# Install BzPeri
sudo apt update
sudo apt install bzperi bzperi-dev bzperi-tools

# Auto-configure BlueZ (optional but recommended)
export BZPERI_AUTO_EXPERIMENTAL=1
sudo -E apt install --reinstall bzperi

# Run example server
sudo bzp-standalone -d
```

### Build from Source
```bash
# Clone and build
git clone https://github.com/jy1655/BzPeri.git
cd bzperi
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run example server
sudo ./bzp-standalone -d
```

## 📖 What is BzPeri?

BzPeri is a C++20 Bluetooth LE GATT server framework that makes creating BLE services **simple and intuitive**. It handles all the complex BlueZ D-Bus integration while providing a clean, DSL-style API for service definition.

### Key Features

✨ **Modern C++20** - Uses std::expected, concepts, and modern error handling
🔧 **DSL-Style API** - Intuitive service definition with method chaining
🚀 **BlueZ 5.77+ Ready** - Optimized for latest BlueZ with enhanced stability
⚡ **High Performance** - Asynchronous D-Bus operations with intelligent retry
🛡️ **Robust Error Handling** - Comprehensive error recovery and logging
🔒 **Secure by Default** - Proper bonding/pairing support out of the box

## 🏗️ Service Definition

Creating BLE services is **incredibly simple** with BzPeri's DSL:

```cpp
#include <BzPeri.h>

// Simple time service example
.gattServiceBegin("time_service", "1805")
    .gattCharacteristicBegin("current_time", "2A2B", {"read", "notify"})
        .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA {
            auto now = std::chrono::system_clock::now();
            auto time_string = formatTime(now);
            self.methodReturnValue(pInvocation, time_string, true);
        })
    .gattCharacteristicEnd()
.gattServiceEnd()
```

### Complete Application Example

```cpp
#include <BzPeri.h>

// Data access callbacks
const void* dataGetter(const char* name) {
    // Return your application data
    static int batteryLevel = 75;
    if (strcmp(name, "battery/level") == 0) {
        return &batteryLevel;
    }
    return nullptr;
}

int dataSetter(const char* name, const void* data) {
    // Store data from BLE clients
    return 1; // success
}

int main() {
    // Start BzPeri server with bonding enabled (recommended)
    if (!bzpStartWithBondable("my_device", "My BLE Device", "MyDev",
                  dataGetter, dataSetter, 30000, 1)) {
        return -1;
    }

    // Your application logic here
    while (bzpIsServerRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Notify clients of data changes
        bzpNofifyUpdatedCharacteristic("/com/my_device/battery/level");
    }

    bzpShutdownAndWait();
    return 0;
}
```

## 📚 Documentation

- **[API Reference](include/BzPeri.h)** - Complete API documentation

### Migration from Gobbledegook

If you're coming from the original Gobbledegook library, BzPeri maintains API compatibility while providing enhanced features:

```cpp
// Old API (still supported)
ggkStart("device", "name", "short", getter, setter, timeout);

// New API with bonding control
bzpStartWithBondable("device", "name", "short", getter, setter, timeout, true);
```

## ⚙️ Configuration

### D-Bus Permissions

**🎉 Automatic Setup**: When installed via package manager (`apt install bzperi`), D-Bus permissions are automatically configured and applied. No manual setup or restart required!

For manual builds, create `/etc/dbus-1/system.d/com.bzperi.conf`:
```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN">
<busconfig>
  <policy user="root">
    <allow own="com.bzperi"/>
    <allow send_destination="com.bzperi"/>
    <allow send_destination="org.bluez"/>
  </policy>
</busconfig>
```

D-Bus will automatically detect and apply the new policy. If you encounter permission issues, you can manually reload D-Bus configuration:
```bash
sudo systemctl reload dbus  # Only if needed for troubleshooting
```

### BlueZ Configuration

**🎉 Automatic Helper**: When installed via package manager, BzPeri includes a configuration helper script.

```bash
# Enable BlueZ experimental mode (recommended for optimal compatibility)
sudo /usr/share/bzperi/configure-bluez-experimental.sh enable

# Check current status
sudo /usr/share/bzperi/configure-bluez-experimental.sh status

# Disable if needed
sudo /usr/share/bzperi/configure-bluez-experimental.sh disable
```

**Manual Configuration**: For advanced users or custom setups:
```bash
# Create systemd override (safer than editing system files)
sudo systemctl edit bluetooth

# Add the following lines:
# [Service]
# ExecStart=
# ExecStart=/usr/lib/bluetooth/bluetoothd --experimental
# Note: Replace with your system's actual bluetoothd path

# Apply changes
sudo systemctl restart bluetooth
```

**Key Benefits of the Helper Script**:
- ✅ **Auto-detects** bluetoothd path across different Linux distributions
- ✅ **Preserves existing flags** and adds --experimental safely
- ✅ **Safe override** method - doesn't modify system files
- ✅ **Easy rollback** - can disable anytime

## 🏗️ Architecture Support

BzPeri packages are available for multiple architectures:
- **amd64** (x86_64) - Intel/AMD 64-bit systems ✅
- **arm64** (aarch64) - ARM 64-bit systems (Raspberry Pi 4+, Apple Silicon, etc.) ✅

Both architectures are fully supported and automatically built via GitHub Actions CI/CD.

### Native Compilation
```bash
# Build from source on ARM64 systems
git clone https://github.com/jy1655/BzPeri.git
cd BzPeri
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

## 🔧 Advanced Features

### Bonding/Pairing Control
```cpp
// Enable bonding (recommended for security)
bzpStartWithBondable("device", "name", "short", getter, setter, 30000, 1);

// Disable bonding (open access)
bzpStartWithBondable("device", "name", "short", getter, setter, 30000, 0);
```

### Error Handling
```cpp
auto result = someOperation();
if (!result) {
    Logger::error("Operation failed: {}", result.error().toString());
    return std::unexpected(result.error());
}
```

### Modern Adapter Management
```cpp
// List available adapters
sudo ./bzp-standalone --list-adapters

// Use specific adapter
sudo ./bzp-standalone --adapter=hci1
```

## 🙏 Acknowledgments

**BzPeri is based on the excellent [Gobbledegook](https://github.com/nettlep/gobbledegook) library by Paul Nettle.** We extend our sincere gratitude to Paul for creating the original foundation that made this modern Bluetooth LE library possible.

### Key Differences from Original Gobbledegook

**Enhanced for 2025:**
- ✅ **C++20 Modernization** - std::expected, concepts, modern error handling
- ✅ **BlueZ 5.77+ Compatibility** - Latest BlueZ API integration
- ✅ **Improved Stability** - Enhanced connection handling and retry mechanisms
- ✅ **Bondable Configuration** - Fixes common connection issues
- ✅ **Performance Optimizations** - Asynchronous operations and Linux-specific enhancements

**Preserved from Original:**
- 🔄 **Elegant DSL** - The beloved service definition syntax
- 🔄 **Thread-Safe Design** - Robust data callback architecture
- 🔄 **D-Bus Integration** - Seamless BlueZ communication
- 🔄 **API Compatibility** - Existing code continues to work

Both libraries share the same core philosophy: **making Bluetooth LE development accessible and powerful for Linux developers.**

## 📄 License

BzPeri is licensed under the [MIT License](LICENSE-MIT).

**Original Work Attribution:**
This software contains code derived from Gobbledegook (Copyright 2017-2019 Paul Nettle, BSD License). See [NOTICE](NOTICE) and [COPYRIGHT](COPYRIGHT) files for detailed attribution.

## 🤝 Contributing

We welcome contributions! Whether it's bug fixes, new features, or documentation improvements, please feel free to submit issues and pull requests.

## 🆘 Support

- **Issues**: [GitHub Issues](../../issues)
- **Examples**: Check out `src/bzp-standalone.cpp` for a complete example

---

**BzPeri** - Modern Bluetooth LE made simple. Built on the shoulders of giants. 🚀
