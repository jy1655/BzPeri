// Copyright 2017-2019 Paul Nettle
//
// This file is part of Gobbledegook.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  INSIDE THIS FILE
// >>
//
// This is our representation of a GATT Characteristic which is intended to be used in our server description
//
// >>
// >>>  DISCUSSION
// >>
//
// A GATT characteristic is the component within the Bluetooth LE standard that holds and serves data over Bluetooth. This class
// is intended to be used within the server description. For an explanation of how this class is used, see the detailed discussion
// in Server.cpp.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <bzp/GattCharacteristic.h>
#include <bzp/GattDescriptor.h>
#include <bzp/GattProperty.h>
#include <bzp/GattUuid.h>
#include <bzp/DBusObject.h>
#include <bzp/GattService.h>
#include <bzp/Server.h>
#include <bzp/Utils.h>
#include <bzp/Logger.h>

namespace bzp {

namespace {

callbacks::CharacteristicMethodCallHandler makeMethodCallHandler(const callbacks::CharacteristicMethodHandler &callback)
{
	if (!callback)
	{
		return {};
	}

	return [callback](const GattCharacteristic &self, const std::string &methodName, DBusMethodCallRef methodCall) {
		callback(self, methodCall.connection(), methodName, methodCall.parameters(), methodCall.invocation(), methodCall.userData());
	};
}

callbacks::CharacteristicUpdateCallHandler makeUpdateCallHandler(const callbacks::CharacteristicUpdateHandler &callback)
{
	if (!callback)
	{
		return {};
	}

	return [callback](const GattCharacteristic &self, DBusUpdateRef update) {
		return callback(self, update.connection(), update.userData());
	};
}

} // namespace

//
// Standard constructor
//

// Construct a GattCharacteristic
//
// Genreally speaking, these objects should not be constructed directly. Rather, use the `gattCharacteristicBegin()` method
// in `GattService`.
GattCharacteristic::GattCharacteristic(DBusObject &owner, GattService &service, const std::string &name)
: GattInterface(owner, name), service(service)
{
}

// Returning the owner pops us one level up the hierarchy
//
// This method compliments `GattService::gattCharacteristicBegin()`
GattService &GattCharacteristic::gattCharacteristicEnd()
{
	return service;
}

// Locates a D-Bus method within this D-Bus interface and invokes the method
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
bool GattCharacteristic::callMethod(const std::string &methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData) const
{
	return callMethod(methodName, DBusMethodCallRef(pConnection, pParameters, pInvocation, pUserData));
}
#endif

bool GattCharacteristic::callMethod(const std::string &methodName, DBusMethodCallRef methodCall) const
{
	for (const DBusMethod &method : methods)
	{
		if (methodName == method.getName())
		{
			const std::string notImplementedErrorName = owner.getServer().getOwnedName() + ".NotImplemented";
			method.call<GattCharacteristic>(methodCall, getPath(), getName(), methodName, notImplementedErrorName);
			return true;
		}
	}

	return false;
}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
bool GattCharacteristic::callMethod(const std::string &methodName, DBusConnectionRef connection, DBusVariantRef parameters, DBusMethodInvocationRef invocation, gpointer pUserData) const
{
	return callMethod(methodName, DBusMethodCallRef(connection, parameters, invocation, pUserData));
}
#endif

// Modern approach: Use GLib timers directly for periodic updates
// Applications should use g_timeout_add_seconds() or g_timeout_add() for periodic operations

// Specialized support for ReadlValue method
//
// Defined as: array{byte} ReadValue(dict options)
//
// D-Bus breakdown:
//
//     Input args:  options - "a{sv}"
//     Output args: value   - "ay"
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
GattCharacteristic &GattCharacteristic::onReadValue(RawMethodCallback callback)
{
	callbacks::CharacteristicMethodCallHandler handler;
	if (callback != nullptr)
	{
		handler = [callback](const GattCharacteristic &self, const std::string &methodName, DBusMethodCallRef methodCall) {
			callback(self, methodCall.connection().get(), methodName, methodCall.parameters().get(), methodCall.invocation().get(), methodCall.userData());
		};
	}

	static const char *inArgs[] = {"a{sv}", nullptr};
	if (static_cast<bool>(readHandler_)) {
		Logger::warn("GattCharacteristic::onReadValue() called twice — replacing callback without re-adding method");
		readHandler_ = handler;
		return *this;
	}
	readHandler_ = handler;
	addMethod("ReadValue", inArgs, "ay",
		[](const DBusInterface& self, DBusConnectionRef c, const std::string& mn, DBusVariantRef p, DBusMethodInvocationRef inv, void* u) {
			const auto* ch = dynamic_cast<const GattCharacteristic*>(&self);
			if (!ch) {
				Logger::error("ReadValue handler: type mismatch — expected GattCharacteristic");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Type error");
				return;
			}
			if (!ch->readHandler_) return;
			try {
				ch->readHandler_(*ch, mn, DBusMethodCallRef(c, p, inv, u));
			} catch (const std::exception& e) {
				Logger::error(SSTR << "ReadValue handler: user callback threw exception: " << e.what());
				inv.returnDbusError("com.bzperi.Error.InternalError", e.what());
			} catch (...) {
				Logger::error("ReadValue handler: user callback threw unknown exception");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Unknown internal error");
			}
		});
	return *this;
}
#endif

GattCharacteristic &GattCharacteristic::onReadValue(const callbacks::CharacteristicMethodHandler &callback)
{
	return onReadValue(makeMethodCallHandler(callback));
}

GattCharacteristic &GattCharacteristic::onReadValue(const callbacks::CharacteristicMethodCallHandler &callback)
{
	static const char *inArgs[] = {"a{sv}", nullptr};
	if (static_cast<bool>(readHandler_)) {
		Logger::warn("GattCharacteristic::onReadValue() called twice — replacing callback without re-adding method");
		readHandler_ = callback;
		return *this;
	}
	readHandler_ = callback;
	addMethod("ReadValue", inArgs, "ay",
		[](const DBusInterface& self, DBusConnectionRef c, const std::string& mn, DBusVariantRef p, DBusMethodInvocationRef inv, void* u) {
			const auto* ch = dynamic_cast<const GattCharacteristic*>(&self);
			if (!ch) {
				Logger::error("ReadValue handler: type mismatch — expected GattCharacteristic");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Type error");
				return;
			}
			if (!ch->readHandler_) return;
			try {
				ch->readHandler_(*ch, mn, DBusMethodCallRef(c, p, inv, u));
			} catch (const std::exception& e) {
				Logger::error(SSTR << "ReadValue handler: user callback threw exception: " << e.what());
				inv.returnDbusError("com.bzperi.Error.InternalError", e.what());
			} catch (...) {
				Logger::error("ReadValue handler: user callback threw unknown exception");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Unknown internal error");
			}
		});
	return *this;
}

// Specialized support for WriteValue method
//
// Defined as: void WriteValue(array{byte} value, dict options)
//
// D-Bus breakdown:
//
//     Input args:  value   - "ay"
//                  options - "a{sv}"
//     Output args: void
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
GattCharacteristic &GattCharacteristic::onWriteValue(RawMethodCallback callback)
{
	callbacks::CharacteristicMethodCallHandler handler;
	if (callback != nullptr)
	{
		handler = [callback](const GattCharacteristic &self, const std::string &methodName, DBusMethodCallRef methodCall) {
			callback(self, methodCall.connection().get(), methodName, methodCall.parameters().get(), methodCall.invocation().get(), methodCall.userData());
		};
	}

	static const char *inArgs[] = {"ay", "a{sv}", nullptr};
	if (static_cast<bool>(writeHandler_)) {
		Logger::warn("GattCharacteristic::onWriteValue() called twice — replacing callback without re-adding method");
		writeHandler_ = handler;
		return *this;
	}
	writeHandler_ = handler;
	addMethod("WriteValue", inArgs, nullptr,
		[](const DBusInterface& self, DBusConnectionRef c, const std::string& mn, DBusVariantRef p, DBusMethodInvocationRef inv, void* u) {
			const auto* ch = dynamic_cast<const GattCharacteristic*>(&self);
			if (!ch) {
				Logger::error("WriteValue handler: type mismatch — expected GattCharacteristic");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Type error");
				return;
			}
			if (!ch->writeHandler_) return;
			try {
				ch->writeHandler_(*ch, mn, DBusMethodCallRef(c, p, inv, u));
			} catch (const std::exception& e) {
				Logger::error(SSTR << "WriteValue handler: user callback threw exception: " << e.what());
				inv.returnDbusError("com.bzperi.Error.InternalError", e.what());
			} catch (...) {
				Logger::error("WriteValue handler: user callback threw unknown exception");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Unknown internal error");
			}
		});
	return *this;
}
#endif

GattCharacteristic &GattCharacteristic::onWriteValue(const callbacks::CharacteristicMethodHandler &callback)
{
	return onWriteValue(makeMethodCallHandler(callback));
}

GattCharacteristic &GattCharacteristic::onWriteValue(const callbacks::CharacteristicMethodCallHandler &callback)
{
	static const char *inArgs[] = {"ay", "a{sv}", nullptr};
	if (static_cast<bool>(writeHandler_)) {
		Logger::warn("GattCharacteristic::onWriteValue() called twice — replacing callback without re-adding method");
		writeHandler_ = callback;
		return *this;
	}
	writeHandler_ = callback;
	addMethod("WriteValue", inArgs, nullptr,
		[](const DBusInterface& self, DBusConnectionRef c, const std::string& mn, DBusVariantRef p, DBusMethodInvocationRef inv, void* u) {
			const auto* ch = dynamic_cast<const GattCharacteristic*>(&self);
			if (!ch) {
				Logger::error("WriteValue handler: type mismatch — expected GattCharacteristic");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Type error");
				return;
			}
			if (!ch->writeHandler_) return;
			try {
				ch->writeHandler_(*ch, mn, DBusMethodCallRef(c, p, inv, u));
			} catch (const std::exception& e) {
				Logger::error(SSTR << "WriteValue handler: user callback threw exception: " << e.what());
				inv.returnDbusError("com.bzperi.Error.InternalError", e.what());
			} catch (...) {
				Logger::error("WriteValue handler: user callback threw unknown exception");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Unknown internal error");
			}
		});
	return *this;
}

// Custom support for handling updates to our characteristic's value
//
// Defined as: (NOT defined by Bluetooth or BlueZ - this method is internal only)
//
// This method is called by our framework whenever a characteristic's value is updated. If you need to perform any actions
// when a value is updatd, this is a good place to do that work.
//
// If you need to perform the same action(s) when a value is updated from the client (via `onWriteValue`) or from this server,
// then it may be beneficial to call this method from within your onWriteValue callback to reduce duplicated code. See
// `callOnUpdatedValue` for more information.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
GattCharacteristic &GattCharacteristic::onUpdatedValue(RawUpdatedValueCallback callback)
{
	if (callback == nullptr)
	{
		updateHandler_ = {};
		return *this;
	}

	updateHandler_ = [callback](const GattCharacteristic &self, DBusUpdateRef update) {
		return callback(self, update.connection().get(), update.userData());
	};
	return *this;
}
#endif

GattCharacteristic &GattCharacteristic::onUpdatedValue(const callbacks::CharacteristicUpdateHandler &callback)
{
	updateHandler_ = makeUpdateCallHandler(callback);
	return *this;
}

GattCharacteristic &GattCharacteristic::onUpdatedValue(const callbacks::CharacteristicUpdateCallHandler &callback)
{
	updateHandler_ = callback;
	return *this;
}

// Calls the onUpdatedValue method, if one was set.
//
// Returns false if there was no method set, otherwise, returns the boolean result of the method call.
//
// If you need to perform the same action(s) when a value is updated from the client (via onWriteValue) or from this server,
// then it may be beneficial to place those actions in the `onUpdatedValue` method and call it from from within your
// `onWriteValue` callback to reduce duplicated code. To call the `onUpdatedValue` method from within your `onWriteValue`, you
// can use this pattern:
//
//      .onWriteValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
//      {
//          // Update your value
//          ...
//
//          // Call the onUpdateValue method that was set in the same Characteristic
//          self.callOnUpdatedValue(pConnection, pUserData);
//      })
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
bool GattCharacteristic::callOnUpdatedValue(GDBusConnection *pConnection, void *pUserData) const
{
	return callOnUpdatedValue(DBusUpdateRef(pConnection, pUserData));
}
#endif

bool GattCharacteristic::callOnUpdatedValue(DBusConnectionRef connection, void *pUserData) const
{
	return callOnUpdatedValue(DBusUpdateRef(connection, pUserData));
}

bool GattCharacteristic::callOnUpdatedValue(DBusUpdateRef update) const
{
	if (!updateHandler_)
	{
		return false;
	}

	LOG_DEBUG_STREAM(SSTR << "Calling OnUpdatedValue function for interface at path '" << getPath() << "'");
	return updateHandler_(*this, update);
}

// Convenience functions to add a GATT descriptor to the hierarchy
//
// We simply add a new child at the given path and add an interface configured as a GATT descriptor to it. The
// new descriptor is declared with a UUID and a variable argument list of flags (in string form.) For a complete and
// up-to-date list of flag values, see: https://git.kernel.org/pub/scm/bluetooth/bluez.git/plain/doc/gatt-api.txt
//
// At the time of this writing, the list of flags is as follows:
//
//             "read"
//             "write"
//             "encrypt-read"
//             "encrypt-write"
//             "encrypt-authenticated-read"
//             "encrypt-authenticated-write"
//             "secure-read" (Server Only)
//             "secure-write" (Server Only)
//
//
// To end a descriptor, call `GattDescriptor::gattDescriptorEnd()`
GattDescriptor &GattCharacteristic::gattDescriptorBegin(const std::string &pathElement, const GattUuid &uuid, const std::vector<const char *> &flags)
{
	DBusObject &child = owner.addChild(DBusObjectPath(pathElement));
	GattDescriptor &descriptor = *child.addInterface(std::make_shared<GattDescriptor>(child, *this, "org.bluez.GattDescriptor1"));
	descriptor.addProperty<GattDescriptor>("UUID", uuid);
	descriptor.addProperty<GattDescriptor>("Characteristic", getPath());
	descriptor.addProperty<GattDescriptor>("Flags", flags);
	return descriptor;
}

// Sends a change notification to subscribers to this characteristic
//
// This is a generalized method that accepts a `GVariant *`. A templated version is available that supports common types called
// `sendChangeNotificationValue()`.
//
// The caller may choose to consult getActiveBluezAdapter().getActiveConnectionCount() in order to determine if there are any
// active connections before sending a change notification.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
void GattCharacteristic::sendChangeNotificationVariant(GDBusConnection *pBusConnection, GVariant *pNewValue) const
{
	(void)sendChangeNotificationVariant(DBusNotificationRef(pBusConnection, pNewValue));
}
#endif

void GattCharacteristic::sendChangeNotificationVariant(DBusConnectionRef busConnection, DBusVariantRef newValue) const
{
	(void)sendChangeNotificationVariant(DBusNotificationRef(busConnection, newValue));
}

void GattCharacteristic::sendChangeNotificationVariant(DBusNotificationRef notification) const
{
	(void)sendChangeNotificationVariantChecked(notification);
}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
bool GattCharacteristic::sendChangeNotificationVariantChecked(GDBusConnection *pBusConnection, GVariant *pNewValue) const
{
	return sendChangeNotificationVariantChecked(DBusNotificationRef(pBusConnection, pNewValue));
}
#endif

bool GattCharacteristic::sendChangeNotificationVariantChecked(DBusConnectionRef busConnection, DBusVariantRef newValue) const
{
	return sendChangeNotificationVariantChecked(DBusNotificationRef(busConnection, newValue));
}

bool GattCharacteristic::sendChangeNotificationVariantChecked(DBusNotificationRef notification) const
{
	g_auto(GVariantBuilder) builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add(&builder, "{sv}", "Value", notification.value().get());
	GVariant *pSasv = g_variant_new("(sa{sv})", "org.bluez.GattCharacteristic1", &builder);
	return owner.emitSignalChecked(DBusSignalRef(notification.connection(), "org.freedesktop.DBus.Properties", "PropertiesChanged", DBusVariantRef(pSasv)));
}
}; // namespace bzp
