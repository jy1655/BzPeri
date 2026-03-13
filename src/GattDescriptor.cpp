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

namespace {

callbacks::DescriptorMethodCallHandler makeMethodCallHandler(const callbacks::DescriptorMethodHandler &callback)
{
	if (!callback)
	{
		return {};
	}

	return [callback](const GattDescriptor &self, const std::string &methodName, DBusMethodCallRef methodCall) {
		callback(self, methodCall.connection(), methodName, methodCall.parameters(), methodCall.invocation(), methodCall.userData());
	};
}

callbacks::DescriptorUpdateCallHandler makeUpdateCallHandler(const callbacks::DescriptorUpdateHandler &callback)
{
	if (!callback)
	{
		return {};
	}

	return [callback](const GattDescriptor &self, DBusUpdateRef update) {
		return callback(self, update.connection(), update.userData());
	};
}

} // namespace

//
// Standard constructor
//

// Construct a GattDescriptor
//
// Genreally speaking, these objects should not be constructed directly. Rather, use the `gattDescriptorBegin()` method
// in `GattCharacteristic`.
GattDescriptor::GattDescriptor(DBusObject &owner, GattCharacteristic &characteristic, const std::string &name)
: GattInterface(owner, name), characteristic(characteristic)
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
	return callMethod(methodName, DBusMethodCallRef(pConnection, pParameters, pInvocation, pUserData));
}

bool GattDescriptor::callMethod(const std::string &methodName, DBusMethodCallRef methodCall) const
{
	for (const DBusMethod &method : methods)
	{
		if (methodName == method.getName())
		{
			const std::string notImplementedErrorName = owner.getServer().getOwnedName() + ".NotImplemented";
			method.call<GattDescriptor>(methodCall, getPath(), getName(), methodName, notImplementedErrorName);
			return true;
		}
	}

	return false;
}

