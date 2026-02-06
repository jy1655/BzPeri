# Changelog

All notable changes to BzPeri will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
| 0.1.9 | 2025-11-14 | Debian 12 arm64 container builds |
| 0.1.7 | 2025-11-14 | GLIBCXX/Debian 12 compatibility |
| 0.1.3 | 2025-11-14 | std::format fixes |
| 0.1.1 | 2025-09-19 | API rename, samples reorganization |
| 0.1.0 | 2025-09-18 | Multi-arch support, APT repository |
| 0.0.1 | 2025-09-17 | Initial C++20 modernization release |
