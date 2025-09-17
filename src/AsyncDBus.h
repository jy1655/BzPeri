#pragma once

#include "BluezTypes.h"
#include "GLibRAII.h"
#include <gio/gio.h>
#include <functional>
#include <memory>

namespace ggk {

// Modern async D-Bus call wrapper
// Replaces g_dbus_connection_call_sync with non-blocking patterns
class AsyncDBus {
public:
    using AsyncCallback = std::function<void(BluezResult<GVariantPtr>)>;

    // Async property get
    static void getProperty(GDBusConnection* connection,
                           const std::string& serviceName,
                           const std::string& objectPath,
                           const std::string& interfaceName,
                           const std::string& propertyName,
                           AsyncCallback callback);

    // Async property set
    static void setProperty(GDBusConnection* connection,
                           const std::string& serviceName,
                           const std::string& objectPath,
                           const std::string& interfaceName,
                           const std::string& propertyName,
                           GVariant* value,
                           AsyncCallback callback);

    // Async method call
    static void callMethod(GDBusConnection* connection,
                          const std::string& serviceName,
                          const std::string& objectPath,
                          const std::string& interfaceName,
                          const std::string& methodName,
                          GVariant* parameters,
                          AsyncCallback callback);

private:
    struct CallContext {
        AsyncCallback callback;
        std::string operation;
    };

    static void onPropertyGetReady(GObject* source, GAsyncResult* result, gpointer user_data);
    static void onPropertySetReady(GObject* source, GAsyncResult* result, gpointer user_data);
    static void onMethodCallReady(GObject* source, GAsyncResult* result, gpointer user_data);
};

} // namespace ggk