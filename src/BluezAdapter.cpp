// Copyright (c) 2025 JaeYoung Hwang (BzPeri Project)
// Licensed under MIT License (see LICENSE file)
//
// Modern BlueZ adapter management for BzPeri

#include <bzp/BluezAdapter.h>
#include "BluezAdvertisingSupport.h"
#include "BluezAdvertisement.h"
#include <bzp/Logger.h>
#include <bzp/Server.h>
#include "StructuredLogger.h"
#include "GLibRAII.h"
#include <bzp/Utils.h>
#include <glib.h>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <functional>
#include <string_view>
#include <chrono>
#include <thread>

namespace bzp {

namespace {

std::string currentAdvertisementPath(const std::string& serviceName)
{
	if (!serviceName.empty())
	{
		// Convert dots in service name to slashes for valid D-Bus object path
		// e.g., "bzperi.myapp" becomes "/com/bzperi/myapp/advertisement0"
		std::string pathServiceName = serviceName;
		std::replace(pathServiceName.begin(), pathServiceName.end(), '.', '/');
		return std::string("/com/") + pathServiceName + "/advertisement0";
	}

	return "/com/bzperi/advertisement0";
}

std::optional<std::pair<int, int>> parseBluezMajorMinor(const std::string& version)
{
	size_t index = 0;
	while (index < version.size() && !std::isdigit(static_cast<unsigned char>(version[index])))
	{
		++index;
	}
	if (index >= version.size())
	{
		return std::nullopt;
	}

	size_t next = index;
	while (next < version.size() && std::isdigit(static_cast<unsigned char>(version[next])))
	{
		++next;
	}
	int major = std::stoi(version.substr(index, next - index));

	if (next >= version.size() || version[next] != '.')
	{
		return std::make_pair(major, 0);
	}

	index = next + 1;
	next = index;
	while (next < version.size() && std::isdigit(static_cast<unsigned char>(version[next])))
	{
		++next;
	}
	if (index == next)
	{
		return std::make_pair(major, 0);
	}

	return std::make_pair(major, std::stoi(version.substr(index, next - index)));
}

std::string detectBluezVersionString()
{
	const std::pair<const char*, const char*> candidates[] = {
		{"bluetoothd", "-v"},
		{"bluetoothctl", "-v"},
	};

	for (const auto& [binary, flag] : candidates)
	{
		gchar* stdoutText = nullptr;
		gchar* stderrText = nullptr;
		GError* error = nullptr;
		gint exitStatus = 0;
		gchar* argv[] = {g_strdup(binary), g_strdup(flag), nullptr};

		gboolean ok = g_spawn_sync(
			nullptr,
			argv,
			nullptr,
			G_SPAWN_SEARCH_PATH,
			nullptr,
			nullptr,
			&stdoutText,
			&stderrText,
			&exitStatus,
			&error);

		g_free(argv[0]);
		g_free(argv[1]);

		if (!ok)
		{
			if (error) g_error_free(error);
			g_free(stdoutText);
			g_free(stderrText);
			continue;
		}

		std::string output;
		if (stdoutText && *stdoutText)
		{
			output = stdoutText;
		}
		else if (stderrText && *stderrText)
		{
			output = stderrText;
		}

		g_free(stdoutText);
		g_free(stderrText);
		if (error) g_error_free(error);

		if (exitStatus != 0)
		{
			continue;
		}

		size_t begin = output.find_first_of("0123456789");
		if (begin == std::string::npos)
		{
			continue;
		}

		size_t end = begin;
		while (end < output.size() &&
			   (std::isdigit(static_cast<unsigned char>(output[end])) || output[end] == '.'))
		{
			++end;
		}
		return output.substr(begin, end - begin);
	}

	return {};
}

std::string joinStrings(const std::vector<std::string>& values, std::string_view separator)
{
	std::string joined;
	for (size_t index = 0; index < values.size(); ++index)
	{
		if (index != 0)
		{
			joined += separator;
		}
		joined += values[index];
	}
	return joined;
}

void configureAdvertisementPayload(BluezAdvertisement& advertisement, const BluezCapabilities& capabilities, const Server* serverContext)
{
	std::vector<std::string> discoveredServiceUUIDs;
	if (serverContext != nullptr)
	{
		discoveredServiceUUIDs = detail::collectGattServiceUUIDs(*serverContext);
	}

	const auto selectedServiceUUIDs = detail::selectAdvertisementServiceUUIDs(discoveredServiceUUIDs, capabilities);
	const bool usingExtendedAdvertising = detail::canUseExtendedAdvertising(capabilities);
	const char* payloadMode = usingExtendedAdvertising ? "extended" : "legacy";

	if (discoveredServiceUUIDs.empty())
	{
		Logger::info(SSTR << "Advertising payload mode: " << payloadMode
			<< " (MaxAdvLen=" << capabilities.maxAdvertisingDataLength << ", no service UUIDs discovered)");
	}
	else
	{
		Logger::info(SSTR << "Advertising payload mode: " << payloadMode
			<< " (MaxAdvLen=" << capabilities.maxAdvertisingDataLength
			<< ", discovered=" << discoveredServiceUUIDs.size()
			<< ", selected=" << selectedServiceUUIDs.size()
			<< ", uuids=" << joinStrings(selectedServiceUUIDs, ", ") << ")");
	}

	if (!discoveredServiceUUIDs.empty() && !usingExtendedAdvertising &&
		selectedServiceUUIDs.size() < discoveredServiceUUIDs.size())
	{
		Logger::info(SSTR << "Legacy advertising budget active (" << capabilities.maxAdvertisingDataLength
			<< " bytes); advertising " << selectedServiceUUIDs.size() << " of "
			<< discoveredServiceUUIDs.size() << " service UUIDs");
	}
	else if (!selectedServiceUUIDs.empty())
	{
		Logger::debug(SSTR << "Advertising " << selectedServiceUUIDs.size()
			<< " service UUIDs" << (usingExtendedAdvertising ? " with extended payload" : " with legacy payload"));
	}

	advertisement.setServiceUUIDs(selectedServiceUUIDs);
	advertisement.setAdvertisementType("peripheral");
	advertisement.setIncludeTxPower(false);
}

GMainContext* currentOrDefaultMainContext() noexcept
{
	if (GMainContext* threadDefault = g_main_context_get_thread_default(); threadDefault != nullptr)
	{
		return threadDefault;
	}

	return g_main_context_default();
}

guint attachTimeoutSource(guint intervalMS, GSourceFunc callback, gpointer userData)
{
	GSource* source = g_timeout_source_new(intervalMS);
	g_source_set_callback(source, callback, userData, nullptr);
	const guint sourceId = g_source_attach(source, currentOrDefaultMainContext());
	g_source_unref(source);
	return sourceId;
}

guint attachTimeoutSecondsSource(guint intervalSeconds, GSourceFunc callback, gpointer userData)
{
	GSource* source = g_timeout_source_new_seconds(intervalSeconds);
	g_source_set_callback(source, callback, userData, nullptr);
	const guint sourceId = g_source_attach(source, currentOrDefaultMainContext());
	g_source_unref(source);
	return sourceId;
}

} // namespace

void BluezAdapter::setServiceNameContext(std::string serviceName)
{
	serviceNameContext_ = serviceName.empty() ? "bzperi" : std::move(serviceName);
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
	dbusConnection = DBusConnectionRef(g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error));
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
		g_object_unref(dbusConnection.get());
		dbusConnection = DBusConnectionRef();
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
		dbusConnection.get(),
		"org.bluez",
		"org.freedesktop.DBus.Properties",
		"PropertiesChanged",
		nullptr,
		nullptr,  // Listen to all interfaces
		G_DBUS_SIGNAL_FLAGS_NONE,
		[](GDBusConnection*, const gchar*, const gchar* object_path, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
			static_cast<BluezAdapter*>(user_data)->handlePropertiesChanged(object_path != nullptr ? object_path : "", DBusVariantRef(parameters));
		},
		this,
		nullptr);

	interfacesAddedSubscription = g_dbus_connection_signal_subscribe(
		dbusConnection.get(),
		"org.bluez",
		"org.freedesktop.DBus.ObjectManager",
		"InterfacesAdded",
		nullptr,
		nullptr,
		G_DBUS_SIGNAL_FLAGS_NONE,
		[](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
			static_cast<BluezAdapter*>(user_data)->handleInterfacesAdded(DBusVariantRef(parameters));
		},
		this,
		nullptr);

	interfacesRemovedSubscription = g_dbus_connection_signal_subscribe(
		dbusConnection.get(),
		"org.bluez",
		"org.freedesktop.DBus.ObjectManager",
		"InterfacesRemoved",
		nullptr,
		nullptr,
		G_DBUS_SIGNAL_FLAGS_NONE,
		[](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
			static_cast<BluezAdapter*>(user_data)->handleInterfacesRemoved(DBusVariantRef(parameters));
		},
		this,
		nullptr);

	// Monitor BlueZ service availability
	nameOwnerChangedSubscription = g_dbus_connection_signal_subscribe(
		dbusConnection.get(),
		"org.freedesktop.DBus",
		"org.freedesktop.DBus",
		"NameOwnerChanged",
		nullptr,
		"org.bluez",
		G_DBUS_SIGNAL_FLAGS_NONE,
		[](GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters, gpointer user_data) {
			static_cast<BluezAdapter*>(user_data)->handleNameOwnerChanged(DBusVariantRef(parameters));
		},
		this,
		nullptr);

	// Detect BlueZ capabilities
	auto capabilitiesResult = detectCapabilities();
	if (capabilitiesResult.isSuccess())
	{
		capabilities = capabilitiesResult.value();
		Logger::info(SSTR << "BlueZ capabilities detected - LE Advertising: "
					 << (capabilities.hasLEAdvertisingManager ? "Yes" : "No")
					 << ", GATT Manager: " << (capabilities.hasGattManager ? "Yes" : "No")
					 << ", MaxAdvLen: " << capabilities.maxAdvertisingDataLength
					 << ", Secondary PHYs: "
					 << (capabilities.supportedSecondaryChannels.empty()
					 	? "none"
					 	: joinStrings(capabilities.supportedSecondaryChannels, ",")));
	}

	initialized = true;
	bluezLogger.log().op("Initialize").path(adapterPath).result("Success").info();
	return BluezResult<void>();
}

