// Copyright (c) 2025 JaeYoung Hwang (BzPeri Project)
// Licensed under MIT License (see LICENSE file)
//
// Modern BlueZ adapter management for BzPeri

#include <bzp/BluezAdapter.h>
#include "BluezAdvertisement.h"
#include <bzp/Globals.h>
#include <bzp/Server.h>
#include <bzp/Logger.h>
#include "StructuredLogger.h"
#include "GLibRAII.h"
#include <bzp/Utils.h>
#include <glib.h>
#include <cstring>
#include <algorithm>
#include <functional>
#include <chrono>
#include <thread>

namespace bzp {

namespace {

std::string currentAdvertisementPath()
{
	if (TheServer)
	{
		// Convert dots in service name to slashes for valid D-Bus object path
		// e.g., "bzperi.myapp" becomes "/com/bzperi/myapp/advertisement0"
		std::string serviceName = TheServer->getServiceName();
		std::replace(serviceName.begin(), serviceName.end(), '.', '/');
		return std::string("/com/") + serviceName + "/advertisement0";
	}

	return "/com/bzperi/advertisement0";
}

} // namespace

// Singleton instance
BluezAdapter& BluezAdapter::getInstance()
{
	static BluezAdapter instance;
	return instance;
}

BluezAdapter::~BluezAdapter()
{
	shutdown();
}

// Enhanced initialization with adapter discovery
BluezResult<void> BluezAdapter::initialize(const std::string& preferredAdapter)
{
	if (initialized)
	{
		Logger::debug("BluezAdapter already initialized");
		return BluezResult<void>();
	}

	// Initialize default configuration - fixed narrowing conversion
	defaultRetryPolicy = RetryPolicy{3, 1000, 5000, 2.0};
	timeoutConfig = TimeoutConfig{5000, 3000, 10000, 30000};

	// Get D-Bus connection
	GError* error = nullptr;
	dbusConnection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
	if (!dbusConnection)
	{
		std::string message = error ? error->message : "Unknown D-Bus connection error";
		Logger::error(SSTR << "Failed to get system D-Bus connection: " << message);
		if (error) g_error_free(error);
		return BluezResult<void>(BluezError::ConnectionFailed, message);
	}

	// Setup ObjectManager for adapter discovery
	auto setupResult = setupObjectManager();
	if (setupResult.hasError())
	{
		g_object_unref(dbusConnection);
		dbusConnection = nullptr;
		return setupResult;
	}

	// Discover available adapters
	auto adaptersResult = discoverAdapters();
	if (adaptersResult.hasError())
	{
		Logger::error("Failed to discover BlueZ adapters");
		shutdown();
		return BluezResult<void>(adaptersResult.error(), adaptersResult.errorMessage());
	}

	availableAdapters = adaptersResult.value();
	if (availableAdapters.empty())
	{
		Logger::error("No BlueZ adapters found");
		shutdown();
		return BluezResult<void>(BluezError::NotFound, "No BlueZ adapters available");
	}

	// Select adapter (preferred, powered, or first available)
	std::string selectedPath;
	if (!preferredAdapter.empty())
	{
		// Use preferred adapter if specified and available
		for (const auto& adapter : availableAdapters)
		{
			if (adapter.path == preferredAdapter || adapter.address == preferredAdapter ||
				adapter.path.find(preferredAdapter) != std::string::npos)
			{
				selectedPath = adapter.path;
				break;
			}
		}
		if (selectedPath.empty())
		{
			Logger::warn(SSTR << "Preferred adapter '" << preferredAdapter << "' not found, using default");
		}
	}

	if (selectedPath.empty())
	{
		// Find first powered adapter, or use first available
		for (const auto& adapter : availableAdapters)
		{
			if (adapter.powered)
			{
				selectedPath = adapter.path;
				break;
			}
		}
		if (selectedPath.empty())
		{
			selectedPath = availableAdapters[0].path;
		}
	}

	auto selectResult = selectAdapter(selectedPath);
	if (selectResult.hasError())
	{
		shutdown();
		return selectResult;
	}

	// Subscribe to D-Bus signals for connection tracking and adapter monitoring
	propertiesChangedSubscription = g_dbus_connection_signal_subscribe(
		dbusConnection,
		"org.bluez",
		"org.freedesktop.DBus.Properties",
		"PropertiesChanged",
		nullptr,
		nullptr,  // Listen to all interfaces
		G_DBUS_SIGNAL_FLAGS_NONE,
		onPropertiesChanged,
		this,
		nullptr);

	interfacesAddedSubscription = g_dbus_connection_signal_subscribe(
		dbusConnection,
		"org.bluez",
		"org.freedesktop.DBus.ObjectManager",
		"InterfacesAdded",
		nullptr,
		nullptr,
		G_DBUS_SIGNAL_FLAGS_NONE,
		onInterfacesAdded,
		this,
		nullptr);

	interfacesRemovedSubscription = g_dbus_connection_signal_subscribe(
		dbusConnection,
		"org.bluez",
		"org.freedesktop.DBus.ObjectManager",
		"InterfacesRemoved",
		nullptr,
		nullptr,
		G_DBUS_SIGNAL_FLAGS_NONE,
		onInterfacesRemoved,
		this,
		nullptr);

	// Monitor BlueZ service availability
	nameOwnerChangedSubscription = g_dbus_connection_signal_subscribe(
		dbusConnection,
		"org.freedesktop.DBus",
		"org.freedesktop.DBus",
		"NameOwnerChanged",
		nullptr,
		"org.bluez",
		G_DBUS_SIGNAL_FLAGS_NONE,
		onNameOwnerChanged,
		this,
		nullptr);

	// Detect BlueZ capabilities
	auto capabilitiesResult = detectCapabilities();
	if (capabilitiesResult.isSuccess())
	{
		capabilities = capabilitiesResult.value();
		Logger::info(SSTR << "BlueZ capabilities detected - LE Advertising: "
					 << (capabilities.hasLEAdvertisingManager ? "Yes" : "No")
					 << ", GATT Manager: " << (capabilities.hasGattManager ? "Yes" : "No"));
	}

	initialized = true;
	bluezLogger.log().op("Initialize").path(adapterPath).result("Success").info();
	return BluezResult<void>();
}

// Shutdown and cleanup
void BluezAdapter::shutdown()
{
	if (!initialized)
		return;

	// Unsubscribe from D-Bus signals
	if (dbusConnection)
	{
		if (propertiesChangedSubscription > 0)
		{
			g_dbus_connection_signal_unsubscribe(dbusConnection, propertiesChangedSubscription);
			propertiesChangedSubscription = 0;
		}
		if (interfacesAddedSubscription > 0)
		{
			g_dbus_connection_signal_unsubscribe(dbusConnection, interfacesAddedSubscription);
			interfacesAddedSubscription = 0;
		}
		if (interfacesRemovedSubscription > 0)
		{
			g_dbus_connection_signal_unsubscribe(dbusConnection, interfacesRemovedSubscription);
			interfacesRemovedSubscription = 0;
		}
		if (nameOwnerChangedSubscription > 0)
		{
			g_dbus_connection_signal_unsubscribe(dbusConnection, nameOwnerChangedSubscription);
			nameOwnerChangedSubscription = 0;
		}
	}

	// Clean up object manager
	if (objectManager)
	{
		g_object_unref(objectManager);
		objectManager = nullptr;
	}

	// Clean up D-Bus connection
	if (dbusConnection)
	{
		g_object_unref(dbusConnection);
		dbusConnection = nullptr;
	}

	// Cancel any active retries
	for (auto& retry : activeRetries)
	{
		if (retry->timeoutId > 0)
		{
			g_source_remove(retry->timeoutId);
		}
	}
	activeRetries.clear();

	// Cancel advertising retry
	if (activeAdvertisingRetry && activeAdvertisingRetry->timeoutId > 0)
	{
		g_source_remove(activeAdvertisingRetry->timeoutId);
	}
	activeAdvertisingRetry.reset();

	// Reset state
	initialized = false;
	adapterPath.clear();
	availableAdapters.clear();
	connectedDevices.clear();
	supportedInterfaces.clear();
	activeConnections.store(0);

	Logger::debug("BluezAdapter shutdown complete");
}

// Setup ObjectManager for adapter discovery
BluezResult<void> BluezAdapter::setupObjectManager()
{
	GError* error = nullptr;
	objectManager = g_dbus_object_manager_client_new_sync(
		dbusConnection,
		G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
		"org.bluez",
		"/",
		nullptr,  // get_proxy_type_func
		nullptr,  // get_proxy_type_user_data
		nullptr,  // get_proxy_type_destroy_notify
		nullptr,  // cancellable
		&error);

	if (!objectManager)
	{
		std::string message = error ? error->message : "Failed to create ObjectManager";
		Logger::error(SSTR << "Failed to create BlueZ ObjectManager: " << message);
		if (error) g_error_free(error);
		return BluezResult<void>(BluezError::Failed, message);
	}

	return BluezResult<void>();
}

// Discover available BlueZ adapters
BluezResult<std::vector<AdapterInfo>> BluezAdapter::discoverAdapters()
{
	if (!objectManager)
	{
		return BluezResult<std::vector<AdapterInfo>>(BluezError::NotReady, "ObjectManager not initialized");
	}

	std::vector<AdapterInfo> adapters;
	GList* objects = g_dbus_object_manager_get_objects(objectManager);

	for (GList* l = objects; l != nullptr; l = l->next)
	{
		GDBusObject* object = G_DBUS_OBJECT(l->data);
		const gchar* objectPath = g_dbus_object_get_object_path(object);

		// Look for Adapter1 interfaces
		GDBusInterface* adapterInterface = g_dbus_object_get_interface(object, "org.bluez.Adapter1");
		if (adapterInterface)
		{
			AdapterInfo info;
			info.path = objectPath;

			// Get adapter properties
			GDBusProxy* proxy = G_DBUS_PROXY(adapterInterface);
			GVariant* address = g_dbus_proxy_get_cached_property(proxy, "Address");
			GVariant* name = g_dbus_proxy_get_cached_property(proxy, "Name");
			GVariant* alias = g_dbus_proxy_get_cached_property(proxy, "Alias");
			GVariant* powered = g_dbus_proxy_get_cached_property(proxy, "Powered");
			GVariant* discoverable = g_dbus_proxy_get_cached_property(proxy, "Discoverable");
			GVariant* connectable = g_dbus_proxy_get_cached_property(proxy, "Connectable");
			GVariant* pairable = g_dbus_proxy_get_cached_property(proxy, "Pairable");

			if (address) { info.address = g_variant_get_string(address, nullptr); g_variant_unref(address); }
			if (name) { info.name = g_variant_get_string(name, nullptr); g_variant_unref(name); }
			if (alias) { info.alias = g_variant_get_string(alias, nullptr); g_variant_unref(alias); }
			if (powered) { info.powered = g_variant_get_boolean(powered); g_variant_unref(powered); }
			if (discoverable) { info.discoverable = g_variant_get_boolean(discoverable); g_variant_unref(discoverable); }
			if (connectable) { info.connectable = g_variant_get_boolean(connectable); g_variant_unref(connectable); }
			if (pairable) { info.pairable = g_variant_get_boolean(pairable); g_variant_unref(pairable); }

			adapters.push_back(info);
			Logger::debug(SSTR << "Found adapter: " << info.path << " (" << info.address << ") - Powered: " << info.powered);

			g_object_unref(adapterInterface);
		}
	}

	g_list_free_full(objects, g_object_unref);

	if (adapters.empty())
	{
		return BluezResult<std::vector<AdapterInfo>>(BluezError::NotFound, "No BlueZ adapters found");
	}

	return BluezResult<std::vector<AdapterInfo>>(std::move(adapters));
}

// Select specific adapter
BluezResult<void> BluezAdapter::selectAdapter(const std::string& adapterPath)
{
	// Validate adapter exists
	bool found = false;
	for (const auto& adapter : availableAdapters)
	{
		if (adapter.path == adapterPath)
		{
			found = true;
			break;
		}
	}

	if (!found)
	{
		return BluezResult<void>(BluezError::NotFound, "Adapter not found: " + adapterPath);
	}

	this->adapterPath = adapterPath;
	Logger::info(SSTR << "Selected adapter: " << adapterPath);
	return BluezResult<void>();
}

// Get current adapter information
BluezResult<AdapterInfo> BluezAdapter::getAdapterInfo() const
{
	for (const auto& adapter : availableAdapters)
	{
		if (adapter.path == adapterPath)
		{
			return BluezResult<AdapterInfo>(adapter);
		}
	}

	return BluezResult<AdapterInfo>(BluezError::NotFound, "Current adapter not found");
}

// Enhanced property setter with error handling and readonly check
BluezResult<void> BluezAdapter::setAdapterProperty(const std::string& property, GVariant* value)
{
	if (!initialized || adapterPath.empty())
	{
		return BluezResult<void>(BluezError::NotReady, "BluezAdapter not initialized");
	}

	// Check for readonly properties based on BlueZ Adapter1 interface documentation
	static const std::vector<std::string> readonlyProperties = {
		"Address",        // MAC address (readonly)
		"AddressType",    // Address type (readonly)
		"Name",           // Controller name (readonly) - use Alias for setting name
		"Class",          // Class of Device (readonly)
		"UUIDs",          // Service UUIDs (readonly)
		"Modalias",       // Device modalias (readonly)
		"Roles",          // Supported roles (readonly, experimental)
		"ExperimentalFeatures"  // Experimental features (readonly, experimental)
	};

	for (const auto& readonly : readonlyProperties)
	{
		if (property == readonly)
		{
			return BluezResult<void>(BluezError::NotSupported, "Property '" + property + "' is read-only");
		}
	}

	auto operation = [this, &property, value]() -> BluezResult<void>
	{
		GError* error = nullptr;
		GVariant* result = g_dbus_connection_call_sync(
			dbusConnection,
			"org.bluez",
			adapterPath.c_str(),
			"org.freedesktop.DBus.Properties",
			"Set",
			g_variant_new("(ssv)", "org.bluez.Adapter1", property.c_str(), value),
			nullptr,
			G_DBUS_CALL_FLAGS_NONE,
			timeoutConfig.propertyTimeoutMs,
			nullptr,
			&error);

		if (!result)
		{
			auto errorResult = BluezResult<void>::fromGError(error);
			if (error) g_error_free(error);
			return errorResult;
		}

		g_variant_unref(result);
		Logger::debug(SSTR << "Successfully set " << property);
		return BluezResult<void>();
	};

	return retryOperationWithTimeout(operation, defaultRetryPolicy);
}

// Get adapter property
BluezResult<GVariant*> BluezAdapter::getAdapterProperty(const std::string& property)
{
	if (!initialized || adapterPath.empty())
	{
		return BluezResult<GVariant*>(BluezError::NotReady, "BluezAdapter not initialized");
	}

	GError* error = nullptr;
	GVariant* result = g_dbus_connection_call_sync(
		dbusConnection,
		"org.bluez",
		adapterPath.c_str(),
		"org.freedesktop.DBus.Properties",
		"Get",
		g_variant_new("(ss)", "org.bluez.Adapter1", property.c_str()),
		G_VARIANT_TYPE("(v)"),
		G_DBUS_CALL_FLAGS_NONE,
		timeoutConfig.propertyTimeoutMs,
		nullptr,
		&error);

	if (!result)
	{
		auto errorResult = BluezResult<GVariant*>::fromGError(error);
		if (error) g_error_free(error);
		return errorResult;
	}

	GVariant* value = nullptr;
	g_variant_get(result, "(v)", &value);
	g_variant_unref(result);

	return BluezResult<GVariant*>(value);
}

// Non-blocking retry operations using GLib timeouts
BluezResult<void> BluezAdapter::retryOperationWithTimeout(std::function<BluezResult<void>()> operation, const RetryPolicy& policy)
{
	// Try once synchronously first
	auto result = operation();
    // Use BluezError-based retryability check (not GError*)
    if (result.isSuccess() || !::bzp::isRetryableError(result.error()))
	{
		return result;
	}

	Logger::debug(SSTR << "Operation failed, scheduling async retry: " << result.errorMessage());

	// Schedule non-blocking retry for non-critical operations
	scheduleAsyncRetry(operation, policy);

	// Return the initial failure result - async retry will happen in background
	// For critical operations that need synchronous result, caller should check return value
	return result;
}

// Template implementation for retry operations (legacy interface)
template<typename Func>
BluezResult<void> BluezAdapter::retryOperation(Func operation, const RetryPolicy& policy)
{
	return retryOperationWithTimeout(operation, policy);
}

// Non-blocking async retry implementation
void BluezAdapter::scheduleAsyncRetry(std::function<BluezResult<void>()> operation,
                                     const RetryPolicy& policy,
                                     std::function<void(BluezResult<void>)> completionCallback)
{
	auto retryState = std::make_unique<RetryState>();
	retryState->operation = operation;
	retryState->policy = policy;
	retryState->currentAttempt = 1;
	retryState->completionCallback = completionCallback;

	// Schedule first retry
	int delayMs = policy.getDelayMs(1);
	Logger::debug(SSTR << "Scheduling async retry in " << delayMs << "ms (attempt 1/" << policy.maxAttempts << ")");

	retryState->timeoutId = g_timeout_add(delayMs, onRetryTimeout, retryState.get());
	activeRetries.push_back(std::move(retryState));
}

// Static callback for GLib timeout
gboolean BluezAdapter::onRetryTimeout(gpointer user_data)
{
	RetryState* state = static_cast<RetryState*>(user_data);
	BluezAdapter& adapter = getInstance();

	// Execute the retry operation
	auto result = state->operation();

    // Use BluezError-based retryability check (not GError*)
    if (result.isSuccess() || !::bzp::isRetryableError(result.error()) || state->currentAttempt >= state->policy.maxAttempts)
	{
		// Operation succeeded or max attempts reached
		Logger::debug(SSTR << "Async retry " << (result.isSuccess() ? "succeeded" : "exhausted")
		             << " after " << state->currentAttempt << " attempts");

		// Call completion callback if provided
		if (state->completionCallback)
		{
			state->completionCallback(result);
		}

		// Remove from active retries list
		adapter.activeRetries.erase(
			std::remove_if(adapter.activeRetries.begin(), adapter.activeRetries.end(),
				[state](const std::unique_ptr<RetryState>& ptr) { return ptr.get() == state; }),
			adapter.activeRetries.end());

		return G_SOURCE_REMOVE; // Don't repeat
	}

	// Schedule next retry
	state->currentAttempt++;
	int delayMs = state->policy.getDelayMs(state->currentAttempt);
	Logger::debug(SSTR << "Async retry failed, scheduling next attempt in " << delayMs
	             << "ms (attempt " << state->currentAttempt << "/" << state->policy.maxAttempts << ")");

	state->timeoutId = g_timeout_add(delayMs, onRetryTimeout, state);
	return G_SOURCE_REMOVE; // Remove current timeout, new one scheduled
}

// Advertising retry methods
void BluezAdapter::scheduleAdvertisingRetry(bool enabled, const RetryPolicy& policy, std::function<void(BluezResult<void>)> callback)
{
	activeAdvertisingRetry = std::make_unique<AdvertisingRetryState>();
	activeAdvertisingRetry->enabled = enabled;
	activeAdvertisingRetry->policy = policy;
	activeAdvertisingRetry->currentAttempt = 1;
	activeAdvertisingRetry->completionCallback = callback;

	// Schedule first retry
	int delayMs = policy.getDelayMs(1);
	bluezLogger.log().op("ScheduleAdvertisingRetry").extra("attempt 1/" + std::to_string(policy.maxAttempts) + " in " + std::to_string(delayMs) + "ms").info();

	activeAdvertisingRetry->timeoutId = g_timeout_add(delayMs, onAdvertisingRetryTimeout, this);
}

gboolean BluezAdapter::onAdvertisingRetryTimeout(gpointer user_data)
{
	BluezAdapter* adapter = static_cast<BluezAdapter*>(user_data);

	// Robust null checks to prevent use-after-free
	if (!adapter || !adapter->activeAdvertisingRetry)
	{
		bluezLogger.log().op("AdvertisingRetryTimeout").result("Cancelled").extra("retry state cleared").info();
		return G_SOURCE_REMOVE;
	}

	auto& retryState = adapter->activeAdvertisingRetry;

	// Additional validation - ensure retry state is still valid
	if (!retryState)
	{
		bluezLogger.log().op("AdvertisingRetryTimeout").result("Cancelled").extra("retry state invalid").info();
		return G_SOURCE_REMOVE;
	}

	bool enabled = retryState->enabled;
	auto callback = retryState->completionCallback;

	// Try the advertising operation again
	if (enabled)
	{
		if (!adapter->advertisement)
		{
			adapter->advertisement = std::make_unique<BluezAdvertisement>(currentAdvertisementPath());

			// Configure advertisement with essential service UUIDs
			// Reduced to only 16-bit standard UUIDs to fit legacy 31-byte advertising budget
			// Full 128-bit custom UUIDs are still available via GATT but not advertised
			std::vector<std::string> serviceUUIDs = {
				"180A", // Device Information Service
				"180F", // Battery Service
				"1805"  // Current Time Service
				// Custom services (128-bit UUIDs) removed from advertisement to prevent
				// "org.bluez.Error.Failed: Failed to register advertisement" due to payload size
				// They remain available via GATT service discovery after connection
			};

			// LocalName removed - name will be included via Includes=["local-name"] and adapter Alias
			adapter->advertisement->setServiceUUIDs(serviceUUIDs);
			adapter->advertisement->setAdvertisementType("peripheral");

			// Disable tx-power to save more advertising bytes (reduces payload by ~3 bytes)
			// Can be re-enabled if needed: adapter->advertisement->setIncludeTxPower(true);
			adapter->advertisement->setIncludeTxPower(false);
		}

		adapter->advertisement->registerAdvertisementAsync(adapter->dbusConnection, adapter->adapterPath,
			[adapter, callback](BluezResult<void> result) {
				if (result.isSuccess())
				{
					bluezLogger.log().op("AdvertisingRetrySuccess").result("Success").info();

					// Safe cleanup: Cancel any pending timeout before resetting
					if (adapter->activeAdvertisingRetry && adapter->activeAdvertisingRetry->timeoutId > 0)
					{
						g_source_remove(adapter->activeAdvertisingRetry->timeoutId);
						adapter->activeAdvertisingRetry->timeoutId = 0;
					}
					adapter->activeAdvertisingRetry.reset();

					if (callback) callback(result);
				}
				else
				{
					// Check if we should continue retrying (with additional safety checks)
					auto& retryState = adapter->activeAdvertisingRetry;
					if (retryState && retryState->currentAttempt < retryState->policy.maxAttempts &&
						(::bzp::isRetryableError(result.error()) ||
						 result.error() == BluezError::Timeout ||
						 result.error() == BluezError::Failed))
					{
						// Schedule next retry
						retryState->currentAttempt++;
						int delayMs = retryState->policy.getDelayMs(retryState->currentAttempt);
						bluezLogger.log().op("AdvertisingRetryFailed").extra("attempt " + std::to_string(retryState->currentAttempt) + "/" + std::to_string(retryState->policy.maxAttempts) + " in " + std::to_string(delayMs) + "ms").warn();

						// Clear old timeout ID first if it exists
						if (retryState->timeoutId > 0)
						{
							g_source_remove(retryState->timeoutId);
						}
						retryState->timeoutId = g_timeout_add(delayMs, onAdvertisingRetryTimeout, adapter);
					}
					else
					{
						// Max attempts reached or non-retryable error
						bluezLogger.log().op("AdvertisingRetryExhausted").result("Failed").error(result.errorMessage()).error();

						// Safe cleanup: Cancel any pending timeout before resetting
						if (adapter->activeAdvertisingRetry && adapter->activeAdvertisingRetry->timeoutId > 0)
						{
							g_source_remove(adapter->activeAdvertisingRetry->timeoutId);
							adapter->activeAdvertisingRetry->timeoutId = 0;
						}
						adapter->activeAdvertisingRetry.reset();

						if (callback) callback(result);
					}
				}
			});
	}

	return G_SOURCE_REMOVE; // Remove current timeout
}

// Adapter configuration methods
BluezResult<void> BluezAdapter::setPowered(bool enabled)
{
	return setAdapterProperty("Powered", g_variant_new_boolean(enabled));
}

BluezResult<void> BluezAdapter::setDiscoverable(bool enabled, uint16_t timeout)
{
	auto result = setAdapterProperty("Discoverable", g_variant_new_boolean(enabled));
	if (result.hasError()) return result;

	if (enabled && timeout > 0)
	{
		return setAdapterProperty("DiscoverableTimeout", g_variant_new_uint32(timeout));
	}

	return BluezResult<void>();
}

BluezResult<void> BluezAdapter::setConnectable(bool enabled)
{
	// Modern BlueZ: Connectable property doesn't exist for LE adapters
	// BLE advertising handles connectable state automatically based on advertisement type
	bluezLogger.log().op("Set").prop("Connectable").result("NotSupported").extra("use LE advertising for connectable state").info();

	(void)enabled; // Suppress unused parameter warning
	return BluezResult<void>(BluezError::NotSupported, "Connectable property not supported on modern BlueZ LE adapters");
}

BluezResult<void> BluezAdapter::setBondable(bool enabled)
{
	return setAdapterProperty("Pairable", g_variant_new_boolean(enabled));
}

BluezResult<void> BluezAdapter::setName(const std::string& name, const std::string& shortName)
{
	auto result = setAdapterProperty("Alias", g_variant_new_string(name.c_str()));
	if (result.hasError()) return result;

	// Note: ShortName is not a standard BlueZ property, using Alias only
	(void)shortName;  // Mark unused parameter to avoid compiler warning
	return BluezResult<void>();
}

BluezResult<void> BluezAdapter::setLEEnabled(bool enabled)
{
	// LE is typically enabled by default in modern BlueZ, no specific property
	Logger::debug(SSTR << "LE " << (enabled ? "enabled" : "disabled") << " - handled automatically by BlueZ");
	return BluezResult<void>();
}


// Feature detection
BluezResult<BluezCapabilities> BluezAdapter::detectCapabilities()
{
	BluezCapabilities caps;

	if (!objectManager)
	{
		return BluezResult<BluezCapabilities>(BluezError::NotReady, "ObjectManager not initialized");
	}

	// Check if LEAdvertisingManager1 interface exists
	GDBusInterface* advInterface = nullptr;
	if (!adapterPath.empty())
	{
		GDBusObject* adapterObject = g_dbus_object_manager_get_object(objectManager, adapterPath.c_str());
		if (adapterObject)
		{
			advInterface = g_dbus_object_get_interface(adapterObject, "org.bluez.LEAdvertisingManager1");
			caps.hasLEAdvertisingManager = (advInterface != nullptr);
			if (advInterface) g_object_unref(advInterface);

			GDBusInterface* gattInterface = g_dbus_object_get_interface(adapterObject, "org.bluez.GattManager1");
			caps.hasGattManager = (gattInterface != nullptr);
			if (gattInterface) g_object_unref(gattInterface);

			g_object_unref(adapterObject);
		}
	}

	// Store interface support for quick lookup
	supportedInterfaces["org.bluez.LEAdvertisingManager1"] = caps.hasLEAdvertisingManager;
	supportedInterfaces["org.bluez.GattManager1"] = caps.hasGattManager;

	return BluezResult<BluezCapabilities>(caps);
}

// Check interface capability
bool BluezAdapter::hasCapability(const std::string& interface) const
{
	auto it = supportedInterfaces.find(interface);
	return it != supportedInterfaces.end() && it->second;
}

// Get connected devices
BluezResult<std::vector<DeviceInfo>> BluezAdapter::getConnectedDevices()
{
	std::vector<DeviceInfo> devices;
	for (const auto& pair : connectedDevices)
	{
		devices.push_back(pair.second);
	}
	return BluezResult<std::vector<DeviceInfo>>(std::move(devices));
}

// Device connection tracking
void BluezAdapter::handleDeviceConnected(const std::string& devicePath)
{
	auto it = connectedDevices.find(devicePath);
	if (it == connectedDevices.end())
	{
		// New connection
		DeviceInfo info;
		info.path = devicePath;
		info.connected = true;
		connectedDevices[devicePath] = info;

		int newCount = activeConnections.fetch_add(1) + 1;
		Logger::debug(SSTR << "Device connected: " << devicePath << " (total: " << newCount << ")");

		if (connectionCallback)
		{
			connectionCallback(true, devicePath);
		}
	}
}

void BluezAdapter::handleDeviceDisconnected(const std::string& devicePath)
{
	auto it = connectedDevices.find(devicePath);
	if (it != connectedDevices.end() && it->second.connected)
	{
		// Mark as disconnected
		it->second.connected = false;

		int newCount = activeConnections.fetch_sub(1) - 1;
		Logger::debug(SSTR << "Device disconnected: " << devicePath << " (total: " << newCount << ")");

		if (connectionCallback)
		{
			connectionCallback(false, devicePath);
		}
	}
}

// D-Bus signal handlers
void BluezAdapter::onPropertiesChanged(GDBusConnection* connection,
									  const gchar* sender_name,
									  const gchar* object_path,
									  const gchar* interface_name,
									  const gchar* signal_name,
									  GVariant* parameters,
									  gpointer user_data)
{
	BluezAdapter* adapter = static_cast<BluezAdapter*>(user_data);

	// Mark unused parameters to avoid compiler warnings
	(void)connection;
	(void)sender_name;
	(void)interface_name;
	(void)signal_name;

	const gchar* changedInterface = nullptr;
	GVariant* changedProperties = nullptr;
	GVariant* invalidatedProperties = nullptr;

	g_variant_get(parameters, "(&s@a{sv}@as)", &changedInterface, &changedProperties, &invalidatedProperties);

	// Handle Device1 connection state changes
	if (g_strcmp0(changedInterface, "org.bluez.Device1") == 0)
	{
		GVariantIter iter;
		const gchar* key = nullptr;
		GVariant* value = nullptr;

		g_variant_iter_init(&iter, changedProperties);
		while (g_variant_iter_next(&iter, "{&sv}", &key, &value))
		{
			if (g_strcmp0(key, "Connected") == 0)
			{
				gboolean connected = g_variant_get_boolean(value);
				if (connected)
				{
					adapter->handleDeviceConnected(object_path);
				}
				else
				{
					adapter->handleDeviceDisconnected(object_path);
				}
			}
			g_variant_unref(value);
		}
	}

	g_variant_unref(changedProperties);
	g_variant_unref(invalidatedProperties);
}

void BluezAdapter::onInterfacesAdded(GDBusConnection* connection,
									const gchar* sender_name,
									const gchar* object_path,
									const gchar* interface_name,
									const gchar* signal_name,
									GVariant* parameters,
									gpointer user_data)
{
	BluezAdapter* adapter = static_cast<BluezAdapter*>(user_data);

	// Mark unused parameters to avoid compiler warnings
	(void)connection;
	(void)sender_name;
	(void)object_path;
	(void)interface_name;
	(void)signal_name;

	const gchar* objectPath = nullptr;
	GVariant* interfaces = nullptr;

	g_variant_get(parameters, "(&o@a{sa{sv}})", &objectPath, &interfaces);

	// Check if this is a Device1 interface being added
	GVariantIter iter;
	const gchar* interfaceName = nullptr;
	GVariant* properties = nullptr;

	g_variant_iter_init(&iter, interfaces);
	while (g_variant_iter_next(&iter, "{&s@a{sv}}", &interfaceName, &properties))
	{
		if (g_strcmp0(interfaceName, "org.bluez.Device1") == 0)
		{
			// Device was added, check if it's connected
			GVariant* connected = g_variant_lookup_value(properties, "Connected", G_VARIANT_TYPE_BOOLEAN);
			if (connected && g_variant_get_boolean(connected))
			{
				adapter->handleDeviceConnected(objectPath);
			}
			if (connected) g_variant_unref(connected);
		}
		g_variant_unref(properties);
	}

	g_variant_unref(interfaces);
}

void BluezAdapter::onInterfacesRemoved(GDBusConnection* connection,
									  const gchar* sender_name,
									  const gchar* object_path,
									  const gchar* interface_name,
									  const gchar* signal_name,
									  GVariant* parameters,
									  gpointer user_data)
{
	BluezAdapter* adapter = static_cast<BluezAdapter*>(user_data);

	// Mark unused parameters to avoid compiler warnings
	(void)connection;
	(void)sender_name;
	(void)object_path;
	(void)interface_name;
	(void)signal_name;

	const gchar* objectPath = nullptr;
	GVariant* interfaces = nullptr;

	g_variant_get(parameters, "(&o@as)", &objectPath, &interfaces);

	// Check if Device1 interface was removed
	GVariantIter iter;
	const gchar* interfaceName = nullptr;

	g_variant_iter_init(&iter, interfaces);
	while (g_variant_iter_next(&iter, "&s", &interfaceName))
	{
		if (g_strcmp0(interfaceName, "org.bluez.Device1") == 0)
		{
			// Device was removed, clean up from tracking
			auto it = adapter->connectedDevices.find(objectPath);
			if (it != adapter->connectedDevices.end())
			{
				if (it->second.connected)
				{
					adapter->handleDeviceDisconnected(objectPath);
				}
				adapter->connectedDevices.erase(it);
			}
		}
	}

	g_variant_unref(interfaces);
}

void BluezAdapter::onNameOwnerChanged(GDBusConnection* connection,
									 const gchar* sender_name,
									 const gchar* object_path,
									 const gchar* interface_name,
									 const gchar* signal_name,
									 GVariant* parameters,
									 gpointer user_data)
{
	BluezAdapter* adapter = static_cast<BluezAdapter*>(user_data);

	// Mark unused parameters to avoid compiler warnings
	(void)connection;
	(void)sender_name;
	(void)object_path;
	(void)interface_name;
	(void)signal_name;

	const gchar* name = nullptr;
	const gchar* old_owner = nullptr;
	const gchar* new_owner = nullptr;
	g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);

