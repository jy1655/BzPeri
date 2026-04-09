# BzPeri - Modern Bluetooth LE GATT Server

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![BlueZ](https://img.shields.io/badge/BlueZ-5.77%2B-brightgreen.svg)](http://www.bluez.org/)

**BzPeri** (BlueZ Peripheral) is a modern C++20 Bluetooth LE GATT server library for Linux systems. It provides an elegant DSL-style interface for creating and managing Bluetooth LE services using BlueZ over D-Bus.

## Quick Start

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

# Check the host, start the managed demo, then inspect it from another terminal
sudo bzp-standalone doctor
sudo bzp-standalone demo -d
sudo bzp-standalone inspect --live
```

### Build from Source
```bash
# Clone and build
git clone https://github.com/jy1655/BzPeri.git
cd BzPeri
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Configure D-Bus and BlueZ (required before first run)
# See "Configuration" section below for detailed instructions
sudo cp ../dbus/com.bzperi.conf /etc/dbus-1/system.d/
sudo ../scripts/configure-bluez-experimental.sh enable

# Check the host, start the managed demo, then inspect it from another terminal
sudo ./bzp-standalone doctor
sudo ./bzp-standalone demo -d
sudo ./bzp-standalone inspect --live
```

For a full first-run workflow, including Raspberry Pi / Debian-over-SSH notes and a verification-client path, see [STANDALONE_USAGE.md](STANDALONE_USAGE.md).

## What is BzPeri?

BzPeri is a C++20 Bluetooth LE GATT server framework that makes creating BLE services **simple and intuitive**. It handles all the complex BlueZ D-Bus integration while providing a clean, DSL-style API for service definition.

### Key Features

- **Modern C++20** - Uses std::expected, concepts, and modern error handling
- **DSL-style API** - Intuitive service definition with method chaining
- **BlueZ 5.77+ ready** - Optimized for current BlueZ with improved stability
- **Asynchronous runtime** - D-Bus operations with retry/recovery paths
- **Robust error handling** - Detailed result codes and defensive runtime checks
- **Secure by default** - Proper bonding/pairing support out of the box

## New in v0.2.1

Compared to `v0.2.0`, `v0.2.1` is about making first-run validation feel like a complete product path instead of a pile of flags.

- **Terminal-first standalone workflow**: `bzp-standalone doctor`, `demo`, and `inspect --live` now form the primary validation path.
- **Live inspect session reports**: the managed demo writes session state that `inspect --live` can read back as object metadata, recent events, and next-step guidance.
- **Packaged-install verification**: CI now checks both the build-tree binary and the staged installed binary, so release packaging regressions get caught before tagging.
- **Workflow-oriented docs**: README, build docs, packaging docs, and standalone usage docs now all walk through the same `doctor -> demo -> inspect` flow.
- **Linux host hardening**: the BlueZ experimental helper and D-Bus policy now better match real packaged installs and multi-name service usage.

## Upgrading from v0.1.x

If you are coming from `v0.1.9` or an earlier `0.1.x` release, the important practical changes are:

| Area | v0.1.x | v0.2.0 |
|------|--------|--------|
| Event-loop model | Internal server thread only | Internal thread or manual run-loop / hidden-poll integration |
| Error reporting | Mostly `0/1` or `void` | `Ex` result-code APIs across the main control paths |
| Public extension surface | Raw GLib callbacks still prominent | Wrapper request objects are the canonical public path |
| Compatibility cleanup | Deprecated layers always built | Legacy singleton/raw GLib layers can be compiled out |
| Runtime controls | Limited GLib/power knobs | GLib capture modes, targets, domains, pause/resume, sleep integration, optional inhibitor |
| Validation | Limited automated coverage | `ctest` regression target plus expanded runtime verification |

Recommended upgrade order:

1. Move to the corrected `bzpNotify*` names if you still use the old misspelled aliases.
2. Prefer `Ex` APIs where you need failure reasons.
3. Move custom raw GLib callback code to wrapper request-object APIs.
4. Test with `-DENABLE_LEGACY_SINGLETON_COMPAT=OFF -DENABLE_LEGACY_RAW_GLIB_COMPAT=OFF`.
5. If embedding into a larger process, review GLib capture defaults and consider `HOST_MANAGED`.

## Service Definition

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
        bzpNotifyUpdatedCharacteristic("/com/bzperi/mydevice/samples/battery/level");
    }

    bzpShutdownAndWait();
    return 0;
}
```

## Documentation

### For Users
- **[API Reference](include/BzPeri.h)** - Complete API documentation
- **[Changelog](CHANGELOG.md)** - Release-by-release summary for the current `v0.2.1` line and earlier milestones
- **[Configurator API](include/bzp/ConfiguratorSupport.h)** - Modern service configuration API
- **[Compatibility Migration](COMPATIBILITY_MIGRATION.md)** - Deprecated API migration from Gobbledegook and legacy BzPeri shims
- **[Standalone Usage](STANDALONE_USAGE.md)** - Command-line options and adapter selection guide

### For Developers & Contributors
- **[Build Guide](BUILD.md)** - Detailed build instructions, dependencies, and CMake options
- **[Contributing Guide](CONTRIBUTING.md)** - Contribution workflow and repository expectations
- **[Code of Conduct](CODE_OF_CONDUCT.md)** - Community expectations for contributors
- **[Security Policy](SECURITY.md)** - Responsible disclosure and supported security reporting path

### Technical Documentation
- **[Packaging Guide](README-PACKAGING.md)** - Debian package building and APT repository setup
- **[BlueZ Migration](BLUEZ_MIGRATION.md)** - HCI Management API → D-Bus migration details
- **[Modernization Guide](MODERNIZATION.md)** - 2019→2025 upgrade overview and architecture changes
- **[Project TODOs](TODOS.md)** - Deferred follow-up work beyond the current release slice

## Header Layout

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

### Migration from Gobbledegook

If you're coming from the original Gobbledegook library, BzPeri maintains API compatibility while providing enhanced features:

- `#include "Gobbledegook.h"` is still supported as a compatibility path, but new code should include `BzPeri.h` directly.
- Legacy `ggk*` wrappers are deprecated in favor of `bzp*` APIs.
- The full compatibility/deprecation transition plan is documented in [COMPATIBILITY_MIGRATION.md](COMPATIBILITY_MIGRATION.md).

```cpp
// Old API (still supported) - note service name requirements
ggkStart("bzperi.device", "name", "short", getter, setter, timeout);

// New API with bonding control
bzpStartWithBondable("bzperi.myapp", "name", "short", getter, setter, timeout, true);
```

### Runtime Integration Modes

BzPeri supports two integration styles. Pick the one that matches who owns the process lifecycle.

| Mode | Main APIs | Best fit |
|------|-----------|----------|
| Thread-owned | `bzpStart*()` / `bzpStart*NoWait()` | Normal applications that are happy to let BzPeri run its own server thread |
| Manual iteration | `bzpStartManual()` / `bzpStartWithBondableManual()` + `bzpRunLoopIteration*()` | Embedded hosts, existing event loops, or processes that want explicit control over startup, callbacks, and shutdown |

#### Manual Mode Tools

Use manual mode when the host wants to decide exactly when BLE initialization, D-Bus callbacks, and cleanup are pumped.

- `bzpRunLoopIteration()` / `bzpRunLoopIterationFor()`: drive progress directly
- `bzpRunLoopAttach()` / `bzpRunLoopDetach()`: explicitly claim or release run-loop ownership
- `bzpRunLoopInvoke()`: queue host work onto the same dedicated run loop
- `bzpRunLoopDriveUntilState()` / `bzpRunLoopDriveUntilShutdown()`: pump until a lifecycle milestone is reached
- `bzpRunLoopPollPrepare()` / `bzpRunLoopPollQuery()` / `bzpRunLoopPollCheck()` / `bzpRunLoopPollDispatch()` / `bzpRunLoopPollCancel()`: expose the run loop as plain poll descriptors for hosts that already use `poll(2)` or `select(2)`
- `bzpRunLoopIsManualMode()`, `bzpRunLoopHasOwner()`, and `bzpRunLoopIsCurrentThreadOwner()`: inspect current ownership state

If you want failure-aware results instead of legacy `0/1` behavior, use the `Ex` variants such as `bzpRunLoopIterationEx()`, `bzpRunLoopAttachEx()`, `bzpRunLoopPollPrepareEx()`, and `bzpRunLoopDriveUntilStateEx()`. These distinguish cases like `NOT_MANUAL_MODE`, `WRONG_THREAD`, `NO_POLL_CYCLE`, `BUFFER_TOO_SMALL`, and timeout or idle outcomes.

#### GLib Log Capture

For hosts that need tighter control over process-global GLib handlers, BzPeri exposes four capture modes through `bzpSetGLibLogCaptureMode()`:

- `AUTOMATIC`: install capture while the runtime is active
- `DISABLED`: never install capture automatically
- `HOST_MANAGED`: the host explicitly calls `bzpInstallGLibLogCapture()` / `bzpRestoreGLibLogCapture()`
- `STARTUP_AND_SHUTDOWN`: capture during lifecycle transitions, then release once the server reaches `ERunning`

You can narrow the capture scope with:

- `bzpSetGLibLogCaptureTargets()`: choose `g_log` only, or include `g_print` / `g_printerr`
- `bzpSetGLibLogCaptureDomains()`: limit capture to default, `GLib`, `GIO`, BlueZ, or other application domains
- `bzpPauseGLibLogCapture()` / `bzpResumeGLibLogCapture()`: temporarily yield capture without changing the configured mode

Mode changes apply immediately against the current runtime state. If another component replaces one of the process-global handlers while capture is active, BzPeri preserves that external handler during restore instead of blindly overwriting it. Hosts can inspect and clear that contention state with `bzpGetGLibLogCaptureContentionTargetsEx()` / `bzpClearGLibLogCaptureContention()`.

If you need structured failure reasons, prefer `bzpSetGLibLogCaptureModeEx()`, `bzpSetGLibLogCaptureTargetsEx()`, `bzpSetGLibLogCaptureDomainsEx()`, `bzpPauseGLibLogCaptureEx()`, `bzpResumeGLibLogCaptureEx()`, `bzpInstallGLibLogCaptureEx()`, and `bzpRestoreGLibLogCaptureEx()`.

#### Power Management

On systemd-based systems, BzPeri can subscribe to `org.freedesktop.login1.Manager.PrepareForSleep` so advertising pauses before suspend and resumes afterward.

- `bzpSetPrepareForSleepIntegrationEnabled()` / `bzpGetPrepareForSleepIntegrationEnabledEx()`: toggle suspend/resume advertising integration
- `bzpSetSleepInhibitorEnabled()` / `bzpGetSleepInhibitorEnabledEx()`: hold a login1 delay inhibitor fd while the server is active

When the sleep inhibitor is enabled, BzPeri keeps the `PrepareForSleep` subscription active even if advertising pause/resume integration itself is disabled, because the inhibitor lifecycle depends on the same signal.

#### Failure-Aware Control APIs

The same detailed-result pattern now exists across the runtime control surface:

- `bzpStartEx()` / `bzpStartWithBondableEx()` / `bzpStartManualEx()`: startup failure reasons via `BZPStartResult`
- `bzpWaitEx()` / `bzpWaitForStateEx()` / `bzpWaitForShutdownEx()` / `bzpShutdownAndWaitEx()`: distinguish `NOT_RUNNING`, invalid state, timeout, and join failures
- `bzpTriggerShutdownEx()`: distinguishes `NOT_RUNNING` from repeated `ALREADY_STOPPING`
- `bzpUpdateQueueClearEx()`: reports how many queued entries were removed
- `bzpRunLoopIsManualModeEx()`, `bzpRunLoopHasOwnerEx()`, `bzpRunLoopIsCurrentThreadOwnerEx()`, `bzpGetGLibLogCaptureEnabledEx()`, `bzpIsGLibLogCaptureInstalledEx()`, `bzpUpdateQueueIsEmptyEx()`, `bzpUpdateQueueSizeEx()`, and `bzpIsServerRunningEx()`: query APIs that return `BZPQueryResult` instead of plain `0/1`

The older `0/1` and `void` forms still exist as compatibility shims, but new integration code should prefer the `Ex` and query-result variants when failure reasons matter.

#### Build-Time Defaults

You can change the compiled defaults with:

- `-DBZP_DEFAULT_GLIB_LOG_CAPTURE_MODE=AUTOMATIC|DISABLED|HOST_MANAGED|STARTUP_AND_SHUTDOWN`
- `-DBZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS=ALL|LOG|LOG,PRINTERR|...`
- `-DBZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS=ALL|BLUEZ|GLIB,GIO|...`
- `-DBZP_DEFAULT_PREPARE_FOR_SLEEP_INTEGRATION=ON|OFF`
- `-DBZP_DEFAULT_SLEEP_INHIBITOR=ON|OFF`
- `-DBZP_COMPILED_LOG_LEVEL=TRACE|DEBUG|INFO|STATUS|WARN|ERROR|FATAL|ALWAYS`

At runtime, the compiled defaults are visible through `bzpGetConfiguredGLibLogCaptureMode()`, `bzpGetConfiguredGLibLogCaptureTargets()`, `bzpGetConfiguredGLibLogCaptureDomains()`, `bzpGetConfiguredPrepareForSleepIntegrationEnabled()`, `bzpGetConfiguredSleepInhibitorEnabled()`, and `bzpGetConfiguredCompiledLogLevel()`.

#### Compatibility Toggles

If you do not need legacy compatibility layers, you can compile them out:

- `-DENABLE_LEGACY_SINGLETON_COMPAT=OFF`: removes deprecated singleton/global implementation units, and new C++ code can rely on `getRuntimeServer()` / `getRuntimeServerPtr()` / `getRuntimeBluezAdapterPtr()`
- `-DENABLE_LEGACY_RAW_GLIB_COMPAT=OFF`: removes deprecated raw property getter/setter registration helpers, raw signal/reply/notification helpers, raw `GVariant*` convenience overloads, raw `Utils::gvariantFrom*()` helpers, and their legacy alias names

If you want to exercise these runtime options quickly, the bundled `bzp-standalone` sample supports `--manual-loop`, `--glib-log-capture=auto|off|host|startup-shutdown`, `--glib-log-targets=all|log|log,printerr`, and `--glib-log-domains=all|bluez|glib,gio`.

When advertising starts, BzPeri now logs the selected payload mode (`legacy` vs `extended`), `MaxAdvLen`, and the UUIDs actually placed into the advertisement so extended-advertising verification is visible at `INFO` level. The payload path is implemented, but validation on actually extended-capable hardware is still pending.

## Configuration

### D-Bus Permissions

#### Automatic Setup (Package Installation)

Installed packages configure the D-Bus policy automatically. No manual setup is required in the normal APT-based path.

#### Manual Installation (Build from Source)

For builds from source or troubleshooting, install the D-Bus policy manually:

```bash
# Copy the policy file to system D-Bus directory
sudo cp dbus/com.bzperi.conf /etc/dbus-1/system.d/

# Verify file permissions
sudo chmod 644 /etc/dbus-1/system.d/com.bzperi.conf

# D-Bus auto-detects changes, but you can force reload if needed
sudo systemctl reload dbus
```

**Alternative method** - Install the runtime assets via CMake:
```bash
cd build
sudo cmake --install . --component libraries
```

This installs the runtime library, `bzp-standalone`, the D-Bus policy file, and the BlueZ helper script.

#### Service Name Requirements

Important: all service names must be `bzperi` or start with `bzperi.` (for example `bzperi.myapp`, `bzperi.company.device`) to ensure D-Bus policy compatibility and prevent conflicts.

Path structure: service names with dots become D-Bus object paths with slashes:
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
    <allow own_prefix="com.bzperi"/>
    <allow send_destination_prefix="com.bzperi"/>
    <allow send_destination="org.bluez"/>
    <allow receive_sender="org.bluez"/>
  </policy>

  <!-- Bluetooth group permissions -->
  <policy group="bluetooth">
    <allow send_destination_prefix="com.bzperi"/>
  </policy>

  <!-- Introspection for debugging -->
  <policy context="default">
    <allow send_destination_prefix="com.bzperi"
           send_interface="org.freedesktop.DBus.Introspectable"/>
  </policy>
</busconfig>
```

#### Troubleshooting D-Bus Permissions

**Check if policy is installed:**
```bash
ls -la /usr/share/dbus-1/system.d/com.bzperi.conf
# Or, for source-build/manual installs:
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

Common issues:
- **"Connection refused"**: D-Bus policy not installed or incorrect permissions
- **"Access denied"**: Service name is not `bzperi` and does not start with `bzperi.`
- **"Name already in use"**: Another instance is running

**Force D-Bus to reload policies:**
```bash
sudo systemctl reload dbus
# Or restart D-Bus (may affect other services):
sudo systemctl restart dbus
```

### BlueZ Configuration

BzPeri includes a configuration helper script for package installs:

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

Key benefits of the helper script:
- **Auto-detects** the `bluetoothd` path across different Linux distributions
- **Preserves existing flags** and adds `--experimental` safely
- **Uses a safe override** method instead of editing system files directly
- **Supports rollback** by disabling the override later

## Architecture Support

BzPeri packages are available for multiple architectures:
- **amd64** (x86_64) - Intel/AMD 64-bit systems
- **arm64** (aarch64) - ARM 64-bit systems (Raspberry Pi 4+, Apple Silicon, etc.)

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

## Advanced Features

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
    // Inspect and report the error in your preferred logging style.
    return std::unexpected(result.error());
}
```

### Modern Adapter Management
```bash
# List available adapters
sudo ./bzp-standalone doctor --list-adapters

