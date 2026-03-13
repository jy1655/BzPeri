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
// See the discussion at the top of GattCharacteristic.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <bzp/GLibTypes.h>
#include <string>
#include <list>

#include <bzp/Utils.h>
#include "GattInterface.h"
#include <bzp/BluezAdapter.h>

namespace bzp {

// ---------------------------------------------------------------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------------------------------------------------------------

struct GattCharacteristic;
struct GattDescriptor;
struct GattProperty;
struct GattService;
struct GattUuid;
struct DBusObject;

// ---------------------------------------------------------------------------------------------------------------------------------
// Modern C++ Callback Types
// ---------------------------------------------------------------------------------------------------------------------------------

#include <functional>

namespace callbacks {
	// Modern typed callback helpers - no more macro magic
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	using CharacteristicMethodFunc BZP_DEPRECATED("Use callbacks::CharacteristicMethodHandler instead") = LegacyMethodFunction<GattCharacteristic>;
	using CharacteristicUpdateFunc BZP_DEPRECATED("Use callbacks::CharacteristicUpdateHandler instead") = LegacyUpdateFunction<GattCharacteristic>;
#endif
	using CharacteristicMethodHandler = std::function<void(const GattCharacteristic&, DBusConnectionRef, const std::string&, DBusVariantRef, DBusMethodInvocationRef, void*)>;
	using CharacteristicUpdateHandler = std::function<bool(const GattCharacteristic&, DBusConnectionRef, void*)>;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Representation of a Bluetooth GATT Characteristic
// ---------------------------------------------------------------------------------------------------------------------------------

struct GattCharacteristic : GattInterface
{
	// Our interface type
	static constexpr const char *kInterfaceType = "GattCharacteristic";

	using RawMethodCallback = bzp::RawMethodCallback<GattCharacteristic>;
	using MethodCallback BZP_DEPRECATED("Use callbacks::CharacteristicMethodHandler instead of raw GDBus callback typedefs") = RawMethodCallback;
	using RawUpdatedValueCallback = bzp::RawUpdateCallback<GattCharacteristic>;
	using UpdatedValueCallback BZP_DEPRECATED("Use callbacks::CharacteristicUpdateHandler instead of raw GDBus callback typedefs") = RawUpdatedValueCallback;

	// Construct a GattCharacteristic
	//
	// Genreally speaking, these objects should not be constructed directly. Rather, use the `gattCharacteristicBegin()` method
	// in `GattService`.
	GattCharacteristic(DBusObject &owner, GattService &service, const std::string &name);
	virtual ~GattCharacteristic() {}

	// Returns a string identifying the type of interface
	virtual const std::string getInterfaceType() const { return GattCharacteristic::kInterfaceType; }

	// Returning the owner pops us one level up the hierarchy
	//
	// This method compliments `GattService::gattCharacteristicBegin()`
	GattService &gattCharacteristicEnd();

	// Locates a D-Bus method within this D-Bus interface and invokes the method
	BZP_DEPRECATED("Use GattCharacteristic::callMethod() wrapper overload with DBusConnectionRef/DBusVariantRef/DBusMethodInvocationRef")
	virtual bool callMethod(const std::string &methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData) const;
	bool callMethod(const std::string &methodName, DBusConnectionRef connection, DBusVariantRef parameters, DBusMethodInvocationRef invocation, gpointer pUserData) const;

	// Modern periodic updates: Use GLib timers directly
	// Example: g_timeout_add_seconds(60, [](gpointer data) -> gboolean {
	//   auto* characteristic = static_cast<GattCharacteristic*>(data);
	//   characteristic->sendChangeNotificationValue(connection, newValue);
	//   return G_SOURCE_CONTINUE; // Keep repeating
	// }, this);

	// Specialized support for Characteristic ReadlValue method
	//
	// Defined as: array{byte} ReadValue(dict options)
	//
	// D-Bus breakdown:
	//
	//     Input args:  options - "a{sv}"
	//     Output args: value   - "ay"
	BZP_DEPRECATED("Use GattCharacteristic::onReadValue() with callbacks::CharacteristicMethodHandler")
	GattCharacteristic &onReadValue(RawMethodCallback callback);
	GattCharacteristic &onReadValue(const callbacks::CharacteristicMethodHandler &callback);

