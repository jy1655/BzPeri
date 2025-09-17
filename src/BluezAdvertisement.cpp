#include "BluezAdvertisement.h"
#include "GLibRAII.h"
#include "Logger.h"
#include <cstring>

namespace ggk {

// Global logger for advertisement operations
extern StructuredLogger bluezLogger;

// D-Bus introspection XML for LEAdvertisement1 interface
// Note: LocalName property removed - name is included via Includes=["local-name"]
static const gchar advertisement_introspection_xml[] =
    "<node>"
    "  <interface name='org.bluez.LEAdvertisement1'>"
    "    <method name='Release'>"
    "    </method>"
    "    <property name='Type' type='s' access='read'/>"
    "    <property name='ServiceUUIDs' type='as' access='read'/>"
    "    <property name='Includes' type='as' access='read'/>"
    "  </interface>"
    "</node>";

static const GDBusInterfaceVTable advertisement_interface_vtable = {
    BluezAdvertisement::onMethodCall,
    BluezAdvertisement::onGetProperty,
    BluezAdvertisement::onSetProperty,
    { nullptr }
};

BluezAdvertisement::BluezAdvertisement(const std::string& objectPath)
    : objectPath_(objectPath)
    , advertisementType_("peripheral")
    , includeTxPower_(true)
    , registered_(false)
    , exported_(false)
    , connection_(nullptr)
    , registrationId_(0)
{
    bluezLogger.log().op("Create").path(objectPath_).result("Success").info();
}

BluezAdvertisement::~BluezAdvertisement()
{
    unexportFromDBus();
}

// setLocalName method removed - name will be included via Includes=["local-name"] and adapter Alias

void BluezAdvertisement::setServiceUUIDs(const std::vector<std::string>& uuids)
{
    serviceUUIDs_ = uuids;
    bluezLogger.log().op("SetServiceUUIDs").extra(std::to_string(uuids.size()) + " UUIDs").result("Success").info();
}

void BluezAdvertisement::setAdvertisementType(const std::string& type)
{
    advertisementType_ = type;
    bluezLogger.log().op("SetType").extra(type).result("Success").info();
}

void BluezAdvertisement::setIncludeTxPower(bool include)
{
    includeTxPower_ = include;
    bluezLogger.log().op("SetIncludeTxPower").extra(include ? "true" : "false").result("Success").info();
}

BluezResult<void> BluezAdvertisement::exportToDBus(GDBusConnection* connection)
{
    if (exported_)
    {
        return BluezResult<void>(BluezError::InProgress, "Advertisement already exported");
    }

    connection_ = connection;

    // Ensure introspection data is created and kept alive for the duration
    if (!introspection_data_)
    {
        GError* rawError = nullptr;
        introspection_data_ = g_dbus_node_info_new_for_xml(advertisement_introspection_xml, &rawError);
        if (rawError != nullptr)
        {
            auto err = make_error(rawError);
            return BluezResult<void>::fromGError(err.get());
        }
        interface_info_ = introspection_data_->interfaces[0];
    }

    GError* rawError = nullptr;
    registrationId_ = g_dbus_connection_register_object(
        connection,
        objectPath_.c_str(),
        interface_info_,
        &advertisement_interface_vtable,
        this,  // user_data
        nullptr,  // user_data_free_func
        &rawError);

    auto error = make_error(rawError);

    if (error.get() || registrationId_ == 0)
    {
        bluezLogger.log().op("Export").path(objectPath_).result("Failed").error(error.get() ? error.get()->message : "unknown").warn();
        return BluezResult<void>::fromGError(error.get());
    }

    exported_ = true;
    bluezLogger.log().op("Export").path(objectPath_).result("Success").info();
    return BluezResult<void>();
}

void BluezAdvertisement::unexportFromDBus()
{
    if (exported_ && connection_ && registrationId_ != 0)
    {
        g_dbus_connection_unregister_object(connection_, registrationId_);
        exported_ = false;
        registrationId_ = 0;
        connection_ = nullptr;
        bluezLogger.log().op("Unexport").path(objectPath_).result("Success").info();
    }

    // Release introspection data when no longer exported
    if (introspection_data_)
    {
        g_dbus_node_info_unref(introspection_data_);
        introspection_data_ = nullptr;
        interface_info_ = nullptr;
    }
}

BluezResult<void> BluezAdvertisement::registerAdvertisement(GDBusConnection* connection, const std::string& adapterPath)
{
    if (!exported_)
    {
        auto exportResult = exportToDBus(connection);
        if (exportResult.hasError())
        {
            return exportResult;
        }
    }

    auto error = make_error(nullptr);
    GError* rawError = nullptr;

    // Find the LEAdvertisingManager1 interface on the adapter
    auto variant = make_gvariant(g_dbus_connection_call_sync(
        connection,
        "org.bluez",
        adapterPath.c_str(),
        ADVERTISING_MANAGER_INTERFACE,
        "RegisterAdvertisement",
        g_variant_new("(oa{sv})", objectPath_.c_str(), nullptr), // Empty options dict
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        5000,  // 5 second timeout
        nullptr,  // cancellable
        &rawError));
    error.reset(rawError);

    if (error.get())
    {
        bluezLogger.log().op("RegisterAdvertisement").path(objectPath_).result("Failed").error(error.get()->message).warn();
        return BluezResult<void>::fromGError(error.get());
    }

    registered_ = true;
    bluezLogger.log().op("RegisterAdvertisement").path(objectPath_).result("Success").info();
    return BluezResult<void>();
}

BluezResult<void> BluezAdvertisement::unregisterAdvertisement(GDBusConnection* connection, const std::string& adapterPath)
{
    if (!registered_)
    {
        return BluezResult<void>();
    }

    auto error = make_error(nullptr);
    GError* rawError = nullptr;

    auto variant = make_gvariant(g_dbus_connection_call_sync(
        connection,
        "org.bluez",
        adapterPath.c_str(),
        ADVERTISING_MANAGER_INTERFACE,
        "UnregisterAdvertisement",
        g_variant_new("(o)", objectPath_.c_str()),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,  // cancellable
        &rawError));
    error.reset(rawError);

    if (error.get())
    {
        bluezLogger.log().op("UnregisterAdvertisement").path(objectPath_).result("Failed").error(error.get()->message).warn();
        return BluezResult<void>::fromGError(error.get());
    }

    registered_ = false;
    bluezLogger.log().op("UnregisterAdvertisement").path(objectPath_).result("Success").info();
    return BluezResult<void>();
}

// Async registration methods to prevent deadlock
void BluezAdvertisement::registerAdvertisementAsync(GDBusConnection* connection, const std::string& adapterPath, RegistrationCallback callback)
{
    if (!exported_)
    {
        auto exportResult = exportToDBus(connection);
        if (exportResult.hasError())
        {
            if (callback) {
                callback(exportResult);
            }
            return;
        }
    }

    auto* context = new CallbackContext{this, callback, "RegisterAdvertisement"};

    g_dbus_connection_call(
        connection,
        "org.bluez",
        adapterPath.c_str(),
        ADVERTISING_MANAGER_INTERFACE,
        "RegisterAdvertisement",
        g_variant_new("(oa{sv})", objectPath_.c_str(), nullptr), // Empty options dict
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        15000,  // 15 second timeout (increased from 5s)
        nullptr,  // cancellable
        onRegisterAdvertisementCallback,
        context);
}

void BluezAdvertisement::unregisterAdvertisementAsync(GDBusConnection* connection, const std::string& adapterPath, RegistrationCallback callback)
{
    if (!registered_)
    {
        if (callback) {
            callback(BluezResult<void>());
        }
        return;
    }

    auto* context = new CallbackContext{this, callback, "UnregisterAdvertisement"};

    g_dbus_connection_call(
        connection,
        "org.bluez",
        adapterPath.c_str(),
        ADVERTISING_MANAGER_INTERFACE,
        "UnregisterAdvertisement",
        g_variant_new("(o)", objectPath_.c_str()),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        5000,
        nullptr,  // cancellable
        onUnregisterAdvertisementCallback,
        context);
}

// Async callback handlers
void BluezAdvertisement::onRegisterAdvertisementCallback(GObject* source_object, GAsyncResult* res, gpointer user_data)
{
    auto* context = static_cast<CallbackContext*>(user_data);
    auto* advertisement = context->advertisement;
    auto callback = context->callback;
    auto operation = context->operation;
    delete context;

    GError* error = nullptr;
    auto variant = make_gvariant(g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source_object), res, &error));

    BluezResult<void> result;
    if (error)
    {
        result = BluezResult<void>::fromGError(error);
        bluezLogger.log().op(operation).path(advertisement->objectPath_).result("Failed").error(error->message).warn();
        g_error_free(error);
    }
    else
    {
        advertisement->registered_ = true;
        result = BluezResult<void>();
        bluezLogger.log().op(operation).path(advertisement->objectPath_).result("Success").info();
    }

    if (callback)
    {
        callback(result);
    }
}