# Use a specific adapter for the managed demo
sudo ./bzp-standalone demo --adapter=hci1
```

## Acknowledgments

**BzPeri is based on the excellent [Gobbledegook](https://github.com/nettlep/gobbledegook) library by Paul Nettle.** We extend our sincere gratitude to Paul for creating the original foundation that made this modern Bluetooth LE library possible.

### Key Differences from Original Gobbledegook

**Enhanced for 2025+:**
- **C++20 modernization** - std::expected, concepts, modern error handling
- **BlueZ 5.77+ compatibility** - Current BlueZ API integration
- **Improved stability** - Enhanced connection handling and retry mechanisms
- **Bondable configuration** - Fixes common connection issues
- **Performance optimizations** - Asynchronous operations and Linux-specific enhancements

**Preserved from Original:**
- **Elegant DSL** - The service definition syntax remains familiar
- **Thread-safe design** - Robust data callback architecture
- **D-Bus integration** - Seamless BlueZ communication
- **API compatibility** - Existing code continues to work

Both libraries share the same core philosophy: **making Bluetooth LE development accessible and powerful for Linux developers.**

## License

BzPeri is licensed under the [MIT License](LICENSE).

**Original Work Attribution:**
This software contains code derived from Gobbledegook (Copyright 2017-2019 Paul Nettle, BSD License). See [NOTICE](NOTICE) and [COPYRIGHT](COPYRIGHT) files for detailed attribution.

## Contributing

We welcome contributions! Whether it's bug fixes, new features, or documentation improvements, please feel free to submit issues and pull requests.

## Support

- **Issues**: [GitHub Issues](https://github.com/jy1655/BzPeri/issues)
- **Examples**: See [`samples/standalone.cpp`](samples/standalone.cpp) for the main sample application

---

**BzPeri** - Modern Bluetooth LE made simple. Built on the shoulders of giants.