// Shutdown and cleanup
void BluezAdapter::shutdown()
{
	// Cancel any pending reconnect timers immediately
	reconnectCancelled_.store(true);
	clearReconnectTimers();

	if (!initialized)
		return;

	// Unsubscribe from D-Bus signals
	if (dbusConnection)
	{
		if (propertiesChangedSubscription > 0)
		{
			g_dbus_connection_signal_unsubscribe(dbusConnection.get(), propertiesChangedSubscription);
			propertiesChangedSubscription = 0;
		}
		if (interfacesAddedSubscription > 0)
		{
			g_dbus_connection_signal_unsubscribe(dbusConnection.get(), interfacesAddedSubscription);
			interfacesAddedSubscription = 0;
		}
		if (interfacesRemovedSubscription > 0)
		{
			g_dbus_connection_signal_unsubscribe(dbusConnection.get(), interfacesRemovedSubscription);
			interfacesRemovedSubscription = 0;
		}
		if (nameOwnerChangedSubscription > 0)
		{
			g_dbus_connection_signal_unsubscribe(dbusConnection.get(), nameOwnerChangedSubscription);
			nameOwnerChangedSubscription = 0;
		}
	}

	// Clean up object manager
	if (objectManager)
	{
		g_object_unref(objectManager.get());
		objectManager = DBusObjectManagerRef();
	}

	// Clean up D-Bus connection
	if (dbusConnection)
	{
		g_object_unref(dbusConnection.get());
		dbusConnection = DBusConnectionRef();
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
	serverContext_ = nullptr;

	Logger::debug("BluezAdapter shutdown complete");
}

// Setup ObjectManager for adapter discovery
BluezResult<void> BluezAdapter::setupObjectManager()
{
	GError* error = nullptr;
	objectManager = DBusObjectManagerRef(g_dbus_object_manager_client_new_sync(
		dbusConnection.get(),
		G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,
		"org.bluez",
		"/",
		nullptr,  // get_proxy_type_func
		nullptr,  // get_proxy_type_user_data
		nullptr,  // get_proxy_type_destroy_notify
		nullptr,  // cancellable
		&error));

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
	GList* objects = g_dbus_object_manager_get_objects(objectManager.get());

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

			if (address && g_variant_is_of_type(address, G_VARIANT_TYPE_STRING)) { info.address = g_variant_get_string(address, nullptr); g_variant_unref(address); } else if (address) { g_variant_unref(address); }
			if (name && g_variant_is_of_type(name, G_VARIANT_TYPE_STRING)) { info.name = g_variant_get_string(name, nullptr); g_variant_unref(name); } else if (name) { g_variant_unref(name); }
			if (alias && g_variant_is_of_type(alias, G_VARIANT_TYPE_STRING)) { info.alias = g_variant_get_string(alias, nullptr); g_variant_unref(alias); } else if (alias) { g_variant_unref(alias); }
			if (powered && g_variant_is_of_type(powered, G_VARIANT_TYPE_BOOLEAN)) { info.powered = g_variant_get_boolean(powered); g_variant_unref(powered); } else if (powered) { g_variant_unref(powered); }
			if (discoverable && g_variant_is_of_type(discoverable, G_VARIANT_TYPE_BOOLEAN)) { info.discoverable = g_variant_get_boolean(discoverable); g_variant_unref(discoverable); } else if (discoverable) { g_variant_unref(discoverable); }
			if (connectable && g_variant_is_of_type(connectable, G_VARIANT_TYPE_BOOLEAN)) { info.connectable = g_variant_get_boolean(connectable); g_variant_unref(connectable); } else if (connectable) { g_variant_unref(connectable); }
			if (pairable && g_variant_is_of_type(pairable, G_VARIANT_TYPE_BOOLEAN)) { info.pairable = g_variant_get_boolean(pairable); g_variant_unref(pairable); } else if (pairable) { g_variant_unref(pairable); }

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
BluezResult<void> BluezAdapter::setAdapterProperty(const std::string& property, DBusVariantRef value)
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
			dbusConnection.get(),
			"org.bluez",
			adapterPath.c_str(),
			"org.freedesktop.DBus.Properties",
			"Set",
			g_variant_new("(ssv)", "org.bluez.Adapter1", property.c_str(), value.get()),
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
BluezResult<DBusVariantRef> BluezAdapter::getAdapterProperty(const std::string& property)
{
	if (!initialized || adapterPath.empty())
	{
		return BluezResult<DBusVariantRef>(BluezError::NotReady, "BluezAdapter not initialized");
	}

	GError* error = nullptr;
	GVariant* result = g_dbus_connection_call_sync(
		dbusConnection.get(),
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
		const BluezError mappedError = mapDBusErrorName(error && error->message ? error->message : "");
		const std::string message = error && error->message ? error->message : "Unknown D-Bus property error";
		if (error) g_error_free(error);
		return BluezResult<DBusVariantRef>(mappedError, message);
	}

	GVariant* value = nullptr;
	g_variant_get(result, "(v)", &value);
	g_variant_unref(result);

	return BluezResult<DBusVariantRef>(DBusVariantRef(value));
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

	LOG_DEBUG_STREAM(SSTR << "Operation failed, scheduling async retry: " << result.errorMessage());

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
	static constexpr size_t kMaxActiveRetries = 16;
	if (activeRetries.size() >= kMaxActiveRetries) {
		LOG_WARN_STREAM(SSTR << "Max active retries (" << kMaxActiveRetries << ") reached, dropping operation");
		if (completionCallback) {
			completionCallback(BluezResult<void>(BluezError::Failed, "Too many concurrent retries"));
		}
		return;
	}
	auto retryState = std::make_unique<RetryState>();
	retryState->owner = this;
	retryState->operation = operation;
	retryState->policy = policy;
	retryState->currentAttempt = 1;
	retryState->completionCallback = completionCallback;

	// Schedule first retry
	int delayMs = policy.getDelayMs(1);
	LOG_DEBUG_STREAM(SSTR << "Scheduling async retry in " << delayMs << "ms (attempt 1/" << policy.maxAttempts << ")");

	retryState->timeoutId = attachTimeoutSource(delayMs, onRetryTimeout, retryState.get());
	activeRetries.push_back(std::move(retryState));
}

// Static callback for GLib timeout
gboolean BluezAdapter::onRetryTimeout(gpointer user_data)
{
	RetryState* state = static_cast<RetryState*>(user_data);
	if (state == nullptr || state->owner == nullptr)
	{
		return G_SOURCE_REMOVE;
	}
	BluezAdapter& adapter = *state->owner;

	// Execute the retry operation
	auto result = state->operation();

    // Use BluezError-based retryability check (not GError*)
    if (result.isSuccess() || !::bzp::isRetryableError(result.error()) || state->currentAttempt >= state->policy.maxAttempts)
	{
		// Operation succeeded or max attempts reached
		LOG_DEBUG_STREAM(SSTR << "Async retry " << (result.isSuccess() ? "succeeded" : "exhausted")
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
	LOG_DEBUG_STREAM(SSTR << "Async retry failed, scheduling next attempt in " << delayMs
	             << "ms (attempt " << state->currentAttempt << "/" << state->policy.maxAttempts << ")");

	state->timeoutId = attachTimeoutSource(delayMs, onRetryTimeout, state);
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

	activeAdvertisingRetry->timeoutId = attachTimeoutSource(delayMs, onAdvertisingRetryTimeout, this);
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
			adapter->advertisement = std::make_unique<BluezAdvertisement>(currentAdvertisementPath(adapter->serviceNameContext_));
		}
		configureAdvertisementPayload(*adapter->advertisement, adapter->capabilities, adapter->serverContext_);

			adapter->advertisement->registerAdvertisementAsync(adapter->dbusConnection.get(), adapter->adapterPath,
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
						retryState->timeoutId = attachTimeoutSource(delayMs, onAdvertisingRetryTimeout, adapter);
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
	return setAdapterProperty("Powered", DBusVariantRef(g_variant_new_boolean(enabled)));
}

BluezResult<void> BluezAdapter::setDiscoverable(bool enabled, uint16_t timeout)
{
	auto result = setAdapterProperty("Discoverable", DBusVariantRef(g_variant_new_boolean(enabled)));
	if (result.hasError()) return result;

	if (enabled && timeout > 0)
	{
		return setAdapterProperty("DiscoverableTimeout", DBusVariantRef(g_variant_new_uint32(timeout)));
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
	return setAdapterProperty("Pairable", DBusVariantRef(g_variant_new_boolean(enabled)));
}

BluezResult<void> BluezAdapter::setName(const std::string& name, const std::string& shortName)
{
	auto result = setAdapterProperty("Alias", DBusVariantRef(g_variant_new_string(name.c_str())));
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
		GDBusObject* adapterObject = g_dbus_object_manager_get_object(objectManager.get(), adapterPath.c_str());
		if (adapterObject)
		{
			advInterface = g_dbus_object_get_interface(adapterObject, "org.bluez.LEAdvertisingManager1");
			caps.hasLEAdvertisingManager = (advInterface != nullptr);
			if (advInterface)
			{
				GDBusProxy* proxy = G_DBUS_PROXY(advInterface);

				if (GVariant* supportedCapabilities = g_dbus_proxy_get_cached_property(proxy, "SupportedCapabilities"))
				{
					guint8 maxAdvLen = static_cast<guint8>(caps.maxAdvertisingDataLength);
					guint8 maxScanResponseLen = static_cast<guint8>(caps.maxScanResponseLength);
					g_variant_lookup(supportedCapabilities, "MaxAdvLen", "y", &maxAdvLen);
					g_variant_lookup(supportedCapabilities, "MaxScnRspLen", "y", &maxScanResponseLen);
					caps.maxAdvertisingDataLength = maxAdvLen;
					caps.maxScanResponseLength = maxScanResponseLen;
					g_variant_unref(supportedCapabilities);
				}

				if (GVariant* supportedSecondaryChannels = g_dbus_proxy_get_cached_property(proxy, "SupportedSecondaryChannels"))
				{
					gsize count = 0;
					gchar** values = g_variant_dup_strv(supportedSecondaryChannels, &count);
					for (gsize index = 0; index < count; ++index)
					{
						caps.supportedSecondaryChannels.emplace_back(values[index]);
					}
					g_strfreev(values);
					g_variant_unref(supportedSecondaryChannels);
				}

				g_object_unref(advInterface);
			}

			GDBusInterface* gattInterface = g_dbus_object_get_interface(adapterObject, "org.bluez.GattManager1");
			caps.hasGattManager = (gattInterface != nullptr);
			if (gattInterface) g_object_unref(gattInterface);

			g_object_unref(adapterObject);
		}
	}

	// Store interface support for quick lookup
	caps.bluezVersion = detectBluezVersionString();
	if (!caps.bluezVersion.empty())
	{
		if (auto version = parseBluezMajorMinor(caps.bluezVersion))
		{
			const auto [major, minor] = *version;
			caps.hasAcquireWrite = major > 5 || (major == 5 && minor >= 68);
			caps.hasAcquireNotify = major > 5 || (major == 5 && minor >= 68);
			caps.hasExtendedAdvertising = major > 5 || (major == 5 && minor >= 77);
		}
	}
	caps.hasExtendedAdvertising = caps.hasExtendedAdvertising || detail::canUseExtendedAdvertising(caps);

	supportedInterfaces["org.bluez.LEAdvertisingManager1"] = caps.hasLEAdvertisingManager;
	supportedInterfaces["org.bluez.GattManager1"] = caps.hasGattManager;
	supportedInterfaces["org.bluez.GattCharacteristic1.AcquireWrite"] = caps.hasAcquireWrite;
	supportedInterfaces["org.bluez.GattCharacteristic1.AcquireNotify"] = caps.hasAcquireNotify;
	supportedInterfaces["org.bluez.LEAdvertisingManager1.ExtendedAdvertising"] = detail::canUseExtendedAdvertising(caps);

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
	std::lock_guard<std::mutex> lock(connectedDevicesMutex_);
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
	bool shouldNotify = false;
	ConnectionCallback callback;
	{
		std::lock_guard<std::mutex> lock(connectedDevicesMutex_);
		auto it = connectedDevices.find(devicePath);
		if (it == connectedDevices.end())
		{
			// New connection
			DeviceInfo info;
			info.path = devicePath;
			info.connected = true;
			connectedDevices[devicePath] = info;

			int newCount = activeConnections.fetch_add(1) + 1;
			bluezLogger.logConnectionEvent(devicePath, true, newCount);

			shouldNotify = true;
			callback = connectionCallback;
		}
	}

	if (shouldNotify && callback)
	{
		callback(true, devicePath);
	}
}

void BluezAdapter::handleDeviceDisconnected(const std::string& devicePath)
{
	bool shouldNotify = false;
	ConnectionCallback callback;
	{
		std::lock_guard<std::mutex> lock(connectedDevicesMutex_);
		auto it = connectedDevices.find(devicePath);
		if (it != connectedDevices.end() && it->second.connected)
		{
			// Mark as disconnected
			it->second.connected = false;

			int newCount = activeConnections.fetch_sub(1) - 1;
			bluezLogger.logConnectionEvent(devicePath, false, newCount);

			shouldNotify = true;
			callback = connectionCallback;
		}
	}

	if (shouldNotify && callback)
	{
		callback(false, devicePath);
	}
}

// D-Bus signal handlers
void BluezAdapter::handlePropertiesChanged(std::string_view objectPath, DBusVariantRef parameters)
{
	if (!g_variant_is_of_type(parameters.get(), G_VARIANT_TYPE("(sa{sv}as)"))) {
		Logger::warn("onPropertiesChanged: unexpected parameter type, skipping");
		return;
	}

	const gchar* changedInterface = nullptr;
	GVariant* changedProperties = nullptr;
	GVariant* invalidatedProperties = nullptr;

	g_variant_get(parameters.get(), "(&s@a{sv}@as)", &changedInterface, &changedProperties, &invalidatedProperties);

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
					handleDeviceConnected(std::string(objectPath));
				}
				else
				{
					handleDeviceDisconnected(std::string(objectPath));
				}
			}
			g_variant_unref(value);
		}
	}

	g_variant_unref(changedProperties);
	g_variant_unref(invalidatedProperties);
}

