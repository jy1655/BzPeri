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

#include <glib.h>
#include <gio/gio.h>
#include <string>
#include <list>

#include "Utils.h"
#include "GattInterface.h"
#include "BluezAdapter.h"

namespace ggk {

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

namespace ggk::callbacks {
	// Modern typed callback helpers - no more macro magic
	using CharacteristicMethodFunc = std::function<void(const GattCharacteristic&, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation*, void*)>;
	using CharacteristicUpdateFunc = std::function<bool(const GattCharacteristic&, GDBusConnection*, void*)>;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Representation of a Bluetooth GATT Characteristic
// ---------------------------------------------------------------------------------------------------------------------------------

struct GattCharacteristic : GattInterface
{
	// Our interface type
	static constexpr const char *kInterfaceType = "GattCharacteristic";

	typedef void (*MethodCallback)(const GattCharacteristic &self, GDBusConnection *pConnection, const std::string &methodName, GVariant *pParameters, GDBusMethodInvocation *pInvocation, void *pUserData);
	typedef bool (*UpdatedValueCallback)(const GattCharacteristic &self, GDBusConnection *pConnection, void *pUserData);

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
	virtual bool callMethod(const std::string &methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData) const;

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
	GattCharacteristic &onReadValue(MethodCallback callback);

	// Specialized support for Characteristic WriteValue method
	//
	// Defined as: void WriteValue(array{byte} value, dict options)
	//
	// D-Bus breakdown:
	//
	//     Input args:  value   - "ay"
	//                  options - "a{sv}"
	//     Output args: void
	GattCharacteristic &onWriteValue(MethodCallback callback);

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
	GattCharacteristic &onUpdatedValue(UpdatedValueCallback callback);

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
	bool callOnUpdatedValue(GDBusConnection *pConnection, void *pUserData) const;

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
	// The caller may choose to consult BluezAdapter::getInstance().getActiveConnectionCount() in order to determine if there are any
	// active connections before sending a change notification.
	void sendChangeNotificationVariant(GDBusConnection *pBusConnection, GVariant *pNewValue) const;

	// Sends a change notification to subscribers to this characteristic
	//
	// This is a helper method that accepts common types. For custom types, there is a form that accepts a `GVariant *`, called
	// `sendChangeNotificationVariant()`.
	//
	// The caller may choose to consult BluezAdapter::getInstance().getActiveConnectionCount() in order to determine if there are any
	// active connections before sending a change notification.
	template<typename T>
	void sendChangeNotificationValue(GDBusConnection *pBusConnection, T value) const
	{
		GVariant *pVariant = Utils::gvariantFromByteArray(value);
		sendChangeNotificationVariant(pBusConnection, pVariant);
	}

protected:

	GattService &service;
	UpdatedValueCallback pOnUpdatedValueFunc;

private:

	// Stored callbacks for static thunk pattern
	MethodCallback readCallback_ = nullptr;
	MethodCallback writeCallback_ = nullptr;

	// Static thunks for function pointer compatibility
	static void ReadThunk(const DBusInterface& self, GDBusConnection* c, const std::string& mn, GVariant* p, GDBusMethodInvocation* inv, void* u);
	static void WriteThunk(const DBusInterface& self, GDBusConnection* c, const std::string& mn, GVariant* p, GDBusMethodInvocation* inv, void* u);
};

}; // namespace ggk