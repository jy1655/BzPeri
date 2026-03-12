// Minimal GLib/GIO forward declarations for public headers.
// This keeps BzPeri public includes from depending on <gio/gio.h> unless full GIO APIs are required.

#pragma once

#include <glib.h>

#if defined(__cplusplus)
#define BZP_DEPRECATED(message) [[deprecated(message)]]
#else
#define BZP_DEPRECATED(message)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _GDBusConnection GDBusConnection;
typedef struct _GDBusMethodInvocation GDBusMethodInvocation;
typedef struct _GDBusObject GDBusObject;
typedef struct _GDBusObjectManager GDBusObjectManager;
typedef struct _GDBusProxy GDBusProxy;

typedef GVariant *(*GDBusInterfaceGetPropertyFunc) (GDBusConnection *connection,
                                                    const gchar *sender,
                                                    const gchar *object_path,
                                                    const gchar *interface_name,
                                                    const gchar *property_name,
                                                    GError **error,
                                                    gpointer user_data);

typedef gboolean (*GDBusInterfaceSetPropertyFunc) (GDBusConnection *connection,
                                                   const gchar *sender,
                                                   const gchar *object_path,
                                                   const gchar *interface_name,
                                                   const gchar *property_name,
                                                   GVariant *value,
                                                   GError **error,
                                                   gpointer user_data);

gboolean g_dbus_connection_emit_signal(GDBusConnection *connection,
                                       const gchar *destination_bus_name,
                                       const gchar *object_path,
                                       const gchar *interface_name,
                                       const gchar *signal_name,
                                       GVariant *parameters,
                                       GError **error);

void g_dbus_method_invocation_return_dbus_error(GDBusMethodInvocation *invocation,
                                                const gchar *error_name,
                                                const gchar *error_message);

void g_dbus_method_invocation_return_value(GDBusMethodInvocation *invocation,
                                           GVariant *parameters);

#ifdef __cplusplus
}
#endif

namespace bzp {

class DBusConnectionRef
{
public:
	explicit DBusConnectionRef(GDBusConnection *connection = nullptr) noexcept
	: connection_(connection)
	{
	}

	[[nodiscard]] GDBusConnection *get() const noexcept { return connection_; }
	[[nodiscard]] explicit operator bool() const noexcept { return connection_ != nullptr; }

private:
	GDBusConnection *connection_;
};

class DBusMethodInvocationRef
{
public:
	explicit DBusMethodInvocationRef(GDBusMethodInvocation *invocation = nullptr) noexcept
	: invocation_(invocation)
	{
	}

	[[nodiscard]] GDBusMethodInvocation *get() const noexcept { return invocation_; }
	[[nodiscard]] explicit operator bool() const noexcept { return invocation_ != nullptr; }

private:
	GDBusMethodInvocation *invocation_;
};

class DBusVariantRef
{
public:
	explicit DBusVariantRef(GVariant *variant = nullptr) noexcept
	: variant_(variant)
	{
	}

	explicit DBusVariantRef(const GVariant *variant) noexcept
	: variant_(const_cast<GVariant *>(variant))
	{
	}

	[[nodiscard]] GVariant *get() const noexcept { return variant_; }
	[[nodiscard]] explicit operator bool() const noexcept { return variant_ != nullptr; }

private:
	GVariant *variant_;
};

class DBusErrorRef
{
public:
	explicit DBusErrorRef(GError **error = nullptr) noexcept
	: error_(error)
	{
	}

	[[nodiscard]] GError **get() const noexcept { return error_; }
	[[nodiscard]] explicit operator bool() const noexcept { return error_ != nullptr; }

private:
	GError **error_;
};

} // namespace bzp
