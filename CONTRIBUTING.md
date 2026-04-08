# Contributing to BzPeri

Thank you for your interest in contributing to BzPeri! This document provides guidelines and instructions for contributing.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Coding Standards](#coding-standards)
- [Submitting Changes](#submitting-changes)
- [Reporting Issues](#reporting-issues)

## Code of Conduct

By participating in this project, you agree to maintain a respectful and inclusive environment. Please be kind and constructive in all interactions.

## Getting Started

### Prerequisites

- **Linux** with BlueZ >= 5.77 (for runtime testing)
- **C++20** compatible compiler (GCC 10+ or Clang 12+)
- **CMake** >= 3.16
- **GLib/GIO** >= 2.58
- **libbluetooth-dev** (BlueZ development headers)

### Fork and Clone

1. Fork the repository on GitHub
2. Clone your fork locally:
   ```bash
   git clone https://github.com/YOUR_USERNAME/BzPeri.git
   cd BzPeri
   ```
3. Add upstream remote:
   ```bash
   git remote add upstream https://github.com/jy1655/BzPeri.git
   ```

## Development Setup

### Building from Source

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt install build-essential cmake pkg-config \
    libglib2.0-dev libbluetooth-dev bluez

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Check the host, then run the managed demo (requires sudo for BlueZ access)
sudo ./bzp-standalone doctor
sudo ./bzp-standalone demo -d
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SHARED_LIBS` | ON | Build shared library |
| `BUILD_STANDALONE` | ON (Linux) | Build example application |
| `BUILD_TESTING` | OFF | Build tests |
| `ENABLE_BLUEZ_ADVANCED` | ON (Linux) | Enable BlueZ 5.77+ features |

## Coding Standards

### Language & Style

- **C++20** standard, namespace `bzp`
- **Indentation**: Tabs (match existing files)
- **Line length**: Soft limit ~120 characters
- **Headers**: Use `#pragma once`

### Naming Conventions

| Type | Convention | Example |
|------|------------|---------|
| Types/Classes | PascalCase | `GattService`, `DBusObject` |
| Files | PascalCase | `GattService.cpp`, `BluezAdapter.h` |
| Functions/Methods | camelCase | `registerService()`, `getValue()` |
| Macros/Constants | UPPER_SNAKE | `MAX_BUFFER_SIZE`, `BLUEZ_VERSION` |
| Variables | camelCase | `serviceUuid`, `isConnected` |

### Code Quality

- **No type suppression**: Never use `as any`, `@ts-ignore`, `@ts-expect-error` equivalents
- **No empty catch blocks**: Always handle or log errors
- **Include guards**: Use `#pragma once`
- **Comments**: Document public APIs, complex logic

### Example Code Style

```cpp
#pragma once

#include <string>
#include <expected>

namespace bzp {

class GattService {
public:
    // Creates a new GATT service with the given UUID
    explicit GattService(const std::string& uuid);
    
    // Returns the service UUID
    [[nodiscard]] const std::string& getUuid() const noexcept;
    
    // Registers the service with BlueZ
    // Returns error message on failure
    std::expected<void, std::string> registerWithBluez();

private:
    std::string m_uuid;
    bool m_registered = false;
};

} // namespace bzp
```

## Submitting Changes

### Branch Naming

- `feature/description` - New features
- `fix/description` - Bug fixes
- `docs/description` - Documentation only
- `refactor/description` - Code refactoring

### Commit Messages

- Use concise, imperative subject line (<=72 chars)
- Reference issues when applicable (`#123`)
- Provide details in body when needed

**Good examples:**
```
Fix memory leak in GVariant path handling

Add bonding configuration option to bzpStart API

Update README with ARM64 installation instructions
```

**Bad examples:**
```
fixed stuff
WIP
update
```

### Pull Request Process

1. **Create a feature branch** from `main`:
   ```bash
   git checkout -b feature/my-feature
   ```

2. **Make your changes** following the coding standards

3. **Test your changes**:
   ```bash
   cd build
   cmake .. -DCMAKE_BUILD_TYPE=Debug
   make -j$(nproc)
   sudo ./bzp-standalone doctor
   sudo ./bzp-standalone demo -d
   sudo ./bzp-standalone inspect --live  # Optional second terminal
   ```

4. **Commit your changes** with clear messages

5. **Push to your fork**:
   ```bash
   git push origin feature/my-feature
   ```

6. **Create a Pull Request** on GitHub with:
   - Clear description of the changes
   - Reference to related issues
   - Any breaking changes noted
   - Testing performed

### PR Requirements

- [ ] Builds cleanly without warnings
- [ ] Follows coding standards
- [ ] Updates documentation if behavior changes
- [ ] Minimal scope (one feature/fix per PR)
- [ ] No unrelated changes

## Reporting Issues

### Bug Reports

Please include:
- **Environment**: Linux distribution, BlueZ version, compiler version
- **Steps to reproduce**: Minimal steps to trigger the issue
- **Expected behavior**: What should happen
- **Actual behavior**: What actually happens
- **Logs**: `doctor`, `demo -d`, or `inspect --live --verbose-events` output if applicable

### Feature Requests

Please include:
- **Use case**: Why is this feature needed?
- **Proposed solution**: How do you envision it working?
- **Alternatives considered**: Other approaches you've thought of

## Questions?

- Open a [GitHub Issue](https://github.com/jy1655/BzPeri/issues) for questions
- Check existing issues and documentation first

---

Thank you for contributing to BzPeri!