void BluezAdapter::handleInterfacesAdded(DBusVariantRef parameters)
{
	if (!g_variant_is_of_type(parameters.get(), G_VARIANT_TYPE("(oa{sa{sv}})"))) {
		Logger::warn("onInterfacesAdded: unexpected parameter type, skipping");
		return;
	}

	const gchar* objectPath = nullptr;
	GVariant* interfaces = nullptr;

	g_variant_get(parameters.get(), "(&o@a{sa{sv}})", &objectPath, &interfaces);

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
				handleDeviceConnected(objectPath);
			}
			if (connected) g_variant_unref(connected);
		}
		else if (g_strcmp0(interfaceName, "org.bluez.Adapter1") == 0 &&
				 (!initialized || adapterPath.empty()))
		{
			bluezLogger.log().op("AdapterRecovery").path(objectPath).result("Available").extra("refreshing adapter selection").info();

			auto adaptersResult = discoverAdapters();
			if (adaptersResult.hasError())
			{
				bluezLogger.log().op("AdapterRecovery").path(objectPath).result("RefreshFailed").error(adaptersResult.errorMessage()).warn();
			}
			else
			{
				availableAdapters = adaptersResult.value();
				auto selectResult = selectAdapter(objectPath);
				if (selectResult.hasError())
				{
					bluezLogger.log().op("AdapterRecovery").path(objectPath).result("SelectFailed").error(selectResult.errorMessage()).warn();
				}
				else
				{
					auto capabilitiesResult = detectCapabilities();
					if (capabilitiesResult.isSuccess())
					{
						capabilities = capabilitiesResult.value();
					}

					initialized = true;
					reconnectCancelled_.store(false);
					bluezLogger.log().op("AdapterRecovery").path(objectPath).result("Recovered").info();

					if (advertisement)
					{
						setAdvertisingAsync(true, [](BluezResult<void> advResult) {
							if (advResult.isSuccess()) {
								bluezLogger.log().op("AdapterRecovery").prop("Advertising").result("ReRegistered").info();
							} else {
								bluezLogger.log().op("AdapterRecovery").prop("Advertising").result("ReRegisterFailed").error(advResult.errorMessage()).warn();
							}
						});
					}
				}
			}
		}
		g_variant_unref(properties);
	}

	g_variant_unref(interfaces);
}