	if (g_strcmp0(name, "org.bluez") == 0)
	{
		if (strlen(new_owner) == 0)
		{
			Logger::warn("BlueZ service disappeared - attempting reconnection");
			// Schedule reconnection on a timeout to avoid blocking
			g_timeout_add_seconds(5, [](gpointer user_data) -> gboolean {
				BluezAdapter* adapter = static_cast<BluezAdapter*>(user_data);

				// Proper cleanup before reinitializing
				Logger::info("Cleaning up stale BlueZ connections before reconnection");
				adapter->shutdown();  // This properly cleans up signals, objectManager, dbusConnection

				// Attempt to reinitialize
				auto result = adapter->initialize();
				if (result.isSuccess())
				{
					Logger::info("BlueZ reconnection successful");

					// Re-register advertising if it was previously active
					if (adapter->advertisement)
					{
						adapter->setAdvertisingAsync(true, [](BluezResult<void> advResult) {
							if (advResult.isSuccess()) {
								Logger::info("Advertising re-registered after BlueZ restart");
							} else {
								Logger::warn(SSTR << "Failed to re-register advertising: " << advResult.errorMessage());
							}
						});
					}
				}
				else
				{
					Logger::error(SSTR << "BlueZ reconnection failed: " << result.errorMessage());
					// Schedule another retry after longer delay
					g_timeout_add_seconds(15, [](gpointer user_data) -> gboolean {
						BluezAdapter* adapter = static_cast<BluezAdapter*>(user_data);
						auto result = adapter->initialize();
						if (result.isSuccess())
						{
							Logger::info("BlueZ delayed reconnection successful");
						}
						else
						{
							Logger::error(SSTR << "BlueZ delayed reconnection failed: " << result.errorMessage());
						}
						return G_SOURCE_REMOVE;
					}, adapter);
				}

				return G_SOURCE_REMOVE;  // Remove the timeout
			}, adapter);
		}
		else
		{
			Logger::info("BlueZ service available");
		}
	}
}

