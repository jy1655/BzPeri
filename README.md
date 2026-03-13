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
        .onReadValue([](const GattCharacteristic& self, const std::string&, DBusMethodCallRef methodCall) {
            auto now = std::chrono::system_clock::now();
            auto time_string = formatTime(now);
            self.methodReturnValue(DBusReplyRef(methodCall), time_string, true);
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
                .onReadValue([](const GattCharacteristic& self, const std::string&, DBusMethodCallRef methodCall) {
                    std::string manufacturer = "My Company";
                    self.methodReturnValue(DBusReplyRef(methodCall), manufacturer, true);
                })
            .gattCharacteristicEnd()
        .gattServiceEnd()

        // Custom service example
        .gattServiceBegin("my_service", "12345678-1234-1234-1234-123456789ABC")
            .gattCharacteristicBegin("my_data", "87654321-4321-4321-4321-ABCDEF123456", {"read", "write", "notify"})
                .onReadValue([](const GattCharacteristic& self, const std::string&, DBusMethodCallRef methodCall) {
                    // Handle read requests
                    uint32_t value = self.getDataValue<uint32_t>("my_service/data", 0);
                    self.methodReturnValue(DBusReplyRef(methodCall), value, true);
                })
                .onWriteValue([](const GattCharacteristic& self, const std::string&, DBusMethodCallRef methodCall) {
                    // Extract value from D-Bus parameters
                    GVariant* pAyBuffer = g_variant_get_child_value(methodCall.parameters().get(), 0);
                    std::string incoming = Utils::stringFromGVariantByteArray(DBusVariantRef(pAyBuffer));
                    g_variant_unref(pAyBuffer);

                    // Trigger notification handler and respond to client
                    self.setDataPointer("my_service/data", incoming.c_str());
                    self.callOnUpdatedValue(DBusUpdateRef(methodCall.connection(), methodCall.userData()));
                    self.methodReturnVariant(DBusReplyRef(methodCall), DBusVariantRef());
                })
                .onUpdatedValue([](const GattCharacteristic& self, DBusUpdateRef update) {
                    // Handle notifications
                    uint32_t value = self.getDataValue<uint32_t>("my_service/data", 0);
                    self.sendChangeNotificationValue(update.connection(), value);
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

- `#include "Gobbledegook.h"` is still supported as a compatibility path, but new code should include `BzPeri.h` directly.
- Legacy `ggk*` wrappers are deprecated in favor of `bzp*` APIs.

```cpp
// Old API (still supported) - note service name requirements
ggkStart("bzperi.device", "name", "short", getter, setter, timeout);

// New API with bonding control
bzpStartWithBondable("bzperi.myapp", "name", "short", getter, setter, timeout, true);
```

### Runtime Integration Modes

BzPeri now supports two integration styles:

- Thread-owned mode: `bzpStart*()` / `bzpStart*NoWait()` keep using the internal server thread.
- Manual-iteration mode: `bzpStartManual()` / `bzpStartWithBondableManual()` create the dedicated GLib context without spawning the internal thread; the host then drives progress with `bzpRunLoopIteration()` or bounded waits via `bzpRunLoopIterationFor()`. The first iteration call becomes the owning thread for that manual run loop, or the host can claim/release ownership explicitly with `bzpRunLoopAttach()` / `bzpRunLoopDetach()`.

Manual mode is the better fit when the host already owns its lifecycle and wants explicit control over when BLE initialization, D-Bus callbacks, and shutdown cleanup are pumped.
For host-to-server scheduling without exposing GLib types directly, `bzpRunLoopInvoke()` can queue a callback onto the same dedicated run loop.
For host event loops that already use `poll(2)`/`select(2)`-style integration, the hidden poll API `bzpRunLoopPollPrepare()` / `bzpRunLoopPollQuery()` / `bzpRunLoopPollCheck()` / `bzpRunLoopPollDispatch()` / `bzpRunLoopPollCancel()` exposes the dedicated run loop as plain poll descriptors instead of `GMainContext *`.
For lifecycle control in manual mode, `bzpRunLoopDriveUntilState()` and `bzpRunLoopDriveUntilShutdown()` can pump the loop until a target state is reached.
If the host needs to reason about ownership explicitly, `bzpRunLoopIsManualMode()`, `bzpRunLoopHasOwner()`, and `bzpRunLoopIsCurrentThreadOwner()` expose the current manual run-loop state.
If the host needs failure-aware queries instead of legacy `0/1` predicates, `bzpRunLoopIsManualModeEx()`, `bzpRunLoopHasOwnerEx()`, `bzpRunLoopIsCurrentThreadOwnerEx()`, `bzpGetGLibLogCaptureEnabledEx()`, `bzpIsGLibLogCaptureInstalledEx()`, `bzpUpdateQueueIsEmptyEx()`, `bzpUpdateQueueSizeEx()`, and `bzpIsServerRunningEx()` return `BZPQueryResult` and write their answers through output pointers.
For embedded hosts that need tighter control over process-global GLib handlers, `bzpSetGLibLogCaptureMode()` can switch between automatic capture, fully disabled capture, `HOST_MANAGED` capture with explicit `bzpInstallGLibLogCapture()` / `bzpRestoreGLibLogCapture()`, and `STARTUP_AND_SHUTDOWN` capture that automatically releases the process-global override once the server reaches `ERunning`. The capture scope can also be narrowed with `bzpSetGLibLogCaptureTargets()` so hosts can intercept just `g_log` traffic or include `g_print` / `g_printerr` explicitly, and `bzpSetGLibLogCaptureDomains()` can further limit `g_log` interception to the default domain, `GLib`, `GIO`, BlueZ, or other application domains. If the host needs failure reasons instead of `0/1`, `bzpSetGLibLogCaptureModeEx()` distinguishes invalid modes, `bzpSetGLibLogCaptureTargetsEx()` distinguishes invalid target masks, `bzpSetGLibLogCaptureDomainsEx()` distinguishes invalid domain masks, and `bzpInstallGLibLogCaptureEx()` / `bzpRestoreGLibLogCaptureEx()` distinguish `WRONG_MODE` and `NOT_INSTALLED`.
The same detailed-result pattern is now available for wait helpers via `bzpWaitEx()` / `bzpWaitForStateEx()` / `bzpWaitForShutdownEx()` / `bzpShutdownAndWaitEx()`, which distinguish pre-start `NOT_RUNNING`, invalid state/timeout, timeouts, and join failures. Shutdown triggering now also has `bzpTriggerShutdownEx()`, which distinguishes `NOT_RUNNING` from repeated `ALREADY_STOPPING` requests.
Update queue maintenance also has a detailed helper now: `bzpUpdateQueueClearEx()` reports how many queued entries were cleared instead of silently dropping them through the legacy `void` wrapper.
At this point the remaining `0/1` and `void` forms are retained primarily as compatibility shims; new integration code should prefer the corresponding `Ex` and query-result APIs when failure reasons matter.
Manual run-loop helpers follow the same pattern: `bzpRunLoopIterationEx()`, `bzpRunLoopAttachEx()`, `bzpRunLoopPollPrepareEx()`, `bzpRunLoopDriveUntilStateEx()` and related APIs distinguish `NOT_MANUAL_MODE`, `WRONG_THREAD`, `NO_POLL_CYCLE`, `BUFFER_TOO_SMALL`, and timeout/idle outcomes without exposing raw GLib types.
The build-time defaults can be changed with `-DBZP_DEFAULT_GLIB_LOG_CAPTURE_MODE=AUTOMATIC|DISABLED|HOST_MANAGED|STARTUP_AND_SHUTDOWN` and `-DBZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS=ALL|LOG|LOG,PRINTERR|...`; `bzpGetConfiguredGLibLogCaptureMode()` and `bzpGetConfiguredGLibLogCaptureTargets()` expose those compiled-in defaults at runtime.
Builders that do not need deprecated singleton/global compatibility can configure CMake with `-DENABLE_LEGACY_SINGLETON_COMPAT=OFF`.
With `ENABLE_LEGACY_SINGLETON_COMPAT=OFF`, the deprecated singleton/global implementation units are omitted from the build entirely. New C++ code that wants to avoid compatibility mirrors entirely can consult `getRuntimeServer()` / `getRuntimeServerPtr()` and `getRuntimeBluezAdapterPtr()` to see only the runtime-owned instances created by BzPeri itself.
Builders that do not need deprecated raw GLib callback/method APIs can configure CMake with `-DENABLE_LEGACY_RAW_GLIB_COMPAT=OFF`.
The same `ENABLE_LEGACY_RAW_GLIB_COMPAT=OFF` setting now also removes deprecated raw property getter/setter registration helpers, raw signal/reply/notification helpers, raw `GVariant*` property convenience overloads, raw `Utils::gvariantFrom*()` helpers, and their legacy alias names, so wrapper request-object paths become the only public extension surface.
If the host needs detailed startup failure reasons instead of the legacy `0/1` return, use `bzpStartEx()` / `bzpStartWithBondableEx()` / `bzpStartManualEx()` and inspect `BZPStartResult`.
The bundled `bzp-standalone` sample can be launched in this mode with `--manual-loop`, and its GLib capture strategy can be exercised with `--glib-log-capture=auto|off|host|startup-shutdown`, `--glib-log-targets=all|log|log,printerr`, and `--glib-log-domains=all|bluez|glib,gio`.
When advertising starts, BzPeri now logs the selected payload mode (`legacy` vs `extended`), `MaxAdvLen`, and the UUIDs actually placed into the advertisement so Extended Advertising verification is visible at `INFO` level.
Extended Advertising support is implemented in the payload selection path, but validation on an actually extended-capable controller is still pending because suitable test hardware was not available during this cycle.

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
