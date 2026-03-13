# Changelog

All notable changes to BzPeri will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-03-13

This release is the first `0.2.x` line and is the largest functional update since `0.1.9`.
Where `0.1.9` focused mainly on packaging and Debian/arm64 build compatibility, `0.2.0`
focuses on runtime hardening, host integration, migration tooling, and operational controls.

### Added
- Manual run-loop startup and host-driven execution APIs:
  - `bzpStartManual()` / `bzpStartWithBondableManual()`
  - `bzpRunLoopIteration()` / `bzpRunLoopIterationFor()`
  - `bzpRunLoopAttach()` / `bzpRunLoopDetach()`
  - hidden poll integration via `bzpRunLoopPollPrepare()` / `Query()` / `Check()` / `Dispatch()` / `Cancel()`
  - run-loop invoke helpers and drive-until-state/shutdown helpers
- Detailed `Ex` result-code APIs across startup, shutdown, wait, run-loop, update queue, notifications, and GLib capture control
- Wrapper request-object public extension surface for method calls, replies, signals, notifications, updates, and variants
- Optional compatibility build switches:
  - `ENABLE_LEGACY_SINGLETON_COMPAT=OFF`
  - `ENABLE_LEGACY_RAW_GLIB_COMPAT=OFF`
- GLib log capture modes and controls:
  - `AUTOMATIC`, `DISABLED`, `HOST_MANAGED`, `STARTUP_AND_SHUTDOWN`
  - target filtering (`log`, `print`, `printerr`)
  - domain filtering (`default`, `glib`, `gio`, `bluez`, `other`)
  - pause/resume helpers
  - contention tracking when another component replaces a GLib handler while capture is active
- Power-management controls:
  - `PrepareForSleep` integration toggles
  - optional login1 delay inhibitor support with runtime/build-time configuration
- Public migration documentation:
  - `COMPATIBILITY_MIGRATION.md`
  - expanded `BLUEZ_MIGRATION.md`
- Automated unit/regression test target with `ctest`

### Changed
- Canonical public customization path now prefers wrapper request objects over raw GLib callback signatures
- Canonical C integration path now prefers `Ex` and query-result APIs over legacy `0/1` and `void` helpers
- Advertising payload selection now uses actual registered GATT service UUIDs instead of a fixed hard-coded subset
- Runtime logging is more structured and consistent for connection, recovery, and advertising state transitions
- Public documentation now explicitly separates user usage, BlueZ/D-Bus migration notes, and compatibility/deprecation migration

### Fixed
- D-Bus policy regression blocking `org.freedesktop.DBus.Properties`
- Deadlock risk when connection callbacks re-entered `BluezAdapter`
- Adapter hot-unplug/hot-plug recovery path
- Shutdown-time `GVariant` ownership assertions
- Several legacy raw GLib compatibility leaks into the public API surface
- Pre-start wait/shutdown edge cases that previously returned ambiguous success/failure values
- Object-path sanitization and additional runtime state tracking around GLib capture and manual loop ownership

### Notes for Upgrading from 0.1.9
- Existing code should continue to build with default compatibility settings.
- New code should prefer:
  - corrected `bzpNotify*` names
  - `Ex` result-code APIs
  - wrapper request-object callback APIs
  - `HOST_MANAGED` or narrower GLib capture settings when embedding inside a larger process
- To verify that your application no longer depends on the main legacy surfaces, build with:

```bash
cmake -DENABLE_LEGACY_SINGLETON_COMPAT=OFF -DENABLE_LEGACY_RAW_GLIB_COMPAT=OFF
```

## [0.1.9] - 2025-11-14

### Changed
- Enable Debian 12 (bookworm) container for arm64 builds
- Improved cross-platform build compatibility

## [0.1.8] - 2025-11-14

### Fixed
- Add bash as default shell for pushd/popd support in build scripts

## [0.1.7] - 2025-11-14

### Fixed
- Fix GLIBCXX compatibility for Debian 12 bookworm
- Disabled std::format for broader compatibility (using snprintf fallback)

## [0.1.5] - 2025-11-14

### Fixed
- Fix Debian package dependencies for better compatibility

## [0.1.4] - 2025-11-14

### Added
- Add public.gpg export for APT repository GPG key compatibility

## [0.1.3] - 2025-11-14

### Fixed
- Fix std::format constexpr compatibility issue
- Improve README for better user experience

## [0.1.2] - 2025-09-19

### Changed
- BzPeri header packaging improvements
- Better header organization for downstream projects

## [0.1.1] - 2025-09-19

### Changed
- Rename Gobbledegook interface to BluezPeripheral
- Move sample app under `samples/` directory
- Refactor service configuration API
- Normalize dot-scoped D-Bus paths

### Fixed
- Documentation and license header updates

## [0.1.0] - 2025-09-18

### Added
- Multi-architecture support (amd64, arm64)
- Architecture auto-detection in CMake
- APT repository with GitHub Pages hosting
- BlueZ experimental mode configuration helper script

### Changed
- Improved APT repository UX and documentation
- Enhanced packaging consistency across architectures

## [0.0.9.x] - 2025-09-17

### Fixed
- APT repository GPG signing improvements
- Release file structure fixes
- GitHub Actions workflow refinements

## [0.0.1] - 2025-09-17

### Added
- Initial release of BzPeri
- Modern C++20 codebase (migrated from Gobbledegook)
- BlueZ 5.77+ compatibility
- DSL-style GATT service definition API
- Bondable/pairing configuration support
- D-Bus policy file for permissions
- Debian packaging support

### Changed
- Modernized from original Gobbledegook (2019) to C++20
- Updated from HCI Management API to D-Bus API
- Improved error handling with std::expected

### Credits
- Based on [Gobbledegook](https://github.com/nettlep/gobbledegook) by Paul Nettle
- Original work licensed under BSD License

---

## Version History Summary

| Version | Date | Highlights |
|---------|------|------------|
| 0.2.0 | 2026-03-13 | Runtime hardening, manual run loop, Ex APIs, GLib capture/power controls |
| 0.1.9 | 2025-11-14 | Debian 12 arm64 container builds |
| 0.1.7 | 2025-11-14 | GLIBCXX/Debian 12 compatibility |
| 0.1.3 | 2025-11-14 | std::format fixes |
| 0.1.1 | 2025-09-19 | API rename, samples reorganization |
| 0.1.0 | 2025-09-18 | Multi-arch support, APT repository |
| 0.0.1 | 2025-09-17 | Initial C++20 modernization release |