bool BluezAdapter::isRetryableError(const GError* error) const
{
	if (!error) return false;

	// Retryable D-Bus errors that might be temporary
	if (error->domain == G_DBUS_ERROR)
	{
		switch (error->code)
		{
			case G_DBUS_ERROR_TIMEOUT:
			case G_DBUS_ERROR_NO_REPLY:
			case G_DBUS_ERROR_DISCONNECTED:
			case G_DBUS_ERROR_SERVICE_UNKNOWN:
			case G_DBUS_ERROR_NAME_HAS_NO_OWNER:
				return true;
			default:
				return false;
		}
	}

	// Retryable I/O errors
	if (error->domain == G_IO_ERROR)
	{
		switch (error->code)
		{
			case G_IO_ERROR_BUSY:
			case G_IO_ERROR_WOULD_BLOCK:
			case G_IO_ERROR_TIMED_OUT:
			case G_IO_ERROR_CONNECTION_REFUSED:
			case G_IO_ERROR_NOT_CONNECTED:
				return true;
			default:
				return false;
		}
	}

	return false;
}

BluezResult<void> BluezAdapter::setAdvertising(bool enabled)
{
	// Legacy sync method - prefer setAdvertisingAsync for better reliability
	// This method now uses async internally but blocks until completion
	BluezResult<void> finalResult(BluezError::Timeout, "Operation timeout");
	bool operationComplete = false;

	setAdvertisingAsync(enabled, [&](BluezResult<void> result) {
		finalResult = result;
		operationComplete = true;
	});

	// Wait for async operation to complete (with timeout)
	auto startTime = std::chrono::steady_clock::now();
	while (!operationComplete) {
		g_main_context_iteration(nullptr, FALSE); // Process pending events

		auto elapsed = std::chrono::steady_clock::now() - startTime;
		if (elapsed > std::chrono::milliseconds(20000)) { // 20s timeout
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	return finalResult;
}

void BluezAdapter::setAdvertisingAsync(bool enabled, std::function<void(BluezResult<void>)> callback)
{
	if (!initialized || adapterPath.empty())
	{
		if (callback) {
			callback(BluezResult<void>(BluezError::NotReady, "BluezAdapter not initialized"));
		}
		return;
	}

	// Cancel any existing advertising retry
	if (activeAdvertisingRetry && activeAdvertisingRetry->timeoutId > 0)
	{
		g_source_remove(activeAdvertisingRetry->timeoutId);
		activeAdvertisingRetry.reset();
	}

	if (enabled)
	{
		// Check adapter is powered first
		auto poweredResult = getAdapterProperty("Powered");
		if (poweredResult.hasError() || !g_variant_get_boolean(poweredResult.value()))
		{
			// Try to power on the adapter first
			auto powerResult = setPowered(true);
			if (powerResult.hasError())
			{
				if (callback) {
					callback(BluezResult<void>(BluezError::NotReady, "Adapter not powered and cannot be powered on"));
				}
				return;
			}
		}

		// Create advertisement if it doesn't exist
		if (!advertisement)
		{
			advertisement = std::make_unique<BluezAdvertisement>(currentAdvertisementPath());

			// Configure advertisement with essential service UUIDs
			// Reduced to only 16-bit standard UUIDs to fit legacy 31-byte advertising budget
			// Full 128-bit custom UUIDs are still available via GATT but not advertised
			std::vector<std::string> serviceUUIDs = {
				"180A", // Device Information Service
				"180F", // Battery Service
				"1805"  // Current Time Service
				// Custom services (128-bit UUIDs) removed from advertisement to prevent
				// "org.bluez.Error.Failed: Failed to register advertisement" due to payload size
				// They remain available via GATT service discovery after connection
			};

			// LocalName removed - name will be included via Includes=["local-name"] and adapter Alias
			advertisement->setServiceUUIDs(serviceUUIDs);
			advertisement->setAdvertisementType("peripheral"); // Connectable

			// Disable tx-power to save more advertising bytes (reduces payload by ~3 bytes)
			// Can be re-enabled if needed: advertisement->setIncludeTxPower(true);
			advertisement->setIncludeTxPower(false);
		}

		// Use async registration with retry on failure
		advertisement->registerAdvertisementAsync(dbusConnection, adapterPath,
			[this, callback](BluezResult<void> result) {
				if (result.isSuccess())
				{
					bluezLogger.log().op("StartAdvertising").result("Success").info();
					if (callback) callback(result);
				}
				else
				{
					bluezLogger.log().op("StartAdvertising").result("Failed").error(result.errorMessage()).warn();

					// Check if error is retryable
					if (::bzp::isRetryableError(result.error()) ||
						result.error() == BluezError::Timeout ||
						result.error() == BluezError::Failed)
					{
						// Schedule retry with exponential backoff
						RetryPolicy advRetryPolicy{5, 2000, 30000, 2.0}; // More aggressive retry for advertising
						scheduleAdvertisingRetry(true, advRetryPolicy, callback);
					}
					else
					{
						if (callback) callback(result);
					}
				}
			});
	}
	else
	{
		// Stop advertising
		if (advertisement && advertisement->isRegistered())
		{
			advertisement->unregisterAdvertisementAsync(dbusConnection, adapterPath,
				[callback](BluezResult<void> result) {
					if (result.isSuccess()) {
						bluezLogger.log().op("StopAdvertising").result("Success").info();
					} else {
						bluezLogger.log().op("StopAdvertising").result("Failed").error(result.errorMessage()).warn();
					}
					if (callback) callback(result);
				});
		}
		else
		{
			bluezLogger.log().op("StopAdvertising").result("Success").extra("already stopped").info();
			if (callback) callback(BluezResult<void>());
		}
	}
}

bool BluezAdapter::isAdvertising() const
{
	return advertisement && advertisement->isRegistered();
}

} // namespace bzp
