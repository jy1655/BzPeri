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
#include <bzp/Utils.h>
#include <bzp/Logger.h>

namespace bzp {

//
// Standard constructor
//

// Construct a GattCharacteristic
//
// Genreally speaking, these objects should not be constructed directly. Rather, use the `gattCharacteristicBegin()` method
// in `GattService`.
GattCharacteristic::GattCharacteristic(DBusObject &owner, GattService &service, const std::string &name)
: GattInterface(owner, name), service(service), pOnUpdatedValueFunc(nullptr)
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
bool GattCharacteristic::callMethod(const std::string &methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData) const
{
	for (const DBusMethod &method : methods)
	{
		if (methodName == method.getName())
		{
			method.call<GattCharacteristic>(pConnection, getPath(), getName(), methodName, pParameters, pInvocation, pUserData);
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
GattCharacteristic &GattCharacteristic::onReadValue(MethodCallback callback)
{
	// array{byte} ReadValue(dict options)
	static const char *inArgs[] = {"a{sv}", nullptr};
	// Store callback and use static thunk
	this->readCallback_ = callback;
	addMethod("ReadValue", inArgs, "ay", &GattCharacteristic::ReadThunk);
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
GattCharacteristic &GattCharacteristic::onWriteValue(MethodCallback callback)
{
	static const char *inArgs[] = {"ay", "a{sv}", nullptr};
	// Store callback and use static thunk
	this->writeCallback_ = callback;
	addMethod("WriteValue", inArgs, nullptr, &GattCharacteristic::WriteThunk);
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
GattCharacteristic &GattCharacteristic::onUpdatedValue(UpdatedValueCallback callback)
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
//      .onWriteValue(CHARACTERISTIC_UPDATED_VALUE_CALLBACK_LAMBDA
//      {
//          // Update your value
//          ...
//
//          // Call the onUpdateValue method that was set in the same Characteristic
//          self.callOnUpdatedValue(pConnection, pUserData);
//      })
bool GattCharacteristic::callOnUpdatedValue(GDBusConnection *pConnection, void *pUserData) const
{
	if (nullptr == pOnUpdatedValueFunc)
	{
		return false;
	}

	Logger::debug(SSTR << "Calling OnUpdatedValue function for interface at path '" << getPath() << "'");
	return pOnUpdatedValueFunc(*this, pConnection, pUserData);
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
// The caller may choose to consult BluezAdapter::getInstance().getActiveConnectionCount() in order to determine if there are any
// active connections before sending a change notification.
void GattCharacteristic::sendChangeNotificationVariant(GDBusConnection *pBusConnection, GVariant *pNewValue) const
{
	g_auto(GVariantBuilder) builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE_ARRAY);
	g_variant_builder_add(&builder, "{sv}", "Value", pNewValue);
	GVariant *pSasv = g_variant_new("(sa{sv})", "org.bluez.GattCharacteristic1", &builder);
	owner.emitSignal(pBusConnection, "org.freedesktop.DBus.Properties", "PropertiesChanged", pSasv);
}

// Static thunk implementations for function pointer compatibility

void GattCharacteristic::ReadThunk(const DBusInterface& self, GDBusConnection* c, const std::string& mn, GVariant* p, GDBusMethodInvocation* inv, void* u)
{
	auto& ch = static_cast<const GattCharacteristic&>(self);
	if (ch.readCallback_) {
		ch.readCallback_(ch, c, mn, p, inv, u);
	}
}

void GattCharacteristic::WriteThunk(const DBusInterface& self, GDBusConnection* c, const std::string& mn, GVariant* p, GDBusMethodInvocation* inv, void* u)
{
	auto& ch = static_cast<const GattCharacteristic&>(self);
	if (ch.writeCallback_) {
		ch.writeCallback_(ch, c, mn, p, inv, u);
	}
}


}; // namespace bzp