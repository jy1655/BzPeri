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

# Configure D-Bus and BlueZ (required before first run)
# See "Configuration" section below for detailed instructions
sudo cp ../dbus/com.bzperi.conf /etc/dbus-1/system.d/
sudo ../scripts/configure-bluez-experimental.sh enable

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
        .onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
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
    // Service name must be 'bzperi' or start with 'bzperi.' for D-Bus compatibility
    if (!bzpStartWithBondable("bzperi.mydevice", "My BLE Device", "MyDev",
                  dataGetter, dataSetter, 30000, 1)) {
        return -1;
    }

    // Your application logic here
    while (bzpIsServerRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Notify clients of data changes (note the path structure)
        // Service name "bzperi.mydevice" becomes path "/com/bzperi/mydevice/..."
        bzpNofifyUpdatedCharacteristic("/com/bzperi/mydevice/samples/battery/level");
    }

    bzpShutdownAndWait();
    return 0;
}
```

## 📚 Documentation

### For Users
- **[API Reference](include/BzPeri.h)** - Complete API documentation
- **[Configurator API](include/bzp/ConfiguratorSupport.h)** - Modern service configuration API
- **[Standalone Usage](STANDALONE_USAGE.md)** - Command-line options and adapter selection guide

### For Developers & Contributors
- **[Build Guide](BUILD.md)** - Detailed build instructions, dependencies, and CMake options
- **[Repository Guidelines](AGENTS.md)** - Coding standards, project structure, and contribution workflow

### Technical Documentation
- **[Packaging Guide](README-PACKAGING.md)** - Debian package building and APT repository setup
- **[BlueZ Migration](BLUEZ_MIGRATION.md)** - HCI Management API → D-Bus migration details
- **[Modernization Guide](MODERNIZATION.md)** - 2019→2025 upgrade overview and architecture changes

## 📁 Header Layout

All development headers install into the `bzp/` include namespace. Downstream projects should reference the configurator surface with canonical includes, for example:

```cpp
#include <bzp/ConfiguratorSupport.h>
#include <bzp/Server.h>
#include <bzp/GattCharacteristic.h>
```

This guarantees that code built against the `bzperi-dev` package picks up the packaged headers and stays aligned with the shared library—no copying from `src/` is required.

### Service Configuration with Configurator API

BzPeri now supports a modern configurator API for defining GATT services. This approach provides better modularity and maintainability compared to the legacy C API.

#### Quick Start with Configurators

**1. Include the configurator support header:**
```cpp
#include <bzp/ConfiguratorSupport.h>
```

**2. Define your service configurator:**
```cpp
void configureMyServices(bzp::Server& server) {
    server.configure([](bzp::DBusObject& root) {
        // Device Information Service
        root.gattServiceBegin("device_info", "180A")
            .gattCharacteristicBegin("manufacturer", "2A29", {"read"})
                .onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
                    std::string manufacturer = "My Company";
                    self.methodReturnValue(pInvocation, manufacturer, true);
                })
            .gattCharacteristicEnd()
        .gattServiceEnd()

        // Custom service example
        .gattServiceBegin("my_service", "12345678-1234-1234-1234-123456789ABC")
            .gattCharacteristicBegin("my_data", "87654321-4321-4321-4321-ABCDEF123456", {"read", "write", "notify"})
                .onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
                    // Handle read requests
                    uint32_t value = self.getDataValue<uint32_t>("my_service/data", 0);
                    self.methodReturnValue(pInvocation, value, true);
                })
                .onWriteValue([](const GattCharacteristic& self, GDBusConnection* pConnection, const std::string&, GVariant* pParameters, GDBusMethodInvocation* pInvocation, void* pUserData) {
                    // Extract value from D-Bus parameters
                    GVariant* pAyBuffer = g_variant_get_child_value(pParameters, 0);
                    // Process the byte array as needed for your data type
                    g_variant_unref(pAyBuffer);

                    // Trigger notification handler and respond to client
                    self.callOnUpdatedValue(pConnection, pUserData);
                    self.methodReturnVariant(pInvocation, nullptr);
                })
                .onUpdatedValue([](const GattCharacteristic& self, GDBusConnection* pConnection, void*) {
                    // Handle notifications
                    uint32_t value = self.getDataValue<uint32_t>("my_service/data", 0);
                    self.sendChangeNotificationValue(pConnection, value);
                    return true;
                })
            .gattCharacteristicEnd()
        .gattServiceEnd();
    });
}
```

**3. Register your configurator:**
```cpp
int main() {
    // Register your service configurator
    bzp::registerServiceConfigurator(configureMyServices);

    // Start the server (configurators are applied automatically)
    if (!bzpStartWithBondable("bzperi.myapp", "My App", "App",
                             dataGetter, dataSetter, 30000, 1)) {
        return -1;
    }

    // Rest of your application...
}
```

#### Benefits of the Configurator API

- **Modular**: Each service can be defined in separate files/modules
- **Type Safe**: Fluent interface provides compile-time validation
- **IntelliSense**: Modern IDEs provide auto-completion support
- **Maintainable**: Clear separation between service definition and business logic
- **Testable**: Services can be unit tested independently

#### Available Headers

The configurator API is distributed as part of the `bzperi-dev` package:

- `bzp/ConfiguratorSupport.h` - All-in-one header for configurator development
- `bzp/Server.h` - Server configuration interface
- `bzp/DBusObject.h` - Root object for service tree
- `bzp/GattService.h` - Service definition interface
- `bzp/GattCharacteristic.h` - Characteristic definition interface
- `bzp/GattDescriptor.h` - Descriptor definition interface
- `bzp/GattUuid.h` - UUID handling utilities
- `bzp/GattCallbacks.h` - Callback macros for event handlers

### Migration from Gobbledegook

If you're coming from the original Gobbledegook library, BzPeri maintains API compatibility while providing enhanced features:

```cpp
// Old API (still supported) - note service name requirements
ggkStart("bzperi.device", "name", "short", getter, setter, timeout);