void BluezAdapter::handleInterfacesRemoved(DBusVariantRef parameters)
{
	if (!g_variant_is_of_type(parameters.get(), G_VARIANT_TYPE("(oas)"))) {
		Logger::warn("onInterfacesRemoved: unexpected parameter type, skipping");
		return;
	}

	const gchar* objectPath = nullptr;
	GVariant* interfaces = nullptr;

	g_variant_get(parameters.get(), "(&o@as)", &objectPath, &interfaces);

	// Check if Device1 interface was removed
	GVariantIter iter;
	const gchar* interfaceName = nullptr;

	g_variant_iter_init(&iter, interfaces);
	while (g_variant_iter_next(&iter, "&s", &interfaceName))
	{
		if (g_strcmp0(interfaceName, "org.bluez.Device1") == 0)
		{
			// Device was removed, clean up from tracking
			bool wasConnected = false;
			{
				std::lock_guard<std::mutex> lock(connectedDevicesMutex_);
				auto it = connectedDevices.find(objectPath);
				if (it != connectedDevices.end())
				{
					wasConnected = it->second.connected;
					connectedDevices.erase(it);
					if (wasConnected) {
						activeConnections.fetch_sub(1);
					}
				}
			}
			if (wasConnected && connectionCallback)
			{
				bluezLogger.logConnectionEvent(objectPath, false, activeConnections.load());
				connectionCallback(false, objectPath);
			}
		}
		else if (g_strcmp0(interfaceName, "org.bluez.Adapter1") == 0
		         && adapterPath == objectPath)
		{
			bluezLogger.log().op("AdapterRecovery").path(objectPath).result("Removed").error("active adapter disappeared").error();
			initialized = false;
			adapterPath.clear();

			// Cancel any pending reconnect timers
			reconnectCancelled_.store(true);

			// Notify application via connection callback if registered
			if (connectionCallback) {
				connectionCallback(false, objectPath);
			}
		}
	}

	g_variant_unref(interfaces);
}

