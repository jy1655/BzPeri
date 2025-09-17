// Copyright 2017-2025 Paul Nettle & Contributors
//
// This file is part of Gobbledegook.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

#pragma once

#include <memory>
#include <string_view>
#include <functional>
#include <concepts>
#include <coroutine>
#include <ranges>
#include <expected>
#include <span>

#include "config.h"
#include "GattInterface.h"
#include "Server.h"

#ifdef BLUEZ_ADVANCED_FEATURES
#include "BluezModern.h"
#endif

namespace bzp::gatt {

// Modern C++20 concepts for GATT operations
template<typename T>
concept GattDataProvider = requires(T t, const char* name) {
    { t.getData(name) } -> std::convertible_to<const void*>;
    { t.setData(name, std::declval<const void*>()) } -> std::convertible_to<bool>;
};

template<typename T>
concept GattUuidLike = requires(T t) {
    { std::string_view{t} };
} || requires(T t) {
    { t.toString() } -> std::convertible_to<std::string>;
};

// Modern GATT characteristic with enhanced features
class ModernGattCharacteristic : public GattInterface
{
public:
    // Modern property flags using strongly typed enum
    enum class Properties : uint32_t
    {
        None = 0,
        Read = 1 << 0,
        Write = 1 << 1,
        WriteWithoutResponse = 1 << 2,
        Notify = 1 << 3,
        Indicate = 1 << 4,
        AuthenticatedSignedWrites = 1 << 5,
        ExtendedProperties = 1 << 6,
        ReliableWrite = 1 << 7,
        WritableAuxiliaries = 1 << 8,
        EncryptRead = 1 << 9,
        EncryptWrite = 1 << 10,
        EncryptAuthenticatedRead = 1 << 11,
        EncryptAuthenticatedWrite = 1 << 12,
        SecureRead = 1 << 13,
        SecureWrite = 1 << 14,
        Authorize = 1 << 15
    };

