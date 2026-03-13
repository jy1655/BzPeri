// Copyright (c) 2025 JaeYoung Hwang (BzPeri Project)
// Licensed under MIT License (see LICENSE file)
//
// Modern BlueZ adapter management for BzPeri

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  INSIDE THIS FILE
// >>
//
// Modern BlueZ D-Bus adapter interface - replaces HciAdapter/Mgmt HCI Management API
//
// >>
// >>>  DISCUSSION
// >>
//
// This class provides a modern D-Bus interface to BlueZ adapter functionality, replacing the legacy HCI Management API.
// It uses standard BlueZ D-Bus interfaces (org.bluez.Adapter1, org.bluez.LEAdvertisingManager1) for better compatibility
// and performance with modern BlueZ versions (5.77+).
//
// Key improvements over HCI approach:
// - No HCI socket timeouts
// - Asynchronous property changes
// - Standard BlueZ API compliance
// - Better error handling
// - Connection tracking via D-Bus signals
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <bzp/GLibTypes.h>
#include <string>
#include <atomic>
#include <functional>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <bzp/BluezTypes.h>

namespace bzp {

// Forward declarations
class BluezAdvertisement;
struct Server;

class BluezAdapter
{
public:
	// Legacy singleton access retained for compatibility.
	[[deprecated("Use getActiveBluezAdapter() or getActiveBluezAdapterPtr() instead of BluezAdapter::getInstance()")]]
	static BluezAdapter& getInstance();

	// Initialization and cleanup with adapter discovery
	BluezResult<void> initialize(const std::string& preferredAdapter = "");
	void shutdown();
	void setServiceNameContext(std::string serviceName);
	void setServerContext(const Server* server) noexcept { serverContext_ = server; }

	// Adapter discovery and selection
	BluezResult<std::vector<AdapterInfo>> discoverAdapters();
	BluezResult<void> selectAdapter(const std::string& adapterPath);
	BluezResult<AdapterInfo> getAdapterInfo() const;

	// Adapter configuration methods with proper error handling
	BluezResult<void> setPowered(bool enabled);
	BluezResult<void> setDiscoverable(bool enabled, uint16_t timeout = 0);
	BluezResult<void> setConnectable(bool enabled);
	BluezResult<void> setBondable(bool enabled);
	BluezResult<void> setName(const std::string& name, const std::string& shortName = "");

	// LE specific methods
	BluezResult<void> setLEEnabled(bool enabled);
	BluezResult<void> setAdvertising(bool enabled);

	// Feature detection
	BluezResult<BluezCapabilities> detectCapabilities();
	bool hasCapability(const std::string& interface) const;

	// Connection tracking (replaces HciAdapter connection counting)
	int getActiveConnectionCount() const { return activeConnections.load(); }

	// Adapter information
	std::string getAdapterPath() const { return adapterPath; }
	bool isInitialized() const { return initialized; }

	// Callback registration for connection events
	using ConnectionCallback = std::function<void(bool connected, const std::string& devicePath)>;
	void setConnectionCallback(ConnectionCallback callback) { connectionCallback = callback; }

	// Retry operations with backoff
	template<typename Func>
	BluezResult<void> retryOperation(Func operation, const RetryPolicy& policy = RetryPolicy{});

	// Get connected devices
	BluezResult<std::vector<DeviceInfo>> getConnectedDevices();

	// BLE advertising management
	bool isAdvertising() const;

	// Async advertising with retry support
	void setAdvertisingAsync(bool enabled, std::function<void(BluezResult<void>)> callback = nullptr);

private:
	BluezAdapter() = default;
	~BluezAdapter();
	static BluezAdapter& activeAdapterStorage() noexcept;

	friend BluezAdapter& getActiveBluezAdapter() noexcept;
	friend BluezAdapter* getActiveBluezAdapterPtr() noexcept;

