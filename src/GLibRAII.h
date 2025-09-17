#pragma once

#include <memory>
#include <gio/gio.h>
#include <glib.h>

namespace bzp {

// Modern C++ RAII wrappers for GLib objects
// Replaces manual g_object_unref/g_variant_unref patterns

// GVariant RAII wrapper
using GVariantPtr = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;

inline GVariantPtr make_gvariant(GVariant* variant) {
    return GVariantPtr(variant, g_variant_unref);
}

// GDBusConnection RAII wrapper
using GDBusConnectionPtr = std::unique_ptr<GDBusConnection, decltype(&g_object_unref)>;

inline GDBusConnectionPtr make_connection(GDBusConnection* connection) {
    if (connection) g_object_ref(connection);
    return GDBusConnectionPtr(connection, g_object_unref);
}

// GDBusObjectManager RAII wrapper
using GDBusObjectManagerPtr = std::unique_ptr<GDBusObjectManager, decltype(&g_object_unref)>;

inline GDBusObjectManagerPtr make_object_manager(GDBusObjectManager* manager) {
    if (manager) g_object_ref(manager);
    return GDBusObjectManagerPtr(manager, g_object_unref);
}

// GDBusProxy RAII wrapper
using GDBusProxyPtr = std::unique_ptr<GDBusProxy, decltype(&g_object_unref)>;

inline GDBusProxyPtr make_proxy(GDBusProxy* proxy) {
    if (proxy) g_object_ref(proxy);
    return GDBusProxyPtr(proxy, g_object_unref);
}

// GError RAII wrapper
using GErrorPtr = std::unique_ptr<GError, decltype(&g_error_free)>;

inline GErrorPtr make_error(GError* error) {
    return GErrorPtr(error, g_error_free);
}

// GDBusNodeInfo RAII wrapper
using GDBusNodeInfoPtr = std::unique_ptr<GDBusNodeInfo, decltype(&g_dbus_node_info_unref)>;

inline GDBusNodeInfoPtr make_node_info(GDBusNodeInfo* node_info) {
    return GDBusNodeInfoPtr(node_info, g_dbus_node_info_unref);
}

// GMainLoop RAII wrapper
using GMainLoopPtr = std::unique_ptr<GMainLoop, decltype(&g_main_loop_unref)>;

inline GMainLoopPtr make_main_loop(GMainLoop* loop) {
    if (loop) g_main_loop_ref(loop);
    return GMainLoopPtr(loop, g_main_loop_unref);
}

// Timer source RAII wrapper
class TimerSource {
public:
    TimerSource(guint source_id) : source_id_(source_id) {}

    ~TimerSource() {
        if (source_id_ != 0) {
            g_source_remove(source_id_);
        }
    }

    TimerSource(const TimerSource&) = delete;
    TimerSource& operator=(const TimerSource&) = delete;

    TimerSource(TimerSource&& other) noexcept : source_id_(other.source_id_) {
        other.source_id_ = 0;
    }

    TimerSource& operator=(TimerSource&& other) noexcept {
        if (this != &other) {
            if (source_id_ != 0) {
                g_source_remove(source_id_);
            }
            source_id_ = other.source_id_;
            other.source_id_ = 0;
        }
        return *this;
    }

    guint get() const { return source_id_; }

    void release() { source_id_ = 0; }

private:
    guint source_id_;
};

} // namespace bzp