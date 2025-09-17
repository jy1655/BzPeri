# BlueZ D-Bus Migration Guide

## Overview

This project has been migrated from the legacy HCI Management API to modern BlueZ D-Bus interfaces for improved stability, compatibility, and performance.

## Key Changes

### Removed Components
- `HciAdapter.cpp/h` - Legacy HCI Management API implementation
- `HciSocket.cpp/h` - Low-level HCI socket communication
- Synchronous command-response patterns with 1000ms timeouts

### Added Components
- `BluezAdapter.cpp/h` - Modern D-Bus interface wrapper
- `BluezTypes.cpp/h` - Error handling and type definitions
- Asynchronous D-Bus property operations
- Comprehensive adapter discovery and selection
- Robust error handling with retry mechanisms

## Migration Benefits

### 1. Eliminated Timeout Issues
**Before**: HCI Management API with 1000ms blocking calls
```cpp
// Old: Synchronous HCI with timeouts
if (!mgmt.setLE(true)) {
    // Timeout after 1000ms - common failure
    setRetry();
    return;
}
```

**After**: Asynchronous D-Bus operations
```cpp
// New: Non-blocking D-Bus with proper error handling
auto result = adapter.setPowered(true);
if (result.hasError()) {
    Logger::error(result.errorMessage());
    // Intelligent retry with backoff
}
```

### 2. Improved Compatibility
- **BlueZ 5.77+**: Uses recommended D-Bus interfaces
- **Permission Model**: Works with polkit authorization
- **Feature Detection**: Graceful fallback for unsupported features

### 3. Enhanced Reliability
- **Adapter Discovery**: Automatic detection of available adapters
- **Connection Tracking**: D-Bus signal-based device monitoring
- **Error Recovery**: Retry mechanisms with exponential backoff
- **Service Monitoring**: Automatic BlueZ service restart detection

## D-Bus Permissions

### Root Access
The application works with `sudo` out of the box:
```bash
sudo ./build/ggk-standalone -d
```

### Polkit Configuration
For non-root execution, configure polkit rules:

**File**: `/etc/polkit-1/rules.d/50-gobbledegook-bluez.rules`
```javascript
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.bluez.") == 0) {
        if (subject.user == "your-username") {
            return polkit.Result.YES;
        }
    }
});
```

### D-Bus Policy
Ensure your existing `/etc/dbus-1/system.d/com.gobbledegook.conf` remains for service name ownership. BlueZ access is handled by polkit.

## Feature Detection and Fallbacks

The new implementation includes comprehensive feature detection:

```cpp
// Check BlueZ capabilities
auto caps = adapter.detectCapabilities();
if (caps.isSuccess()) {
    if (caps.value().hasLEAdvertisingManager) {
        // Use modern advertising
    } else {
        Logger::warn("LE Advertising not supported - advertising disabled");
    }
}
```

### Supported Features
- **org.bluez.Adapter1**: Basic adapter control (always available)
- **org.bluez.LEAdvertisingManager1**: LE advertising (BlueZ 5.35+)
- **org.bluez.GattManager1**: GATT service registration (BlueZ 5.35+)
- **AcquireWrite/Notify**: Advanced GATT operations (BlueZ 5.77+)

## Adapter Selection

### Automatic Discovery
```cpp
// Discovers all available adapters
auto adapters = BluezAdapter::getInstance().discoverAdapters();
for (const auto& adapter : adapters.value()) {
    Logger::info(SSTR << "Found: " << adapter.path << " (" << adapter.address << ")");
}
```

### Manual Selection
```bash
# Environment variable
export BLUEZ_ADAPTER=hci1
./build/ggk-standalone

# Or programmatically
adapter.initialize("hci1");  // or "/org/bluez/hci1"
```

## Error Handling

### Standardized Result Types
```cpp
BluezResult<void> result = adapter.setPowered(true);
if (result.hasError()) {
    switch (result.error()) {
        case BluezError::PermissionDenied:
            Logger::error("Run with sudo or configure polkit");
            break;
        case BluezError::NotReady:
            Logger::error("BlueZ service not available");
            break;
        case BluezError::NotSupported:
            Logger::warn("Feature not supported - using fallback");
            break;
    }
}
```

### Retry Mechanisms
```cpp
// Automatic retry with exponential backoff
RetryPolicy policy{
    .maxAttempts = 3,
    .baseDelayMs = 100,
    .maxDelayMs = 5000,
    .backoffMultiplier = 2.0
};

auto result = adapter.retryOperation([&]() {
    return adapter.setPowered(true);
}, policy);
```

## Validation Checklist

### Startup Sequence
1. ✅ Acquires `com.gobbledegook` D-Bus name
2. ✅ Discovers available adapters via ObjectManager
3. ✅ Selects powered adapter (or first available)
4. ✅ Sets Powered/Discoverable/Connectable without blocking
5. ✅ Registers GATT services with GattManager1
6. ✅ Enables advertising (if LEAdvertisingManager1 available)

### Runtime Operations
1. ✅ Device connections trigger Device1.Connected signals
2. ✅ Connection count accurately tracked via D-Bus events
3. ✅ Notifications work correctly for connected devices
4. ✅ Device disconnections properly decrement counters

### Shutdown Process
1. ✅ Signal handlers trigger clean shutdown
2. ✅ D-Bus proxies and subscriptions cleaned up
3. ✅ GLib sources removed before main loop quit
4. ✅ Thread exits cleanly

## Troubleshooting

### Common Issues

**"Permission denied" errors**:
- Run with `sudo` or configure polkit rules
- Check BlueZ D-Bus policies

**"BlueZ service not ready"**:
```bash
# Check BlueZ status
systemctl status bluetooth
bluetoothctl version

# Restart if needed
sudo systemctl restart bluetooth
```

**Adapter not found**:
```bash
# List available adapters
bluetoothctl list

# Check D-Bus objects
busctl tree org.bluez
```

### Debug Information
Enable debug logging to see detailed D-Bus operations:
```bash
sudo ./build/ggk-standalone -d -v
```

Monitor D-Bus traffic:
```bash
# Monitor BlueZ D-Bus activity
sudo dbus-monitor --system "sender='org.bluez'"

# Monitor Bluetooth HCI traffic
sudo btmon
```

## Version Requirements

- **BlueZ**: 5.35+ (recommended: 5.77+)
- **GLib**: 2.58+
- **Kernel**: 4.4+ for modern Bluetooth features
- **systemd**: For service management

## Future Improvements

### Planned Enhancements
- [ ] Container/VM integration tests
- [ ] Metrics collection for call latency
- [ ] Advanced adapter configuration options
- [ ] Automatic reconnection on BlueZ restart
- [ ] Performance optimization for high-frequency operations

### Nice-to-Have
- [ ] Multiple adapter support
- [ ] Hot-plug adapter detection
- [ ] Advanced advertising features (BlueZ 5.77+)
- [ ] Mesh networking support

## Migration Impact

This migration maintains **full API compatibility** while significantly improving:
- **Reliability**: No more HCI timeout failures
- **Performance**: Asynchronous D-Bus operations
- **Maintainability**: Standard BlueZ interfaces
- **Compatibility**: Modern BlueZ version support

The change is **transparent to existing applications** using the GGK library.