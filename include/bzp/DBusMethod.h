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
// This is a representation of a D-Bus interface method.
//
// >>
// >>>  DISCUSSION
// >>
//
// See the discussion at the top of DBusMethod.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// This file contains a representation of a D-Bus interface method.
//
// Methods are identified by their name (such as "Get" or "Set"). They have argument definitions (defined as part of their
// interface) that describe the type of arguments passed into the method and returned from the method.
//
// In addition to the method itself, we also store a callback that can be called whenever the method is invoked.

#pragma once

#include <bzp/GLibTypes.h>
#include <functional>
#include <string>
#include <vector>

#include <bzp/DBusObjectPath.h>
#include <bzp/Logger.h>

namespace bzp {

struct DBusInterface;

namespace callbacks {
	using InterfaceMethodHandler = std::function<void(const DBusInterface&, DBusConnectionRef, const std::string&, DBusVariantRef, DBusMethodInvocationRef, void*)>;
}

struct DBusMethod
{
	// A method callback delegate
	using RawCallback = void (*)(const DBusInterface &self, GDBusConnection *pConnection, const std::string &methodName, GVariant *pParameters, GDBusMethodInvocation *pInvocation, void *pUserData);
	using Callback BZP_DEPRECATED("Use DBusMethod::Handler instead of raw GDBus callback typedefs") = RawCallback;
	using Handler = callbacks::InterfaceMethodHandler;

	// Instantiate a named method on a given interface (pOwner) with a given set of arguments and a callback delegate
	DBusMethod(const DBusInterface *pOwner, const std::string &name, const char *pInArgs[], const char *pOutArgs, RawCallback callback);
	DBusMethod(const DBusInterface *pOwner, const std::string &name, const char *pInArgs[], const char *pOutArgs, const Handler &handler);

	//
	// Accessors
	//

	// Returns the name of the method
	const std::string &getName() const { return name; }

	// Sets the name of the method
	//
	// This method should generally not be called directly. Rather, the name should be set by the constructor
	DBusMethod &setName(const std::string &name) { this->name = name; return *this; }

	// Get the input argument type string (a GVariant type string format)
	const std::vector<std::string> &getInArgs() const { return inArgs; }

	// Get the output argument type string (a GVariant type string format)
	const std::string &getOutArgs() const { return outArgs; }

	// Set the argument types for this method
	//
	// This method should generally not be called directly. Rather, the arguments should be set by the constructor
	DBusMethod &setArgs(const std::vector<std::string> &inArgs, const std::string &outArgs)
	{
		this->inArgs = inArgs;
		this->outArgs = outArgs;
		return *this;
	}

	//
	// Call the method
	//

	// Calls the method
	//
	// If a callback delegate has been set, then this method will call that delegate, otherwise this method will do nothing
	template<typename T>
	void call(GDBusConnection *pConnection, const DBusObjectPath &path, const std::string &interfaceName, const std::string &methodName, const std::string &notImplementedErrorName, GVariant *pParameters, GDBusMethodInvocation *pInvocation, void *pUserData) const
	{
		// This should never happen, but technically possible if instantiated with a nullptr for `callback`
		if (!callback && !handler)
		{
			Logger::error(SSTR << "DBusMethod contains no callback: [" << path << "]:[" << interfaceName << "]:[" << methodName << "]");
			g_dbus_method_invocation_return_dbus_error(pInvocation, notImplementedErrorName.c_str(), "This method is not implemented");
			return;
		}

		Logger::info(SSTR << "Calling method: [" << path << "]:[" << interfaceName << "]:[" << methodName << "]");
		try {
			if (handler)
			{
				handler(*static_cast<const T *>(pOwner), DBusConnectionRef(pConnection), methodName, DBusVariantRef(pParameters), DBusMethodInvocationRef(pInvocation), pUserData);
				return;
			}

			callback(*static_cast<const T *>(pOwner), pConnection, methodName, pParameters, pInvocation, pUserData);
		} catch (const std::exception& e) {
			Logger::error(SSTR << "DBusMethod::call: callback threw exception: " << e.what());
			g_dbus_method_invocation_return_dbus_error(pInvocation, "com.bzperi.Error.InternalError", e.what());
		} catch (...) {
			Logger::error("DBusMethod::call: callback threw unknown exception");
			g_dbus_method_invocation_return_dbus_error(pInvocation, "com.bzperi.Error.InternalError", "Unknown internal error");
		}
	}

	// Internal method used to generate introspection XML used to describe our services on D-Bus
	std::string generateIntrospectionXML(int depth) const;

private:
	const DBusInterface *pOwner;
	std::string name;
	std::vector<std::string> inArgs;
	std::string outArgs;
	RawCallback callback;
	Handler handler;
};

}; // namespace bzp