bool GattDescriptor::callMethod(const std::string &methodName, DBusConnectionRef connection, DBusVariantRef parameters, DBusMethodInvocationRef invocation, gpointer pUserData) const
{
	return callMethod(methodName, DBusMethodCallRef(connection, parameters, invocation, pUserData));
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
	callbacks::DescriptorMethodCallHandler handler;
	if (callback != nullptr)
	{
		handler = [callback](const GattDescriptor &self, const std::string &methodName, DBusMethodCallRef methodCall) {
			callback(self, methodCall.connection().get(), methodName, methodCall.parameters().get(), methodCall.invocation().get(), methodCall.userData());
		};
	}

	const char *inArgs[] = {"a{sv}", nullptr};
	if (static_cast<bool>(readHandler_)) {
		Logger::warn("GattDescriptor::onReadValue() called twice — replacing callback without re-adding method");
		readHandler_ = handler;
		return *this;
	}
	readHandler_ = handler;
	addMethod("ReadValue", inArgs, "ay",
		[](const DBusInterface& self, DBusConnectionRef c, const std::string& mn, DBusVariantRef p, DBusMethodInvocationRef inv, void* u) {
			const auto* desc = dynamic_cast<const GattDescriptor*>(&self);
			if (!desc) {
				Logger::error("ReadValue handler: type mismatch — expected GattDescriptor");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Type error");
				return;
			}
			if (!desc->readHandler_) return;
			try {
				desc->readHandler_(*desc, mn, DBusMethodCallRef(c, p, inv, u));
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

GattDescriptor &GattDescriptor::onReadValue(const callbacks::DescriptorMethodHandler &callback)
{
	return onReadValue(makeMethodCallHandler(callback));
}

GattDescriptor &GattDescriptor::onReadValue(const callbacks::DescriptorMethodCallHandler &callback)
{
	const char *inArgs[] = {"a{sv}", nullptr};
	if (static_cast<bool>(readHandler_)) {
		Logger::warn("GattDescriptor::onReadValue() called twice — replacing callback without re-adding method");
		readHandler_ = callback;
		return *this;
	}
	readHandler_ = callback;
	addMethod("ReadValue", inArgs, "ay",
		[](const DBusInterface& self, DBusConnectionRef c, const std::string& mn, DBusVariantRef p, DBusMethodInvocationRef inv, void* u) {
			const auto* desc = dynamic_cast<const GattDescriptor*>(&self);
			if (!desc) {
				Logger::error("ReadValue handler: type mismatch — expected GattDescriptor");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Type error");
				return;
			}
			if (!desc->readHandler_) return;
			try {
				desc->readHandler_(*desc, mn, DBusMethodCallRef(c, p, inv, u));
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
GattDescriptor &GattDescriptor::onWriteValue(RawMethodCallback callback)
{
	callbacks::DescriptorMethodCallHandler handler;
	if (callback != nullptr)
	{
		handler = [callback](const GattDescriptor &self, const std::string &methodName, DBusMethodCallRef methodCall) {
			callback(self, methodCall.connection().get(), methodName, methodCall.parameters().get(), methodCall.invocation().get(), methodCall.userData());
		};
	}

	const char *inArgs[] = {"ay", "a{sv}", nullptr};
	if (static_cast<bool>(writeHandler_)) {
		Logger::warn("GattDescriptor::onWriteValue() called twice — replacing callback without re-adding method");
		writeHandler_ = handler;
		return *this;
	}
	writeHandler_ = handler;
	addMethod("WriteValue", inArgs, nullptr,
		[](const DBusInterface& self, DBusConnectionRef c, const std::string& mn, DBusVariantRef p, DBusMethodInvocationRef inv, void* u) {
			const auto* desc = dynamic_cast<const GattDescriptor*>(&self);
			if (!desc) {
				Logger::error("WriteValue handler: type mismatch — expected GattDescriptor");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Type error");
				return;
			}
			if (!desc->writeHandler_) return;
			try {
				desc->writeHandler_(*desc, mn, DBusMethodCallRef(c, p, inv, u));
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

GattDescriptor &GattDescriptor::onWriteValue(const callbacks::DescriptorMethodHandler &callback)
{
	return onWriteValue(makeMethodCallHandler(callback));
}

GattDescriptor &GattDescriptor::onWriteValue(const callbacks::DescriptorMethodCallHandler &callback)
{
	const char *inArgs[] = {"ay", "a{sv}", nullptr};
	if (static_cast<bool>(writeHandler_)) {
		Logger::warn("GattDescriptor::onWriteValue() called twice — replacing callback without re-adding method");
		writeHandler_ = callback;
		return *this;
	}
	writeHandler_ = callback;
	addMethod("WriteValue", inArgs, nullptr,
		[](const DBusInterface& self, DBusConnectionRef c, const std::string& mn, DBusVariantRef p, DBusMethodInvocationRef inv, void* u) {
			const auto* desc = dynamic_cast<const GattDescriptor*>(&self);
			if (!desc) {
				Logger::error("WriteValue handler: type mismatch — expected GattDescriptor");
				inv.returnDbusError("com.bzperi.Error.InternalError", "Type error");
				return;
			}
			if (!desc->writeHandler_) return;
			try {
				desc->writeHandler_(*desc, mn, DBusMethodCallRef(c, p, inv, u));
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
	if (callback == nullptr)
	{
		updateHandler_ = {};
		return *this;
	}

	updateHandler_ = [callback](const GattDescriptor &self, DBusUpdateRef update) {
		return callback(self, update.connection().get(), update.userData());
	};
	return *this;
}

GattDescriptor &GattDescriptor::onUpdatedValue(const callbacks::DescriptorUpdateHandler &callback)
{
	updateHandler_ = makeUpdateCallHandler(callback);
	return *this;
}

GattDescriptor &GattDescriptor::onUpdatedValue(const callbacks::DescriptorUpdateCallHandler &callback)
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
	return callOnUpdatedValue(DBusUpdateRef(pConnection, pUserData));
}

bool GattDescriptor::callOnUpdatedValue(DBusConnectionRef connection, void *pUserData) const
{
	return callOnUpdatedValue(DBusUpdateRef(connection, pUserData));
}

bool GattDescriptor::callOnUpdatedValue(DBusUpdateRef update) const
{
	if (!updateHandler_)
	{
		return false;
	}

	Logger::debug(SSTR << "Calling OnUpdatedValue function for interface at path '" << getPath() << "'");
	return updateHandler_(*this, update);
}


}; // namespace bzp