void BluezAdvertisement::onUnregisterAdvertisementCallback(GObject* source_object, GAsyncResult* res, gpointer user_data)
{
    auto* context = static_cast<CallbackContext*>(user_data);
    auto* advertisement = context->advertisement;
    auto callback = context->callback;
    auto operation = context->operation;
    delete context;

    GError* error = nullptr;
    auto variant = make_gvariant(g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source_object), res, &error));

    BluezResult<void> result;
    if (error)
    {
        result = BluezResult<void>::fromGError(error);
        bluezLogger.log().op(operation).path(advertisement->objectPath_).result("Failed").error(error->message).warn();
        g_error_free(error);
    }
    else
    {
        advertisement->registered_ = false;
        result = BluezResult<void>();
        bluezLogger.log().op(operation).path(advertisement->objectPath_).result("Success").info();
    }

    if (callback)
    {
        callback(result);
    }
}

// D-Bus method call handler
void BluezAdvertisement::onMethodCall(GDBusConnection* connection,
                                    const gchar* sender,
                                    const gchar* object_path,
                                    const gchar* interface_name,
                                    const gchar* method_name,
                                    GVariant* parameters,
                                    GDBusMethodInvocation* invocation,
                                    gpointer user_data)
{
    auto* advertisement = static_cast<BluezAdvertisement*>(user_data);

    // Suppress unused parameter warnings
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)parameters;

    if (strcmp(method_name, "Release") == 0)
    {
        bluezLogger.log().op("Release").path(advertisement->objectPath_).result("Success").info();
        advertisement->registered_ = false;
        g_dbus_method_invocation_return_value(invocation, nullptr);
    }
    else
    {
        g_dbus_method_invocation_return_error(invocation,
            G_DBUS_ERROR,
            G_DBUS_ERROR_UNKNOWN_METHOD,
            "Unknown method: %s", method_name);
    }
}

