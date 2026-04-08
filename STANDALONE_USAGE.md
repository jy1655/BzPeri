# Standalone Application Usage

## Overview

`bzp-standalone` is the main sample application for validating a BzPeri build. In the `v0.2.x` line it now acts as a small terminal-first control plane:

- `bzp-standalone doctor` checks whether the host is ready for the happy path
- `bzp-standalone demo` starts the managed sample BLE server
- `bzp-standalone inspect --live` reads the managed demo session state without scraping stdout

For backward compatibility, calling `bzp-standalone` with the old flat flags still enters the demo path.

The intended first run is a 3-command workflow:

```bash
sudo ./build/bzp-standalone doctor
sudo ./build/bzp-standalone demo -d
sudo ./build/bzp-standalone inspect --live
```

`doctor` explains whether the box is sane. `demo` proves that the bundled sample really publishes. `inspect --live` shows the selected object, recent live events, and how to reveal the full object tree or verbose events when needed.

## Workflow Paths

### Ubuntu APT Install
```bash
sudo apt install bzperi bzperi-dev bzperi-tools
sudo bzp-standalone doctor
sudo bzp-standalone demo -d
sudo bzp-standalone inspect --live
```

### Source Build
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel $(nproc)

# Required before the first managed demo on a fresh host
sudo cp ../dbus/com.bzperi.conf /etc/dbus-1/system.d/
sudo ../scripts/configure-bluez-experimental.sh enable

sudo ./bzp-standalone doctor
sudo ./bzp-standalone demo -d
sudo ./bzp-standalone inspect --live
```

### Raspberry Pi OS / Debian 12 over SSH
```bash
# Terminal 1: build or install, then check the host
sudo ./build/bzp-standalone doctor

# Terminal 1: start the managed sample
sudo ./build/bzp-standalone demo -d

# Terminal 2 over SSH: inspect the live session
sudo ./build/bzp-standalone inspect --live
```

Use a phone app or second Linux machine as the verification client. The default sample exposes a battery characteristic at the reported `probe path` and a write-capable text characteristic at the reported `write probe` path, so a successful first run is: advertising is visible, the battery value can be read, the text value can be written, and `inspect --live` shows the object metadata plus read/write/notify activity.

## Command Line Options

### Logging Options
```bash
# Quiet mode (errors only)
sudo ./build/bzp-standalone demo -q

# Verbose mode
sudo ./build/bzp-standalone demo -v

# Debug mode (most detailed)
sudo ./build/bzp-standalone demo -d
```

### BlueZ Adapter Options

#### List Available Adapters
```bash
# List all available BlueZ adapters
sudo ./build/bzp-standalone doctor --list-adapters

# Example output:
#   ADAPTERS
#   /org/bluez/hci0 [00:1A:2B:3C:4D:5E] powered=yes
#   /org/bluez/hci1 [AA:BB:CC:DD:EE:FF] powered=no
```

#### Select Specific Adapter
```bash
# Use specific adapter by name
sudo ./build/bzp-standalone doctor --adapter=hci1
sudo ./build/bzp-standalone demo --adapter=hci1

# Use specific adapter by path
sudo ./build/bzp-standalone doctor --adapter=/org/bluez/hci1
sudo ./build/bzp-standalone demo --adapter=/org/bluez/hci1

# Use specific adapter by MAC address
sudo ./build/bzp-standalone doctor --adapter=00:1A:2B:3C:4D:5E
sudo ./build/bzp-standalone demo --adapter=00:1A:2B:3C:4D:5E
```

#### Combined Options
```bash
# Debug demo with a specific adapter
sudo ./build/bzp-standalone demo -d --adapter=hci1

# Show the object tree and low-signal events while inspecting
sudo ./build/bzp-standalone inspect --live --show-tree --verbose-events
```

### Service Configuration Options
```bash
# Override the D-Bus service namespace (updates characteristic paths)
sudo ./build/bzp-standalone demo --service-name=my_device

# Customize LE advertising names
sudo ./build/bzp-standalone demo --advertise-name="My Device" --advertise-short=MyDev

# Place bundled example services under a custom namespace node
sudo ./build/bzp-standalone demo --service-name=my_device --sample-namespace=demo

# Start with an empty server (no bundled example services)
sudo ./build/bzp-standalone demo --no-sample-services

# Re-enable bundled services after disabling them in the same invocation
sudo ./build/bzp-standalone demo --no-sample-services --with-sample-services

# Run without the internal BzPeri worker thread
sudo ./build/bzp-standalone demo --manual-loop

# Control suspend/resume helpers
sudo ./build/bzp-standalone demo --sleep-integration=off
sudo ./build/bzp-standalone demo --sleep-inhibitor=on

# Control GLib capture strategy
sudo ./build/bzp-standalone demo --glib-log-capture=host
sudo ./build/bzp-standalone demo --glib-log-targets=log,printerr
sudo ./build/bzp-standalone demo --glib-log-domains=bluez,gio
```

### Help
```bash
# Show help message
./build/bzp-standalone --help
./build/bzp-standalone doctor --help
./build/bzp-standalone demo --help
./build/bzp-standalone inspect --help
```

## Environment Variables

You can also use environment variables instead of command-line options:

```bash
# Set preferred adapter
export BLUEZ_ADAPTER=hci1
sudo ./build/bzp-standalone demo

# Enable adapter listing
export BLUEZ_LIST_ADAPTERS=1
sudo ./build/bzp-standalone doctor
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
sudo ./build/bzp-standalone doctor

# Or configure polkit rules (see BLUEZ_MIGRATION.md)
```

### Adapter Not Found
```bash
# List available adapters first
sudo ./build/bzp-standalone doctor --list-adapters

# Then use a valid adapter
sudo ./build/bzp-standalone demo --adapter=hci0
```

### BlueZ Service Issues
```bash
# Check BlueZ service status
systemctl status bluetooth

# Restart if needed
sudo systemctl restart bluetooth

# Then try again
sudo ./build/bzp-standalone doctor
```

## Example Usage Scenarios

### Development and Debugging
```bash
# Start with debug logging and specific adapter
sudo ./build/bzp-standalone demo -d --adapter=hci0
```

### Production Deployment
```bash
# Quiet mode for production
sudo ./build/bzp-standalone demo -q
```

### Multi-Adapter System
```bash
# First, see what's available
sudo ./build/bzp-standalone doctor --list-adapters

# Select the best adapter
sudo ./build/bzp-standalone demo --adapter=hci1
```

### Manual Run-Loop Validation
```bash
# Run the sample in host-driven manual-loop mode
sudo ./build/bzp-standalone demo -d --manual-loop
```

### GLib Capture Validation
```bash
# Keep GLib capture fully host-managed
sudo ./build/bzp-standalone demo -v --glib-log-capture=host

# Use startup/shutdown-only capture with BlueZ/GIO domain filtering
sudo ./build/bzp-standalone demo -v \
  --glib-log-capture=startup-shutdown \
  --glib-log-domains=bluez,gio
```

### USB Bluetooth Dongle
```bash
# When using USB Bluetooth dongles, they often appear as hci1 or higher
sudo ./build/bzp-standalone demo --adapter=hci1
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

- **D-Bus Policy**: `/usr/share/dbus-1/system.d/com.bzperi.conf` for packaged installs, or `/etc/dbus-1/system.d/com.bzperi.conf` for source-build/manual installs
- **Polkit Rules**: `/etc/polkit-1/rules.d/50-bzperi-bluez.rules`
- **BlueZ Config**: `/etc/bluetooth/main.conf`

See `BLUEZ_MIGRATION.md` for detailed configuration instructions.
