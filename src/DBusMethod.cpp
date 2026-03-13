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
// Methods are identified by their name (such as "ReadValue" or "WriteValue"). They have argument definitions (defined as part of
// their interface) that describe the type of arguments passed into the method and returned from the method.
//
// In addition to the method itself, we also store a callback delegate that is responsible for performing the tasks for this method.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <gio/gio.h>
#include <string>
#include <vector>

#include <bzp/DBusMethod.h>

namespace bzp {

// Instantiate a named method on a given interface (pOwner) with a given set of arguments and a callback delegate
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
DBusMethod::DBusMethod(const DBusInterface *pOwner, const std::string &name, const char *pInArgs[], const char *pOutArgs, RawCallback callback)
: pOwner(pOwner), name(name)
{
	const char **ppInArg = pInArgs;
	while(*ppInArg)
	{
		this->inArgs.push_back(std::string(*ppInArg));
		ppInArg++;
	}

	if (nullptr != pOutArgs)
	{
		this->outArgs = pOutArgs;
	}

	if (callback != nullptr)
	{
		callHandler = [callback](const DBusInterface &self, const std::string &methodName, DBusMethodCallRef methodCall) {
			callback(self, methodCall.connection().get(), methodName, methodCall.parameters().get(), methodCall.invocation().get(), methodCall.userData());
		};
	}
}
#endif

DBusMethod::DBusMethod(const DBusInterface *pOwner, const std::string &name, const char *pInArgs[], const char *pOutArgs, const Handler &handler)
: pOwner(pOwner), name(name)
{
	const char **ppInArg = pInArgs;
	while(*ppInArg)
	{
		this->inArgs.push_back(std::string(*ppInArg));
		ppInArg++;
	}

	if (nullptr != pOutArgs)
	{
		this->outArgs = pOutArgs;
	}

	if (handler != nullptr)
	{
		callHandler = [handler](const DBusInterface &self, const std::string &methodName, DBusMethodCallRef methodCall) {
			handler(self, methodCall.connection(), methodName, methodCall.parameters(), methodCall.invocation(), methodCall.userData());
		};
	}
}

DBusMethod::DBusMethod(const DBusInterface *pOwner, const std::string &name, const char *pInArgs[], const char *pOutArgs, const CallHandler &handler)
: pOwner(pOwner), name(name), callHandler(handler)
{
	const char **ppInArg = pInArgs;
	while(*ppInArg)
	{
		this->inArgs.push_back(std::string(*ppInArg));
		ppInArg++;
	}

	if (nullptr != pOutArgs)
	{
		this->outArgs = pOutArgs;
	}
}

// Internal method used to generate introspection XML used to describe our services on D-Bus
std::string DBusMethod::generateIntrospectionXML(int depth) const
{
	std::string prefix;
	prefix.insert(0, depth * 2, ' ');

	std::string xml = std::string();

	xml += prefix + "<method name='" + getName() + "'>\n";

	// Add our input arguments
	for (const std::string &inArg : getInArgs())
	{
		xml += prefix + "  <arg type='" + inArg + "' direction='in'>\n";
		xml += prefix + "    <annotation name='org.gtk.GDBus.C.ForceGVariant' value='true' />\n";
		xml += prefix + "  </arg>\n";
	}

	const std::string &outArgs = getOutArgs();
	if (!outArgs.empty())
	{
		xml += prefix + "  <arg type='" + outArgs + "' direction='out'>\n";
		xml += prefix + "    <annotation name='org.gtk.GDBus.C.ForceGVariant' value='true' />\n";
		xml += prefix + "  </arg>\n";
	}

	xml += prefix + "</method>\n";

	return xml;
}

}; // namespace bzp