void BluezAdapter::handleNameOwnerChanged(DBusVariantRef parameters)
{
	if (!g_variant_is_of_type(parameters.get(), G_VARIANT_TYPE("(sss)"))) {
		Logger::warn("onNameOwnerChanged: unexpected parameter type, skipping");
		return;
	}

	const gchar* name = nullptr;
	const gchar* old_owner = nullptr;
	const gchar* new_owner = nullptr;
	g_variant_get(parameters.get(), "(&s&s&s)", &name, &old_owner, &new_owner);

	if (g_strcmp0(name, "org.bluez") == 0)
	{
		if (strlen(new_owner) == 0)
		{
			bluezLogger.log().op("BlueZService").result("Unavailable").extra("attempting reconnection").warn();
			reconnectCancelled_.store(false);
			clearReconnectTimers();
			scheduleReconnectAttempt(5, false);
		}
		else
		{
			bluezLogger.log().op("BlueZService").result("Available").info();
		}
	}
}

void BluezAdapter::clearReconnectTimers()
{
	if (reconnectTimerId_ > 0)
	{
		g_source_remove(reconnectTimerId_);
		reconnectTimerId_ = 0;
	}

	if (delayedReconnectTimerId_ > 0)
	{
		g_source_remove(delayedReconnectTimerId_);
		delayedReconnectTimerId_ = 0;
	}
}

