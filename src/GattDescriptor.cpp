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

#include <bzp/GattDescriptor.h>
#include <bzp/GattProperty.h>
#include <bzp/DBusObject.h>
#include <bzp/Server.h>
#include <bzp/Utils.h>
#include <bzp/Logger.h>

namespace bzp {

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
			const std::string notImplementedErrorName = owner.getServer().getOwnedName() + ".NotImplemented";
			method.call<GattDescriptor>(pConnection, getPath(), getName(), methodName, notImplementedErrorName, pParameters, pInvocation, pUserData);
			return true;
		}
	}

	return false;
}

bool GattDescriptor::callMethod(const std::string &methodName, DBusConnectionRef connection, DBusVariantRef parameters, DBusMethodInvocationRef invocation, gpointer pUserData) const
{
	return callMethod(methodName, connection.get(), parameters.get(), invocation.get(), pUserData);
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
GattDescriptor &GattDescriptor::onReadValue(RawMethodCallback callback)
{
	// array{byte} ReadValue(dict options)
	const char *inArgs[] = {"a{sv}", nullptr};
	if (readCallback_ != nullptr) {
		Logger::warn("GattDescriptor::onReadValue() called twice — replacing callback without re-adding method");
		this->readCallback_ = callback;
		return *this;
	}
	// Store callback and use static thunk
	this->readCallback_ = callback;
	addMethod("ReadValue", inArgs, "ay", &GattDescriptor::ReadThunk);
	return *this;
}

GattDescriptor &GattDescriptor::onReadValue(const callbacks::DescriptorMethodHandler &callback)
{
	const char *inArgs[] = {"a{sv}", nullptr};
	if (readCallback_ != nullptr || static_cast<bool>(readHandler_)) {
		Logger::warn("GattDescriptor::onReadValue() called twice — replacing callback without re-adding method");
		readCallback_ = nullptr;
		readHandler_ = callback;
		return *this;
	}
	readHandler_ = callback;
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
GattDescriptor &GattDescriptor::onWriteValue(RawMethodCallback callback)
{
	const char *inArgs[] = {"ay", "a{sv}", nullptr};
	if (writeCallback_ != nullptr) {
		Logger::warn("GattDescriptor::onWriteValue() called twice — replacing callback without re-adding method");
		this->writeCallback_ = callback;
		return *this;
	}
	// Store callback and use static thunk
	this->writeCallback_ = callback;
	addMethod("WriteValue", inArgs, nullptr, &GattDescriptor::WriteThunk);
	return *this;
}

GattDescriptor &GattDescriptor::onWriteValue(const callbacks::DescriptorMethodHandler &callback)
{
	const char *inArgs[] = {"ay", "a{sv}", nullptr};
	if (writeCallback_ != nullptr || static_cast<bool>(writeHandler_)) {
		Logger::warn("GattDescriptor::onWriteValue() called twice — replacing callback without re-adding method");
		writeCallback_ = nullptr;
		writeHandler_ = callback;
		return *this;
	}
	writeHandler_ = callback;
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
GattDescriptor &GattDescriptor::onUpdatedValue(RawUpdatedValueCallback callback)
{
	updateHandler_ = {};
	pOnUpdatedValueFunc = callback;
	return *this;
}

GattDescriptor &GattDescriptor::onUpdatedValue(const callbacks::DescriptorUpdateHandler &callback)
{
	pOnUpdatedValueFunc = nullptr;
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
	if (updateHandler_)
	{
		Logger::debug(SSTR << "Calling OnUpdatedValue function for interface at path '" << getPath() << "'");
		return updateHandler_(*this, DBusConnectionRef(pConnection), pUserData);
	}

	if (nullptr == pOnUpdatedValueFunc)
	{
		return false;
	}

	Logger::debug(SSTR << "Calling OnUpdatedValue function for interface at path '" << getPath() << "'");
	return pOnUpdatedValueFunc(*this, pConnection, pUserData);
}

bool GattDescriptor::callOnUpdatedValue(DBusConnectionRef connection, void *pUserData) const
{
	return callOnUpdatedValue(connection.get(), pUserData);
}

// Static thunk implementations for function pointer compatibility

void GattDescriptor::ReadThunk(const DBusInterface& self, GDBusConnection* c, const std::string& mn, GVariant* p, GDBusMethodInvocation* inv, void* u)
{
	const auto* desc = dynamic_cast<const GattDescriptor*>(&self);
	if (!desc) {
		Logger::error("ReadThunk: type mismatch — expected GattDescriptor");
		g_dbus_method_invocation_return_dbus_error(inv, "com.bzperi.Error.InternalError", "Type error");
		return;
	}
	if (!desc->readCallback_ && !desc->readHandler_) return;
	try {
		if (desc->readHandler_) {
			desc->readHandler_(*desc, DBusConnectionRef(c), mn, DBusVariantRef(p), DBusMethodInvocationRef(inv), u);
			return;
		}
		desc->readCallback_(*desc, c, mn, p, inv, u);
	} catch (const std::exception& e) {
		Logger::error(SSTR << "ReadThunk: user callback threw exception: " << e.what());
		g_dbus_method_invocation_return_dbus_error(inv, "com.bzperi.Error.InternalError", e.what());
	} catch (...) {
		Logger::error("ReadThunk: user callback threw unknown exception");
		g_dbus_method_invocation_return_dbus_error(inv, "com.bzperi.Error.InternalError", "Unknown internal error");
	}
}

void GattDescriptor::WriteThunk(const DBusInterface& self, GDBusConnection* c, const std::string& mn, GVariant* p, GDBusMethodInvocation* inv, void* u)
{
	const auto* desc = dynamic_cast<const GattDescriptor*>(&self);
	if (!desc) {
		Logger::error("WriteThunk: type mismatch — expected GattDescriptor");
		g_dbus_method_invocation_return_dbus_error(inv, "com.bzperi.Error.InternalError", "Type error");
		return;
	}
	if (!desc->writeCallback_ && !desc->writeHandler_) return;
	try {
		if (desc->writeHandler_) {
			desc->writeHandler_(*desc, DBusConnectionRef(c), mn, DBusVariantRef(p), DBusMethodInvocationRef(inv), u);
			return;
		}
		desc->writeCallback_(*desc, c, mn, p, inv, u);
	} catch (const std::exception& e) {
		Logger::error(SSTR << "WriteThunk: user callback threw exception: " << e.what());
		g_dbus_method_invocation_return_dbus_error(inv, "com.bzperi.Error.InternalError", e.what());
	} catch (...) {
		Logger::error("WriteThunk: user callback threw unknown exception");
		g_dbus_method_invocation_return_dbus_error(inv, "com.bzperi.Error.InternalError", "Unknown internal error");
	}
}


}; // namespace bzp
