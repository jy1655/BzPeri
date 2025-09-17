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
// A GATT descriptor is the component within the Bluetooth LE standard that holds and serves metadata about a Characteristic over
// Bluetooth. This class is intended to be used within the server description. For an explanation of how this class is used, see the
// detailed discussion in Server.cpp.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "GattDescriptor.h"
#include "GattProperty.h"
#include "DBusObject.h"
#include "Utils.h"
#include "Logger.h"

namespace ggk {

//
// Standard constructor
//

// Construct a GattDescriptor
//
// Genreally speaking, these objects should not be constructed directly. Rather, use the `gattDescriptorBegin()` method
// in `GattCharacteristic`.
GattDescriptor::GattDescriptor(DBusObject &owner, GattCharacteristic &characteristic, const std::string &name)
: GattInterface(owner, name), characteristic(characteristic), pOnUpdatedValueFunc(nullptr)
{
}

// Returning the owner pops us one level up the hierarchy
//
// This method compliments `GattCharacteristic::gattDescriptorBegin()`
GattCharacteristic &GattDescriptor::gattDescriptorEnd()
{
	return characteristic;
}

//
// D-Bus interface methods
//

// Locates a D-Bus method within this D-Bus interface
bool GattDescriptor::callMethod(const std::string &methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData) const
{
	for (const DBusMethod &method : methods)
	{
		if (methodName == method.getName())
		{
			method.call<GattDescriptor>(pConnection, getPath(), getName(), methodName, pParameters, pInvocation, pUserData);
			return true;
		}
	}

	return false;
}

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
GattDescriptor &GattDescriptor::onReadValue(MethodCallback callback)
{
	// array{byte} ReadValue(dict options)
	const char *inArgs[] = {"a{sv}", nullptr};
	// Store callback and use static thunk
	this->readCallback_ = callback;
	addMethod("ReadValue", inArgs, "ay", &GattDescriptor::ReadThunk);
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
GattDescriptor &GattDescriptor::onWriteValue(MethodCallback callback)
{
	const char *inArgs[] = {"ay", "a{sv}", nullptr};
	// Store callback and use static thunk
	this->writeCallback_ = callback;
	addMethod("WriteValue", inArgs, nullptr, &GattDescriptor::WriteThunk);
	return *this;
}

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
GattDescriptor &GattDescriptor::onUpdatedValue(UpdatedValueCallback callback)
{
	pOnUpdatedValueFunc = callback;
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
//      .onUpdatedValue(DESCRIPTOR_UPDATED_VALUE_CALLBACK_LAMBDA
//      {
//          // Update your value
//          ...
//
//          // Call the onUpdateValue method that was set in the same Descriptor
//          self.callOnUpdatedValue(pConnection, pUserData);
//      })
bool GattDescriptor::callOnUpdatedValue(GDBusConnection *pConnection, void *pUserData) const
{
	if (nullptr == pOnUpdatedValueFunc)
	{
		return false;
	}

	Logger::debug(SSTR << "Calling OnUpdatedValue function for interface at path '" << getPath() << "'");
	return pOnUpdatedValueFunc(*this, pConnection, pUserData);
}

// Static thunk implementations for function pointer compatibility

void GattDescriptor::ReadThunk(const DBusInterface& self, GDBusConnection* c, const std::string& mn, GVariant* p, GDBusMethodInvocation* inv, void* u)
{
	auto& desc = static_cast<const GattDescriptor&>(self);
	if (desc.readCallback_) {
		desc.readCallback_(desc, c, mn, p, inv, u);
	}
}

void GattDescriptor::WriteThunk(const DBusInterface& self, GDBusConnection* c, const std::string& mn, GVariant* p, GDBusMethodInvocation* inv, void* u)
{
	auto& desc = static_cast<const GattDescriptor&>(self);
	if (desc.writeCallback_) {
		desc.writeCallback_(desc, c, mn, p, inv, u);
	}
}


}; // namespace ggk