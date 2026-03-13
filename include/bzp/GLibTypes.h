// Minimal GLib/GIO forward declarations for public headers.
// This keeps BzPeri public includes from depending on <gio/gio.h> unless full GIO APIs are required.

#pragma once

#include <glib.h>
#include <functional>
#include <string>

#if defined(__cplusplus)
#define BZP_DEPRECATED(message) [[deprecated(message)]]
#else
#define BZP_DEPRECATED(message)
#endif

#ifndef BZP_ENABLE_LEGACY_SINGLETON_COMPAT
#define BZP_ENABLE_LEGACY_SINGLETON_COMPAT 1
#endif

#ifndef BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
#define BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT 1
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

class DBusVariantRef;

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
using RawPropertyGetterCallback = GDBusInterfaceGetPropertyFunc;
using RawPropertySetterCallback = GDBusInterfaceSetPropertyFunc;
using LegacyPropertyGetterCallback BZP_DEPRECATED("Use callbacks::PropertyGetterHandler instead of raw GDBus property callbacks") = RawPropertyGetterCallback;
using LegacyPropertySetterCallback BZP_DEPRECATED("Use callbacks::PropertySetterHandler instead of raw GDBus property callbacks") = RawPropertySetterCallback;

template<typename TOwner>
using RawMethodCallback = void (*)(const TOwner&, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation*, void*);

template<typename TOwner>
using RawUpdateCallback = bool (*)(const TOwner&, GDBusConnection*, void*);

template<typename TOwner>
using LegacyMethodFunction = std::function<void(const TOwner&, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation*, void*)>;

template<typename TOwner>
using LegacyUpdateFunction = std::function<bool(const TOwner&, GDBusConnection*, void*)>;
#endif

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
	void returnDbusError(const char *errorName, const char *message) const noexcept;
	void returnValue(DBusVariantRef variant, bool wrapInTuple = false) const;

private:
	GDBusMethodInvocation *invocation_;
};

class DBusObjectManagerRef
{
public:
	explicit DBusObjectManagerRef(GDBusObjectManager *objectManager = nullptr) noexcept
	: objectManager_(objectManager)
	{
	}

	[[nodiscard]] GDBusObjectManager *get() const noexcept { return objectManager_; }
	[[nodiscard]] explicit operator bool() const noexcept { return objectManager_ != nullptr; }

private:
	GDBusObjectManager *objectManager_;
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

inline void DBusMethodInvocationRef::returnDbusError(const char *errorName, const char *message) const noexcept
{
	if (invocation_ != nullptr)
	{
		g_dbus_method_invocation_return_dbus_error(invocation_, errorName, message);
	}
}

inline void DBusMethodInvocationRef::returnValue(DBusVariantRef variant, bool wrapInTuple) const
{
	if (invocation_ == nullptr)
	{
		return;
	}

	GVariant *parameters = variant.get();
	if (wrapInTuple)
	{
		parameters = g_variant_new_tuple(&parameters, 1);
	}

	g_dbus_method_invocation_return_value(invocation_, parameters);
}

class DBusMethodCallRef
{
public:
	explicit DBusMethodCallRef(
		DBusConnectionRef connection = DBusConnectionRef(),
		DBusVariantRef parameters = DBusVariantRef(),
		DBusMethodInvocationRef invocation = DBusMethodInvocationRef(),
		gpointer userData = nullptr) noexcept
	: connection_(connection)
	, parameters_(parameters)
	, invocation_(invocation)
	, userData_(userData)
	{
	}

	explicit DBusMethodCallRef(
		GDBusConnection *connection,
		GVariant *parameters,
		GDBusMethodInvocation *invocation,
		gpointer userData = nullptr) noexcept
	: DBusMethodCallRef(DBusConnectionRef(connection), DBusVariantRef(parameters), DBusMethodInvocationRef(invocation), userData)
	{
	}

	[[nodiscard]] DBusConnectionRef connection() const noexcept { return connection_; }
	[[nodiscard]] DBusVariantRef parameters() const noexcept { return parameters_; }
	[[nodiscard]] DBusMethodInvocationRef invocation() const noexcept { return invocation_; }
	[[nodiscard]] gpointer userData() const noexcept { return userData_; }
	void returnDbusError(const char *errorName, const char *message) const noexcept { invocation_.returnDbusError(errorName, message); }
	void returnValue(DBusVariantRef variant, bool wrapInTuple = false) const { invocation_.returnValue(variant, wrapInTuple); }

private:
	DBusConnectionRef connection_;
	DBusVariantRef parameters_;
	DBusMethodInvocationRef invocation_;
	gpointer userData_;
};

class DBusReplyRef
{
public:
	explicit DBusReplyRef(
		DBusMethodInvocationRef invocation = DBusMethodInvocationRef()) noexcept
	: invocation_(invocation)
	{
	}