// D-Bus property getter
GVariant* BluezAdvertisement::onGetProperty(GDBusConnection* connection,
                                          const gchar* sender,
                                          const gchar* object_path,
                                          const gchar* interface_name,
                                          const gchar* property_name,
                                          GError** error,
                                          gpointer user_data)
{
    auto* advertisement = static_cast<BluezAdvertisement*>(user_data);

    // Suppress unused parameter warnings
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)error;

    if (strcmp(property_name, "Type") == 0)
    {
        return advertisement->getType();
    }
    else if (strcmp(property_name, "ServiceUUIDs") == 0)
    {
        return advertisement->getServiceUUIDs();
    }
    // LocalName property removed - name is included via Includes=["local-name"]
    else if (strcmp(property_name, "Includes") == 0)
    {
        return advertisement->getIncludes();
    }

    return nullptr;
}

// D-Bus property setter (not used for LEAdvertisement1, but required)
gboolean BluezAdvertisement::onSetProperty(GDBusConnection* connection,
                                         const gchar* sender,
                                         const gchar* object_path,
                                         const gchar* interface_name,
                                         const gchar* property_name,
                                         GVariant* value,
                                         GError** error,
                                         gpointer user_data)
{
    // Suppress unused parameter warnings
    (void)connection;
    (void)sender;
    (void)object_path;
    (void)interface_name;
    (void)property_name;
    (void)value;
    (void)user_data;

    // LEAdvertisement1 properties are read-only
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_PROPERTY_READ_ONLY,
                "Property '%s' is read-only", property_name);
    return FALSE;
}

// Property getters for D-Bus interface
GVariant* BluezAdvertisement::getType() const
{
    return g_variant_new_string(advertisementType_.c_str());
}

GVariant* BluezAdvertisement::getServiceUUIDs() const
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));

    for (const auto& uuid : serviceUUIDs_)
    {
        g_variant_builder_add(&builder, "s", uuid.c_str());
    }

    return g_variant_builder_end(&builder);
}

// getLocalName method removed - name included via Includes=["local-name"]

GVariant* BluezAdvertisement::getIncludes() const
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));

    // Always include local-name so the adapter's Alias appears in advertising
    g_variant_builder_add(&builder, "s", "local-name");

    if (includeTxPower_)
    {
        g_variant_builder_add(&builder, "s", "tx-power");
    }

    return g_variant_builder_end(&builder);
}

} // namespace ggk