	// D-Bus property operations with error handling
	BluezResult<void> setAdapterProperty(const std::string& property, DBusVariantRef value);
	BluezResult<DBusVariantRef> getAdapterProperty(const std::string& property);

	// Object Manager operations
	BluezResult<void> setupObjectManager();
	void enumerateAdapters();

	// Non-blocking retry operations with GLib timeouts
	BluezResult<void> retryOperationWithTimeout(std::function<BluezResult<void>()> operation, const RetryPolicy& policy);

	// D-Bus signal handling
	void handlePropertiesChanged(std::string_view objectPath, DBusVariantRef parameters);
	void handleInterfacesAdded(DBusVariantRef parameters);
	void handleInterfacesRemoved(DBusVariantRef parameters);
	void handleNameOwnerChanged(DBusVariantRef parameters);

	// Internal connection tracking
	void handleDeviceConnected(const std::string& devicePath);
	void handleDeviceDisconnected(const std::string& devicePath);

	// Member variables
	std::string adapterPath;
	std::string serviceNameContext_ = "bzperi";
	const Server* serverContext_ = nullptr;
	DBusConnectionRef dbusConnection;
	DBusObjectManagerRef objectManager;
	bool initialized = false;
	std::atomic<int> activeConnections{0};

	// Available adapters and capabilities
	std::vector<AdapterInfo> availableAdapters;
	BluezCapabilities capabilities;

	// BLE advertising
	std::unique_ptr<BluezAdvertisement> advertisement;
	std::unordered_map<std::string, bool> supportedInterfaces;

	// Signal subscription IDs
	guint propertiesChangedSubscription = 0;
	guint interfacesAddedSubscription = 0;
	guint interfacesRemovedSubscription = 0;
	guint nameOwnerChangedSubscription = 0;

	// Connected devices tracking
	std::unordered_map<std::string, DeviceInfo> connectedDevices;
	mutable std::mutex connectedDevicesMutex_;

	// Configuration
	RetryPolicy defaultRetryPolicy;
	TimeoutConfig timeoutConfig;

	// Advertising retry state
	struct AdvertisingRetryState {
		bool enabled;
		int currentAttempt;
		RetryPolicy policy;
		guint timeoutId;
		std::function<void(BluezResult<void>)> completionCallback;
	};
	std::unique_ptr<AdvertisingRetryState> activeAdvertisingRetry;

	// Async retry state
	struct RetryState {
		BluezAdapter* owner = nullptr;
		std::function<BluezResult<void>()> operation;
		RetryPolicy policy;
		int currentAttempt;
		guint timeoutId;
		std::function<void(BluezResult<void>)> completionCallback;
	};
	std::vector<std::unique_ptr<RetryState>> activeRetries;

	// Static callback for g_timeout_add
	static gboolean onRetryTimeout(gpointer user_data);
	static gboolean onAdvertisingRetryTimeout(gpointer user_data);
	static gboolean onReconnectTimeout(gpointer user_data);
	static gboolean onDelayedReconnectTimeout(gpointer user_data);
	void scheduleAsyncRetry(std::function<BluezResult<void>()> operation,
	                       const RetryPolicy& policy,
	                       std::function<void(BluezResult<void>)> completionCallback = nullptr);
	void scheduleAdvertisingRetry(bool enabled, const RetryPolicy& policy, std::function<void(BluezResult<void>)> callback = nullptr);
	void clearReconnectTimers();
	void scheduleReconnectAttempt(unsigned int delaySeconds, bool delayedRetry);

	// Callback for connection events
	ConnectionCallback connectionCallback;

	// Flag to cancel pending reconnect timers on shutdown
	std::atomic<bool> reconnectCancelled_{false};
	guint reconnectTimerId_ = 0;
	guint delayedReconnectTimerId_ = 0;
};

[[nodiscard]] BluezAdapter& getActiveBluezAdapter() noexcept;
[[nodiscard]] BluezAdapter* getActiveBluezAdapterPtr() noexcept;

} // namespace bzp