	explicit DBusReplyRef(DBusMethodCallRef methodCall) noexcept
	: invocation_(methodCall.invocation())
	{
	}

	explicit DBusReplyRef(GDBusMethodInvocation *invocation) noexcept
	: invocation_(DBusMethodInvocationRef(invocation))
	{
	}

	[[nodiscard]] DBusMethodInvocationRef invocation() const noexcept { return invocation_; }
	void returnDbusError(const char *errorName, const char *message) const noexcept { invocation_.returnDbusError(errorName, message); }
	void returnValue(DBusVariantRef variant, bool wrapInTuple = false) const { invocation_.returnValue(variant, wrapInTuple); }

private:
	DBusMethodInvocationRef invocation_;
};

class DBusUpdateRef
{
public:
	explicit DBusUpdateRef(
		DBusConnectionRef connection = DBusConnectionRef(),
		gpointer userData = nullptr) noexcept
	: connection_(connection)
	, userData_(userData)
	{
	}

	explicit DBusUpdateRef(
		GDBusConnection *connection,
		gpointer userData = nullptr) noexcept
	: DBusUpdateRef(DBusConnectionRef(connection), userData)
	{
	}

	[[nodiscard]] DBusConnectionRef connection() const noexcept { return connection_; }
	[[nodiscard]] gpointer userData() const noexcept { return userData_; }

private:
	DBusConnectionRef connection_;
	gpointer userData_;
};

class DBusNotificationRef
{
public:
	explicit DBusNotificationRef(
		DBusConnectionRef connection = DBusConnectionRef(),
		DBusVariantRef value = DBusVariantRef()) noexcept
	: connection_(connection)
	, value_(value)
	{
	}

	explicit DBusNotificationRef(
		GDBusConnection *connection,
		GVariant *value) noexcept
	: DBusNotificationRef(DBusConnectionRef(connection), DBusVariantRef(value))
	{
	}

	[[nodiscard]] DBusConnectionRef connection() const noexcept { return connection_; }
	[[nodiscard]] DBusVariantRef value() const noexcept { return value_; }

private:
	DBusConnectionRef connection_;
	DBusVariantRef value_;
};

class DBusSignalRef
{
public:
	explicit DBusSignalRef(
		DBusConnectionRef connection = DBusConnectionRef(),
		std::string_view interfaceName = {},
		std::string_view signalName = {},
		DBusVariantRef parameters = DBusVariantRef()) noexcept
	: connection_(connection)
	, interfaceName_(interfaceName)
	, signalName_(signalName)
	, parameters_(parameters)
	{
	}

	[[nodiscard]] DBusConnectionRef connection() const noexcept { return connection_; }
	[[nodiscard]] std::string_view interfaceName() const noexcept { return interfaceName_; }
	[[nodiscard]] std::string_view signalName() const noexcept { return signalName_; }
	[[nodiscard]] DBusVariantRef parameters() const noexcept { return parameters_; }

private:
	DBusConnectionRef connection_;
	std::string_view interfaceName_;
	std::string_view signalName_;
	DBusVariantRef parameters_;
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

class DBusPropertyCallRef
{
public:
	explicit DBusPropertyCallRef(
		DBusConnectionRef connection = DBusConnectionRef(),
		std::string_view sender = {},
		std::string_view objectPath = {},
		std::string_view interfaceName = {},
		std::string_view propertyName = {},
		DBusVariantRef value = DBusVariantRef(),
		DBusErrorRef error = DBusErrorRef(),
		gpointer userData = nullptr) noexcept
	: connection_(connection)
	, sender_(sender)
	, objectPath_(objectPath)
	, interfaceName_(interfaceName)
	, propertyName_(propertyName)
	, value_(value)
	, error_(error)
	, userData_(userData)
	{
	}

	[[nodiscard]] DBusConnectionRef connection() const noexcept { return connection_; }
	[[nodiscard]] std::string_view sender() const noexcept { return sender_; }
	[[nodiscard]] std::string_view objectPath() const noexcept { return objectPath_; }
	[[nodiscard]] std::string_view interfaceName() const noexcept { return interfaceName_; }
	[[nodiscard]] std::string_view propertyName() const noexcept { return propertyName_; }
	[[nodiscard]] DBusVariantRef value() const noexcept { return value_; }
	[[nodiscard]] DBusErrorRef error() const noexcept { return error_; }
	[[nodiscard]] gpointer userData() const noexcept { return userData_; }

private:
	DBusConnectionRef connection_;
	std::string_view sender_;
	std::string_view objectPath_;
	std::string_view interfaceName_;
	std::string_view propertyName_;
	DBusVariantRef value_;
	DBusErrorRef error_;
	gpointer userData_;
};

} // namespace bzp
