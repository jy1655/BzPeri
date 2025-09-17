#include "AsyncDBus.h"
#include "StructuredLogger.h"

namespace ggk {

void AsyncDBus::getProperty(GDBusConnection* connection,
                           const std::string& serviceName,
                           const std::string& objectPath,
                           const std::string& interfaceName,
                           const std::string& propertyName,
                           AsyncCallback callback)
{
    auto* context = new CallContext{callback, "GetProperty"};

    g_dbus_connection_call(
        connection,
        serviceName.c_str(),
        objectPath.c_str(),
        "org.freedesktop.DBus.Properties",
        "Get",
        g_variant_new("(ss)", interfaceName.c_str(), propertyName.c_str()),
        G_VARIANT_TYPE("(v)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        onPropertyGetReady,
        context
    );
}

void AsyncDBus::setProperty(GDBusConnection* connection,
                           const std::string& serviceName,
                           const std::string& objectPath,
                           const std::string& interfaceName,
                           const std::string& propertyName,
                           GVariant* value,
                           AsyncCallback callback)
{
    auto* context = new CallContext{callback, "SetProperty"};

    g_dbus_connection_call(
        connection,
        serviceName.c_str(),
        objectPath.c_str(),
        "org.freedesktop.DBus.Properties",
        "Set",
        g_variant_new("(ssv)", interfaceName.c_str(), propertyName.c_str(), value),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        onPropertySetReady,
        context
    );
}

void AsyncDBus::callMethod(GDBusConnection* connection,
                          const std::string& serviceName,
                          const std::string& objectPath,
                          const std::string& interfaceName,
                          const std::string& methodName,
                          GVariant* parameters,
                          AsyncCallback callback)
{
    auto* context = new CallContext{callback, "CallMethod"};

    g_dbus_connection_call(
        connection,
        serviceName.c_str(),
        objectPath.c_str(),
        interfaceName.c_str(),
        methodName.c_str(),
        parameters,
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        nullptr,
        onMethodCallReady,
        context
    );
}

void AsyncDBus::onPropertyGetReady(GObject* source, GAsyncResult* result, gpointer user_data)
{
    auto context = std::unique_ptr<CallContext>(static_cast<CallContext*>(user_data));
    auto error = make_error(nullptr);

    auto variant = make_gvariant(g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source), result, error.get()));

    if (error.get()) {
        auto bluezResult = BluezResult<GVariantPtr>::fromGError(error.get());
        context->callback(bluezResult);
    } else {
        GVariant* innerVariant = nullptr;
        g_variant_get(variant.get(), "(v)", &innerVariant);
        context->callback(BluezResult<GVariantPtr>(make_gvariant(innerVariant)));
    }
}

void AsyncDBus::onPropertySetReady(GObject* source, GAsyncResult* result, gpointer user_data)
{
    auto context = std::unique_ptr<CallContext>(static_cast<CallContext*>(user_data));
    auto error = make_error(nullptr);

    auto variant = make_gvariant(g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source), result, error.get()));

    if (error.get()) {
        auto bluezResult = BluezResult<GVariantPtr>::fromGError(error.get());
        context->callback(bluezResult);
    } else {
        context->callback(BluezResult<GVariantPtr>(std::move(variant)));
    }
}

void AsyncDBus::onMethodCallReady(GObject* source, GAsyncResult* result, gpointer user_data)
{
    auto context = std::unique_ptr<CallContext>(static_cast<CallContext*>(user_data));
    auto error = make_error(nullptr);

    auto variant = make_gvariant(g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source), result, error.get()));

    if (error.get()) {
        auto bluezResult = BluezResult<GVariantPtr>::fromGError(error.get());
        context->callback(bluezResult);
    } else {
        context->callback(BluezResult<GVariantPtr>(std::move(variant)));
    }
}

} // namespace ggk