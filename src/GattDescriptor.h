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
// See the discussion at the top of GattDescriptor.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <gio/gio.h>
#include <string>
#include <list>

#include "Utils.h"
#include "GattInterface.h"

namespace ggk {

// ---------------------------------------------------------------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------------------------------------------------------------

struct GattCharacteristic;
struct GattDescriptor;
struct GattProperty;
struct DBusObject;

// ---------------------------------------------------------------------------------------------------------------------------------
// Modern C++ Callback Types
// ---------------------------------------------------------------------------------------------------------------------------------

#include <functional>

namespace ggk::callbacks {
	// Modern typed callback helpers - no more macro magic
	using DescriptorMethodFunc = std::function<void(const GattDescriptor&, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation*, void*)>;
	using DescriptorUpdateFunc = std::function<bool(const GattDescriptor&, GDBusConnection*, void*)>;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// Representation of a Bluetooth GATT Descriptor
// ---------------------------------------------------------------------------------------------------------------------------------

struct GattDescriptor : GattInterface
{
	// Our interface type
	static constexpr const char *kInterfaceType = "GattDescriptor";

	typedef void (*MethodCallback)(const GattDescriptor &self, GDBusConnection *pConnection, const std::string &methodName, GVariant *pParameters, GDBusMethodInvocation *pInvocation, void *pUserData);
	typedef bool (*UpdatedValueCallback)(const GattDescriptor &self, GDBusConnection *pConnection, void *pUserData);

	//
	// Standard constructor
	//

	// Construct a GattDescriptor
	//
	// Genreally speaking, these objects should not be constructed directly. Rather, use the `gattDescriptorBegin()` method
	// in `GattCharacteristic`.
	GattDescriptor(DBusObject &owner, GattCharacteristic &characteristic, const std::string &name);
	virtual ~GattDescriptor() {}

	// Returns a string identifying the type of interface
	virtual const std::string getInterfaceType() const { return GattDescriptor::kInterfaceType; }

	// Returning the owner pops us one level up the hierarchy
	//
	// This method compliments `GattCharacteristic::gattDescriptorBegin()`
	GattCharacteristic &gattDescriptorEnd();

	// Locates a D-Bus method within this D-Bus interface and invokes the method
	virtual bool callMethod(const std::string &methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData) const;

	// Modern periodic updates: Use GLib timers directly
	// Example: g_timeout_add_seconds(interval, callback, userData)

	// Specialized support for Descriptor ReadlValue method
	//
	// Defined as: array{byte} ReadValue(dict options)
	//
	// D-Bus breakdown:
	//
	//     Input args:  options - "a{sv}"
	//     Output args: value   - "ay"
	GattDescriptor &onReadValue(MethodCallback callback);

	// Specialized support for Descriptor WriteValue method
	//
	// Defined as: void WriteValue(array{byte} value, dict options)
	//
	// D-Bus breakdown:
	//
	//     Input args:  value   - "ay"
	//                  options - "a{sv}"
	//     Output args: void
	GattDescriptor &onWriteValue(MethodCallback callback);

	// Custom support for handling updates to our descriptor's value
	//
	// Defined as: (NOT defined by Bluetooth or BlueZ - this method is internal only)
	//
	// This method is called by our framework whenever a Descriptor's value is updated. If you need to perform any actions
	// when a value is updatd, this is a good place to do that work.
	//
	// If you need to perform the same action(s) when a value is updated from the client (via `onWriteValue`) or from this server,
	// then it may be beneficial to call this method from within your onWriteValue callback to reduce duplicated code. See
	// `callOnUpdatedValue` for more information.
	GattDescriptor &onUpdatedValue(UpdatedValueCallback callback);

	// Calls the onUpdatedValue method, if one was set.
	//
	// Returns false if there was no method set, otherwise, returns the boolean result of the method call.
	//
	// If you need to perform the same action(s) when a value is updated from the client (via onWriteValue) or from this server,
	// then it may be beneficial to place those actions in the `onUpdatedValue` method and call it from from within your
	// `onWriteValue` callback to reduce duplicated code. To call the `onUpdatedValue` method from within your `onWriteValue`, you
	// can use this pattern:
	//
	//      .onUpdatedValue(DESCRIPTOR_UPDATED_VALUE_CALLBACK_LAMBDA
	//      {
	//          // Update your value
	//          ...
	//
	//          // Call the onUpdateValue method that was set in the same Descriptor
	//          self.callOnUpdatedValue(pConnection, pUserData);
	//      })
	bool callOnUpdatedValue(GDBusConnection *pConnection, void *pUserData) const;

protected:

	GattCharacteristic &characteristic;
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