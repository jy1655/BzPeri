// Copyright 2017-2025 Paul Nettle & Contributors
//
// This file is part of Gobbledegook.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

#pragma once

#ifdef LINUX_PERFORMANCE_OPTIMIZATION

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <optional>
#include <chrono>
#include <unordered_map>
#include <mutex>

#include "config.h"

// Modern BlueZ 5.77+ specific features and optimizations
namespace ggk::bluez {

// Modern error handling using std::expected (C++20)
enum class BlueZError
{
    Success,
    ConnectionFailed,
    InvalidAdapter,
    PermissionDenied,
    ServiceNotFound,
    CharacteristicNotFound,
    InvalidUUID,
    BufferOverflow,
    Timeout,
    UnknownError
};

// Expected compatibility: use std::expected if available (C++23), otherwise a minimal fallback
#if defined(__cpp_lib_expected) && (__cpp_lib_expected >= 202211L)
  #include <expected>
  template<typename T>
  using BlueZResult = std::expected<T, BlueZError>;
#else
  template<typename T>
  class BlueZExpected
  {
  public:
    BlueZExpected(T value) : ok_(true), value_(std::move(value)), error_(BlueZError::Success) {}
    BlueZExpected(BlueZError err) : ok_(false), value_(), error_(err) {}

    bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }
    T& value() & noexcept { return value_; }
    const T& value() const & noexcept { return value_; }
    T&& value() && noexcept { return std::move(value_); }
    const BlueZError& error() const noexcept { return error_; }

    static BlueZExpected success(T v) { return BlueZExpected(std::move(v)); }
    static BlueZExpected failure(BlueZError e) { return BlueZExpected(e); }

  private:
    bool ok_ = false;
    T value_{};
    BlueZError error_ = BlueZError::UnknownError;
  };

  template<>
  class BlueZExpected<void>
  {
  public:
    BlueZExpected() : ok_(true), error_(BlueZError::Success) {}
    explicit BlueZExpected(BlueZError err) : ok_(false), error_(err) {}

    bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }
    void value() const noexcept {}
    const BlueZError& error() const noexcept { return error_; }

    static BlueZExpected success() { return BlueZExpected(); }
    static BlueZExpected failure(BlueZError e) { return BlueZExpected(e); }

  private:
    bool ok_ = false;
    BlueZError error_ = BlueZError::UnknownError;
  };

  template<typename T>
  using BlueZResult = BlueZExpected<T>;
#endif

// Advanced BlueZ adapter management for modern versions
class ModernAdapterManager
{
public:
    struct AdapterInfo
    {
        std::string path;
        std::string address;
        std::string name;
        bool powered = false;
        bool discoverable = false;
        bool pairable = false;
        std::chrono::seconds discoverableTimeout{0};
        std::chrono::seconds pairableTimeout{0};
    };

    static BlueZResult<std::vector<AdapterInfo>> enumerateAdapters() noexcept;
    static BlueZResult<void> setAdapterPowered(std::string_view adapterPath, bool powered) noexcept;
    static BlueZResult<void> setAdapterDiscoverable(std::string_view adapterPath, bool discoverable, std::chrono::seconds timeout = std::chrono::seconds{0}) noexcept;

private:
    static std::optional<AdapterInfo> parseAdapterInfo(std::string_view dbusObjectPath) noexcept;
};

// Enhanced GATT server with modern BlueZ 5.77+ features
class ModernGattServer
{
public:
    struct ServiceConfiguration
    {
        std::string uuid;
        bool primary = true;
        std::vector<std::string> includes;
        std::string_view handle; // Optional explicit handle
    };

    struct CharacteristicConfiguration
    {
        std::string uuid;
        std::vector<std::string> flags;
        std::optional<uint16_t> mtu;
        bool notifyAcquired = false; // Use AcquireNotify for efficiency
        bool writeAcquired = false;  // Use AcquireWrite for efficiency
    };

    // Register service with modern BlueZ features
    static BlueZResult<std::string> registerService(const ServiceConfiguration& config) noexcept;

    // Register characteristic with advanced features
    static BlueZResult<std::string> registerCharacteristic(std::string_view servicePath, const CharacteristicConfiguration& config) noexcept;

    // Efficient bulk data transfer using acquired write/notify
    static BlueZResult<void> bulkNotify(std::string_view characteristicPath, std::span<const uint8_t> data) noexcept;
    static BlueZResult<std::span<uint8_t>> bulkRead(std::string_view characteristicPath, std::span<uint8_t> buffer) noexcept;
};

// Performance monitoring for BlueZ operations
class BlueZMetrics
{
public:
    struct ConnectionMetrics
    {
        std::chrono::milliseconds connectionTime{0};
        std::chrono::milliseconds lastDataTransfer{0};
        uint64_t bytesTransferred = 0;
        uint32_t packetsTransferred = 0;
        uint32_t errors = 0;
    };

    static void recordConnection(std::string_view devicePath, std::chrono::milliseconds connectionTime) noexcept;
    static void recordDataTransfer(std::string_view devicePath, size_t bytes) noexcept;
    static void recordError(std::string_view devicePath, BlueZError error) noexcept;

    static std::optional<ConnectionMetrics> getMetrics(std::string_view devicePath) noexcept;
    static void clearMetrics() noexcept;

private:
    static inline std::unordered_map<std::string, ConnectionMetrics> metrics_;
    static inline std::mutex metricsMutex_;
};

// Advanced D-Bus introspection for BlueZ 5.77+ features
class BlueZIntrospection
{
public:
    struct InterfaceInfo
    {
        std::string name;
        std::vector<std::string> methods;
        std::vector<std::string> properties;
        std::vector<std::string> signals;
    };

    // Discover available BlueZ interfaces and their capabilities
    static BlueZResult<std::vector<InterfaceInfo>> introspectBlueZManager() noexcept;
    static BlueZResult<std::vector<std::string>> getAvailableAdapters() noexcept;

    // Check for specific BlueZ version features
    static bool supportsAcquiredOperations() noexcept;
    static bool supportsAdvancedAdvertising() noexcept;
    static std::optional<std::string> getBlueZVersion() noexcept;
};

// Utility functions for modern BlueZ interaction
namespace utils {
    // Convert BlueZ error codes to our enum
    BlueZError dbusErrorToBlueZError(std::string_view errorName) noexcept;

    // Format BlueZ object paths
    std::string formatAdapterPath(int adapterIndex) noexcept;
    std::string formatDevicePath(std::string_view adapterPath, std::string_view deviceAddress) noexcept;
    std::string formatServicePath(std::string_view devicePath, std::string_view serviceUuid) noexcept;

    // Efficient UUID operations
    bool isValidUuid(std::string_view uuid) noexcept;
    std::string normalizeUuid(std::string_view uuid) noexcept;
    std::string uuidToPath(std::string_view uuid) noexcept;
}

} // namespace ggk::bluez

#endif // LINUX_PERFORMANCE_OPTIMIZATION