    friend constexpr Properties operator|(Properties a, Properties b) noexcept
    {
        return static_cast<Properties>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    friend constexpr Properties operator&(Properties a, Properties b) noexcept
    {
        return static_cast<Properties>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    // Modern callback types using std::function and concepts
    using ReadCallback = std::function<std::expected<std::vector<uint8_t>, std::error_code>(std::span<uint8_t>)>;
    using WriteCallback = std::function<std::expected<void, std::error_code>(std::span<const uint8_t>)>;
    using NotifyCallback = std::function<void(bool enabled)>;

    // Constructor with modern initialization
    ModernGattCharacteristic(DBusObject& owner,
                           GattService& service,
                           std::string_view name,
                           GattUuidLike auto const& uuid,
                           Properties properties = Properties::Read);

    // Modern fluent interface for configuration
    auto& withReadCallback(ReadCallback callback) noexcept;
    auto& withWriteCallback(WriteCallback callback) noexcept;
    auto& withNotifyCallback(NotifyCallback callback) noexcept;
    auto& withMtu(uint16_t mtu) noexcept;
    auto& withSecurity(Properties securityLevel) noexcept;

#ifdef BLUEZ_ADVANCED_FEATURES
    // Advanced BlueZ 5.77+ features
    auto& withAcquiredWrite(bool enabled = true) noexcept;
    auto& withAcquiredNotify(bool enabled = true) noexcept;
    auto& withHighThroughput(bool enabled = true) noexcept;
#endif

    // Efficient data operations
    std::expected<void, std::error_code> updateValue(std::span<const uint8_t> data) noexcept;
    std::expected<std::vector<uint8_t>, std::error_code> getValue() const noexcept;

    // Notification management
    std::expected<void, std::error_code> notify(std::span<const uint8_t> data) noexcept;
    std::expected<void, std::error_code> indicate(std::span<const uint8_t> data) noexcept;

private:
    Properties properties_;
    ReadCallback readCallback_;
    WriteCallback writeCallback_;
    NotifyCallback notifyCallback_;
    uint16_t mtu_ = 23; // Default BLE MTU
    bool acquiredWrite_ = false;
    bool acquiredNotify_ = false;
    std::vector<uint8_t> currentValue_;
};

// Modern GATT service with C++20 features
class ModernGattService : public GattInterface
{
public:
    // Service types
    enum class Type
    {
        Primary,
        Secondary
    };

    ModernGattService(DBusObject& owner,
                     std::string_view name,
                     GattUuidLike auto const& uuid,
                     Type type = Type::Primary);

    // Modern characteristic creation with perfect forwarding
    template<typename... Args>
    auto& addCharacteristic(Args&&... args)
    {
        auto characteristic = std::make_unique<ModernGattCharacteristic>(
            std::forward<Args>(args)...);
        auto& ref = *characteristic;
        characteristics_.emplace_back(std::move(characteristic));
        return ref;
    }

    // Characteristic lookup with modern algorithms
    auto findCharacteristic(std::string_view name) const noexcept
        -> std::optional<std::reference_wrapper<const ModernGattCharacteristic>>;

    // Range-based iteration over characteristics
    auto characteristics() const noexcept
    {
        return characteristics_ | std::views::transform([](const auto& ptr) -> const auto& { return *ptr; });
    }

private:
    Type type_;
    std::vector<std::unique_ptr<ModernGattCharacteristic>> characteristics_;
};

// High-level GATT server with modern C++20 design
class ModernGattServer
{
public:
    // Server configuration
    struct Configuration
    {
        std::string deviceName = "BzPeri Server";
        std::string shortName = "BZP";
        bool advertisingEnabled = true;
        uint16_t advertisingInterval = 100; // ms
        std::vector<std::string> advertisingUuids;
        bool connectable = true;
        bool discoverable = true;
        std::chrono::seconds discoverableTimeout{0};
    };

    explicit ModernGattServer(Configuration config = {});
    ~ModernGattServer();

    // Service management with perfect forwarding
    template<typename... Args>
    auto& addService(Args&&... args)
    {
        auto service = std::make_unique<ModernGattService>(std::forward<Args>(args)...);
        auto& ref = *service;
        services_.emplace_back(std::move(service));
        return ref;
    }

    // Modern server lifecycle management
    std::expected<void, std::error_code> start() noexcept;
    std::expected<void, std::error_code> stop() noexcept;
    std::expected<void, std::error_code> restart() noexcept;

    // Server state queries
    [[nodiscard]] bool isRunning() const noexcept { return running_; }
    [[nodiscard]] const Configuration& getConfiguration() const noexcept { return config_; }

    // Data provider integration with concepts
    template<GattDataProvider T>
    void setDataProvider(T&& provider)
    {
        dataProvider_ = std::forward<T>(provider);
    }

    // Advanced features for Linux optimization
#ifdef LINUX_PERFORMANCE_OPTIMIZATION
    std::expected<void, std::error_code> enableHighPerformanceMode() noexcept;
    std::expected<void, std::error_code> setConnectionPriority(int priority) noexcept;
    std::expected<void, std::error_code> optimizeForThroughput() noexcept;
    std::expected<void, std::error_code> optimizeForLatency() noexcept;
#endif

#ifdef BLUEZ_ADVANCED_FEATURES
    // BlueZ 5.77+ specific features
    std::expected<void, std::error_code> enableExtendedAdvertising() noexcept;
    std::expected<void, std::error_code> setAdvertisingData(std::span<const uint8_t> data) noexcept;
    std::expected<void, std::error_code> setScanResponseData(std::span<const uint8_t> data) noexcept;
#endif

private:
    Configuration config_;
    std::vector<std::unique_ptr<ModernGattService>> services_;
    bool running_ = false;
    std::any dataProvider_; // Type-erased storage for any data provider

    // Internal implementation
    std::expected<void, std::error_code> initializeBlueZ() noexcept;
    std::expected<void, std::error_code> registerServices() noexcept;
    std::expected<void, std::error_code> startAdvertising() noexcept;
    void cleanup() noexcept;
};

// Utility functions for modern GATT server
namespace utils {
    // UUID validation and conversion
    std::expected<std::string, std::error_code> validateAndNormalizeUuid(std::string_view uuid) noexcept;

    // Data conversion helpers
    std::vector<uint8_t> stringToBytes(std::string_view str) noexcept;
    std::string bytesToString(std::span<const uint8_t> bytes) noexcept;
    std::string bytesToHex(std::span<const uint8_t> bytes) noexcept;

    // BlueZ path helpers
    std::string generateServicePath(std::string_view serviceName) noexcept;
    std::string generateCharacteristicPath(std::string_view servicePath, std::string_view charName) noexcept;
}

} // namespace bzp::gatt