	// Specialized support for Characteristic WriteValue method
	//
	// Defined as: void WriteValue(array{byte} value, dict options)
	//
	// D-Bus breakdown:
	//
	//     Input args:  value   - "ay"
	//                  options - "a{sv}"
	//     Output args: void
	BZP_DEPRECATED("Use GattCharacteristic::onWriteValue() with callbacks::CharacteristicMethodHandler")
	GattCharacteristic &onWriteValue(RawMethodCallback callback);
	GattCharacteristic &onWriteValue(const callbacks::CharacteristicMethodHandler &callback);

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
	BZP_DEPRECATED("Use GattCharacteristic::onUpdatedValue() with callbacks::CharacteristicUpdateHandler")
	GattCharacteristic &onUpdatedValue(RawUpdatedValueCallback callback);
	GattCharacteristic &onUpdatedValue(const callbacks::CharacteristicUpdateHandler &callback);

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
	BZP_DEPRECATED("Use GattCharacteristic::callOnUpdatedValue() wrapper overload with DBusConnectionRef")
	bool callOnUpdatedValue(GDBusConnection *pConnection, void *pUserData) const;
	bool callOnUpdatedValue(DBusConnectionRef connection, void *pUserData) const;

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
	// To end the descriptor, call `gattDescriptorEnd()`
	GattDescriptor &gattDescriptorBegin(const std::string &pathElement, const GattUuid &uuid, const std::vector<const char *> &flags);

	// Sends a change notification to subscribers to this characteristic
	//
	// This is a generalized method that accepts a `GVariant *`. A templated version is available that supports common types called
	// `sendChangeNotificationValue()`.
	//
	// The caller may choose to consult getActiveBluezAdapter().getActiveConnectionCount() in order to determine if there are any
	// active connections before sending a change notification.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use GattCharacteristic::sendChangeNotificationVariant() wrapper overload with DBusConnectionRef/DBusVariantRef")
	void sendChangeNotificationVariant(GDBusConnection *pBusConnection, GVariant *pNewValue) const;
#endif
	void sendChangeNotificationVariant(DBusConnectionRef busConnection, DBusVariantRef newValue) const;

	// Checked variant of sendChangeNotificationVariant(). Returns false if the signal could not be emitted.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use GattCharacteristic::sendChangeNotificationVariantChecked() wrapper overload with DBusConnectionRef/DBusVariantRef")
	bool sendChangeNotificationVariantChecked(GDBusConnection *pBusConnection, GVariant *pNewValue) const;
#endif
	bool sendChangeNotificationVariantChecked(DBusConnectionRef busConnection, DBusVariantRef newValue) const;

	// Sends a change notification to subscribers to this characteristic
	//
	// This is a helper method that accepts common types. For custom types, there is a form that accepts a `GVariant *`, called
	// `sendChangeNotificationVariant()`.
	//
	// The caller may choose to consult getActiveBluezAdapter().getActiveConnectionCount() in order to determine if there are any
	// active connections before sending a change notification.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattCharacteristic::sendChangeNotificationValue() wrapper overload with DBusConnectionRef")
	void sendChangeNotificationValue(GDBusConnection *pBusConnection, T value) const
	{
		sendChangeNotificationVariant(DBusConnectionRef(pBusConnection), Utils::dbusVariantFromByteArray(value));
	}
#endif

	template<typename T>
	void sendChangeNotificationValue(DBusConnectionRef busConnection, T value) const
	{
		sendChangeNotificationVariant(busConnection, Utils::dbusVariantFromByteArray(value));
	}

protected:

	GattService &service;
	RawUpdatedValueCallback pOnUpdatedValueFunc;

private:

	// Stored callbacks for static thunk pattern
	RawMethodCallback readCallback_ = nullptr;
	RawMethodCallback writeCallback_ = nullptr;
	callbacks::CharacteristicMethodHandler readHandler_;
	callbacks::CharacteristicMethodHandler writeHandler_;
	callbacks::CharacteristicUpdateHandler updateHandler_;
};

}; // namespace bzp
