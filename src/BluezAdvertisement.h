// Copyright (c) 2025 BzPeri Contributors
// Licensed under MIT License (see LICENSE file)
//
// BlueZ advertisement management for BLE advertising

#pragma once

#include "BluezTypes.h"
#include "StructuredLogger.h"
#include <gio/gio.h>
#include <string>
#include <vector>
#include <memory>

namespace bzp {

// BlueZ LEAdvertisement1 D-Bus interface implementation
// Manages BLE advertising to make the GATT server discoverable
class BluezAdvertisement {
public:
    BluezAdvertisement(const std::string& objectPath = "/org/bzperi/advertisement");
    ~BluezAdvertisement();

    // Configure advertisement properties
    // setLocalName removed - name will be included via Includes=["local-name"] and adapter Alias
    void setServiceUUIDs(const std::vector<std::string>& uuids);
    void setAdvertisementType(const std::string& type = "peripheral"); // "peripheral" or "broadcast"
    void setIncludeTxPower(bool include);

    // Register/unregister advertisement with BlueZ
    BluezResult<void> registerAdvertisement(GDBusConnection* connection, const std::string& adapterPath);
    BluezResult<void> unregisterAdvertisement(GDBusConnection* connection, const std::string& adapterPath);

    // Async registration with callback (prevents deadlock)
    using RegistrationCallback = std::function<void(BluezResult<void>)>;
    void registerAdvertisementAsync(GDBusConnection* connection, const std::string& adapterPath, RegistrationCallback callback = nullptr);
    void unregisterAdvertisementAsync(GDBusConnection* connection, const std::string& adapterPath, RegistrationCallback callback = nullptr);

    // D-Bus object management
    BluezResult<void> exportToDBus(GDBusConnection* connection);
    void unexportFromDBus();

    // Getters for D-Bus properties
    const std::string& getObjectPath() const { return objectPath_; }
    bool isRegistered() const { return registered_; }

    // D-Bus method handlers (must be public for vtable access)
    static void onMethodCall(GDBusConnection* connection,
                           const gchar* sender,
                           const gchar* object_path,
                           const gchar* interface_name,
                           const gchar* method_name,
                           GVariant* parameters,
                           GDBusMethodInvocation* invocation,
                           gpointer user_data);

    static GVariant* onGetProperty(GDBusConnection* connection,
                                  const gchar* sender,
                                  const gchar* object_path,
                                  const gchar* interface_name,
                                  const gchar* property_name,
                                  GError** error,
                                  gpointer user_data);

    static gboolean onSetProperty(GDBusConnection* connection,
                                 const gchar* sender,
                                 const gchar* object_path,
                                 const gchar* interface_name,
                                 const gchar* property_name,
                                 GVariant* value,
                                 GError** error,
                                 gpointer user_data);

private:
    // Async callback handlers for D-Bus registration
    static void onRegisterAdvertisementCallback(GObject* source_object, GAsyncResult* res, gpointer user_data);
    static void onUnregisterAdvertisementCallback(GObject* source_object, GAsyncResult* res, gpointer user_data);

    // Callback context structure
    struct CallbackContext {
        BluezAdvertisement* advertisement;
        RegistrationCallback callback;
        std::string operation;
    };

    // Property getters for D-Bus interface
    GVariant* getType() const;
    GVariant* getServiceUUIDs() const;
    // getLocalName removed - name included via Includes=["local-name"]
    GVariant* getIncludes() const;

    std::string objectPath_;
    // localName_ removed - name included via Includes=["local-name"]
    std::vector<std::string> serviceUUIDs_;
    std::string advertisementType_;
    bool includeTxPower_;
    bool registered_;
    bool exported_;

    GDBusConnection* connection_;
    guint registrationId_;

    // Keep introspection info alive for the lifetime of the exported object
    GDBusNodeInfo* introspection_data_ = nullptr;
    GDBusInterfaceInfo* interface_info_ = nullptr;

    static constexpr const char* ADVERTISEMENT_INTERFACE = "org.bluez.LEAdvertisement1";
    static constexpr const char* ADVERTISING_MANAGER_INTERFACE = "org.bluez.LEAdvertisingManager1";
};

} // namespace bzp