void BluezAdapter::scheduleReconnectAttempt(unsigned int delaySeconds, bool delayedRetry)
{
	if (reconnectCancelled_.load())
	{
		return;
	}

	guint& timerId = delayedRetry ? delayedReconnectTimerId_ : reconnectTimerId_;
	if (timerId > 0)
	{
		g_source_remove(timerId);
		timerId = 0;
	}

	timerId = attachTimeoutSecondsSource(
		delaySeconds,
		delayedRetry ? onDelayedReconnectTimeout : onReconnectTimeout,
		this);
}

gboolean BluezAdapter::onReconnectTimeout(gpointer user_data)
{
	BluezAdapter* adapter = static_cast<BluezAdapter*>(user_data);
	adapter->reconnectTimerId_ = 0;

	if (adapter->reconnectCancelled_.load())
	{
		return G_SOURCE_REMOVE;
	}

	bluezLogger.log().op("Reconnect").result("Starting").extra("cleaning up stale connections").info();
	adapter->shutdown();
	adapter->reconnectCancelled_.store(false);

	auto result = adapter->initialize();
	if (result.isSuccess())
	{
		bluezLogger.log().op("Reconnect").result("Success").info();

		if (adapter->advertisement)
		{
			adapter->setAdvertisingAsync(true, [](BluezResult<void> advResult) {
				if (advResult.isSuccess()) {
					bluezLogger.log().op("Reconnect").prop("Advertising").result("ReRegistered").info();
				} else {
					bluezLogger.log().op("Reconnect").prop("Advertising").result("ReRegisterFailed").error(advResult.errorMessage()).warn();
				}
			});
		}

		return G_SOURCE_REMOVE;
	}

	bluezLogger.log().op("Reconnect").result("Failed").error(result.errorMessage()).error();
	adapter->reconnectCancelled_.store(false);
	adapter->scheduleReconnectAttempt(15, true);
	return G_SOURCE_REMOVE;
}