// New API with bonding control
bzpStartWithBondable("bzperi.myapp", "name", "short", getter, setter, timeout, true);
```

## ⚙️ Configuration

### D-Bus Permissions

#### Automatic Setup (Package Installation)

**🎉 Automatic**: When installed via package manager (`apt install bzperi`), D-Bus permissions are automatically configured and applied. No manual setup required!

#### Manual Installation (Build from Source)

**📋 For builds from source or troubleshooting**, manually install the D-Bus policy:

```bash
# Copy the policy file to system D-Bus directory
sudo cp dbus/com.bzperi.conf /etc/dbus-1/system.d/

# Verify file permissions
sudo chmod 644 /etc/dbus-1/system.d/com.bzperi.conf

# D-Bus auto-detects changes, but you can force reload if needed
sudo systemctl reload dbus
```

**Alternative method** - Install via CMake:
```bash
cd build
sudo cmake --install . --component dbus-policy
```

#### Service Name Requirements

**🔒 Important**: All service names must be `bzperi` or start with `bzperi.` (e.g., `bzperi.myapp`, `bzperi.company.device`) to ensure D-Bus policy compatibility and prevent conflicts.

**📍 Path Structure**: Service names with dots become D-Bus object paths with slashes:
- `bzperi` → `/com/bzperi/...`
- `bzperi.myapp` → `/com/bzperi/myapp/...`
- `bzperi.company.device` → `/com/bzperi/company/device/...`

#### D-Bus Policy File

The included policy file (`dbus/com.bzperi.conf`) provides comprehensive permissions:

```xml
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN">
<busconfig>
  <!-- Root user permissions -->
  <policy user="root">
    <allow own="com.bzperi"/>
    <allow send_destination="com.bzperi"/>
    <allow send_destination="org.bluez"/>
    <allow receive_sender="org.bluez"/>
  </policy>

  <!-- Bluetooth group permissions -->
  <policy group="bluetooth">
    <allow send_destination="com.bzperi"/>
    <allow receive_sender="com.bzperi"/>
  </policy>

  <!-- Introspection for debugging -->
  <policy context="default">
    <allow send_destination="com.bzperi"
           send_interface="org.freedesktop.DBus.Introspectable"/>
    <allow send_destination="com.bzperi"
           send_interface="org.freedesktop.DBus.Properties"/>
  </policy>
</busconfig>
```

#### Troubleshooting D-Bus Permissions

**Check if policy is installed:**
```bash
ls -la /etc/dbus-1/system.d/com.bzperi.conf
# Should show: -rw-r--r-- root root
```

**Verify D-Bus can see your service:**
```bash
# List all D-Bus services
dbus-send --system --print-reply --dest=org.freedesktop.DBus \
  /org/freedesktop/DBus org.freedesktop.DBus.ListNames
```

**Check D-Bus logs for permission errors:**
```bash
sudo journalctl -u dbus -f
# Then run your BzPeri application and watch for errors
```

**Common issues:**
- ❌ **"Connection refused"** → D-Bus policy not installed or incorrect permissions
- ❌ **"Access denied"** → Service name doesn't start with `bzperi.`
- ❌ **"Name already in use"** → Another instance is running

**Force D-Bus to reload policies:**
```bash
sudo systemctl reload dbus
# Or restart D-Bus (may affect other services):
sudo systemctl restart dbus
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
bzpStartWithBondable("bzperi.secure", "name", "short", getter, setter, 30000, 1);

// Disable bonding (open access)
bzpStartWithBondable("bzperi.open", "name", "short", getter, setter, 30000, 0);
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
