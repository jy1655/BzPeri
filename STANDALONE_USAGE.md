# Standalone Application Usage

## Overview

The `bzp-standalone` application now supports modern BlueZ adapter selection and enhanced command-line options.

## Command Line Options

### Basic Usage
```bash
sudo ./build/bzp-standalone [options]
```

### Logging Options
```bash
# Quiet mode (errors only)
sudo ./build/bzp-standalone -q

# Verbose mode
sudo ./build/bzp-standalone -v

# Debug mode (most detailed)
sudo ./build/bzp-standalone -d
```

### BlueZ Adapter Options

#### List Available Adapters
```bash
# List all available BlueZ adapters
sudo ./build/bzp-standalone --list-adapters

# Example output:
#   Available BlueZ adapters:
#     /org/bluez/hci0 (00:1A:2B:3C:4D:5E) - Powered: true
#     /org/bluez/hci1 (AA:BB:CC:DD:EE:FF) - Powered: false
```

#### Select Specific Adapter
```bash
# Use specific adapter by name
sudo ./build/bzp-standalone --adapter=hci1

# Use specific adapter by path
sudo ./build/bzp-standalone --adapter=/org/bluez/hci1

# Use specific adapter by MAC address
sudo ./build/bzp-standalone --adapter=00:1A:2B:3C:4D:5E
```

#### Combined Options
```bash
# Debug mode with specific adapter
sudo ./build/bzp-standalone -d --adapter=hci1

# List adapters with verbose output
sudo ./build/bzp-standalone -v --list-adapters
```

### Help
```bash
# Show help message
./build/bzp-standalone --help
```

## Environment Variables

You can also use environment variables instead of command-line options:

```bash
# Set preferred adapter
export BLUEZ_ADAPTER=hci1
sudo ./build/bzp-standalone

# Enable adapter listing
export BLUEZ_LIST_ADAPTERS=1
sudo ./build/bzp-standalone
```

## Adapter Selection Logic

The application follows this priority order for adapter selection:

1. **Command-line specified adapter** (`--adapter=` option)
2. **Environment variable** (`BLUEZ_ADAPTER`)
3. **First powered adapter** (if any)
4. **First available adapter** (as fallback)

## Error Handling

### Permission Issues
```bash
# If you get permission errors:
sudo ./build/bzp-standalone

# Or configure polkit rules (see BLUEZ_MIGRATION.md)
```

### Adapter Not Found
```bash
# List available adapters first
sudo ./build/bzp-standalone --list-adapters

# Then use a valid adapter
sudo ./build/bzp-standalone --adapter=hci0
```

### BlueZ Service Issues
```bash
# Check BlueZ service status
systemctl status bluetooth

# Restart if needed
sudo systemctl restart bluetooth

# Then try again
sudo ./build/bzp-standalone
```

## Example Usage Scenarios

### Development and Debugging
```bash
# Start with debug logging and specific adapter
sudo ./build/bzp-standalone -d --adapter=hci0
```

### Production Deployment
```bash
# Quiet mode for production
sudo ./build/bzp-standalone -q
```

### Multi-Adapter System
```bash
# First, see what's available
sudo ./build/bzp-standalone --list-adapters

# Select the best adapter
sudo ./build/bzp-standalone --adapter=hci1
```

### USB Bluetooth Dongle
```bash
# When using USB Bluetooth dongles, they often appear as hci1 or higher
sudo ./build/bzp-standalone --adapter=hci1
```

## Monitoring and Troubleshooting

### View BlueZ Logs
```bash
# Monitor BlueZ service logs
journalctl -u bluetooth -f
```

### Monitor D-Bus Traffic
```bash
# Monitor BlueZ D-Bus activity
sudo dbus-monitor --system "sender='org.bluez'"
```

### Monitor Bluetooth HCI Traffic
```bash
# Monitor low-level Bluetooth traffic
sudo btmon
```

### Check Adapter Status
```bash
# Using bluetoothctl
bluetoothctl list
bluetoothctl show hci0

# Using D-Bus
busctl tree org.bluez
```

## Exit Codes

- **0**: Success
- **1**: Server health error or configuration failure
- **-1**: Initialization failure or invalid arguments

## Signal Handling

The application handles these signals gracefully:

- **SIGINT** (Ctrl+C): Graceful shutdown
- **SIGTERM**: Graceful shutdown

Both signals will trigger a clean shutdown sequence that properly closes all BlueZ connections and cleans up resources.

## Configuration Files

The application respects these configuration files:

- **D-Bus Policy**: `/etc/dbus-1/system.d/com.bzperi.conf`
- **Polkit Rules**: `/etc/polkit-1/rules.d/50-bzperi-bluez.rules`
- **BlueZ Config**: `/etc/bluetooth/main.conf`

See `BLUEZ_MIGRATION.md` for detailed configuration instructions.