gboolean BluezAdapter::onDelayedReconnectTimeout(gpointer user_data)
{
	BluezAdapter* adapter = static_cast<BluezAdapter*>(user_data);
	adapter->delayedReconnectTimerId_ = 0;

	if (adapter->reconnectCancelled_.load())
	{
		return G_SOURCE_REMOVE;
	}

	auto result = adapter->initialize();
	if (result.isSuccess())
	{
		bluezLogger.log().op("Reconnect").result("DelayedSuccess").info();
	}
	else
	{
		bluezLogger.log().op("Reconnect").result("DelayedFailed").error(result.errorMessage()).error();
	}

	return G_SOURCE_REMOVE;
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
		g_main_context_iteration(currentOrDefaultMainContext(), FALSE); // Process pending events

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
		GVariant* poweredValue = poweredResult.hasError() ? nullptr : poweredResult.value().get();
		const bool isPowered = poweredValue != nullptr && g_variant_get_boolean(poweredValue);
		if (poweredValue != nullptr)
		{
			g_variant_unref(poweredValue);
		}
		if (poweredResult.hasError() || !isPowered)
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
			advertisement = std::make_unique<BluezAdvertisement>(currentAdvertisementPath(serviceNameContext_));
		}
		configureAdvertisementPayload(*advertisement, capabilities, serverContext_);

		// Use async registration with retry on failure
		advertisement->registerAdvertisementAsync(dbusConnection.get(), adapterPath,
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
			advertisement->unregisterAdvertisementAsync(dbusConnection.get(), adapterPath,
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
