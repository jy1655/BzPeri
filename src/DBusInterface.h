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
// This is an abstraction layer for a D-Bus interface, the base class for all interfaces.
//
// >>
// >>>  DISCUSSION
// >>
//
// See the discussion in DBusInterface.cpp.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <bzp/GLibTypes.h>
#include <string>
#include <list>

#include <bzp/DBusMethod.h>

namespace bzp {

// ---------------------------------------------------------------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------------------------------------------------------------

struct DBusInterface;
struct GattProperty;
struct DBusObject;
struct DBusObjectPath;

// ---------------------------------------------------------------------------------------------------------------------------------
// Useful Lambdas
// ---------------------------------------------------------------------------------------------------------------------------------

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
#define INTERFACE_METHOD_CALLBACK_LAMBDA [] \
( \
       const DBusInterface &self, \
       GDBusConnection *pConnection, \
       const std::string &methodName, \
       GVariant *pParameters, \
       GDBusMethodInvocation *pInvocation, \
       void *pUserData \
)
#endif

#define INTERFACE_METHOD_HANDLER_LAMBDA [] \
( \
       const DBusInterface &self, \
       DBusConnectionRef connection, \
       const std::string &methodName, \
       DBusVariantRef parameters, \
       DBusMethodInvocationRef invocation, \
       void *pUserData \
)

#define TRY_GET_INTERFACE_OF_TYPE(pInterface, type) \
	(pInterface->getInterfaceType() == type::kInterfaceType ? \
		std::static_pointer_cast<type>(pInterface) : \
		nullptr)

#define TRY_GET_CONST_INTERFACE_OF_TYPE(pInterface, type) \
	(pInterface->getInterfaceType() == type::kInterfaceType ? \
		std::static_pointer_cast<const type>(pInterface) : \
		nullptr)

// ---------------------------------------------------------------------------------------------------------------------------------
// Representation of a D-Bus interface
// ---------------------------------------------------------------------------------------------------------------------------------

struct DBusInterface
{
	// Our interface type
	static constexpr const char *kInterfaceType = "DBusInterface";

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	using RawMethodCallback = bzp::RawMethodCallback<DBusInterface>;
	using MethodCallback BZP_DEPRECATED("Use DBusInterface::MethodHandler and INTERFACE_METHOD_HANDLER_LAMBDA instead") = RawMethodCallback;
#endif
	using MethodHandler = DBusMethod::Handler;

	// Standard constructor
	DBusInterface(DBusObject &owner, const std::string &name);
	virtual ~DBusInterface();

	// Returns a string identifying the type of interface
	virtual const std::string getInterfaceType() const { return DBusInterface::kInterfaceType; }

	//
	// Interface name (ex: "org.freedesktop.DBus.Properties")
	//

	const std::string &getName() const;
	DBusInterface &setName(const std::string &name);

	//
	// Owner information
	//

	DBusObject &getOwner() const;
	DBusObjectPath getPathNode() const;
	DBusObjectPath getPath() const;

	//
	// D-Bus interface methods
	//

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use DBusInterface::addMethod(..., MethodHandler) instead of raw GDBus callbacks")
	DBusInterface &addMethod(const std::string &name, const char *pInArgs[], const char *pOutArgs, RawMethodCallback callback);
#endif
	DBusInterface &addMethod(const std::string &name, const char *pInArgs[], const char *pOutArgs, const MethodHandler &handler);

	// NOTE: Subclasses are encouraged to override this method in order to support different callback types that are specific to
	// their subclass type.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use DBusInterface::callMethod() wrapper overloads with DBusConnectionRef/DBusVariantRef/DBusMethodInvocationRef")
	virtual bool callMethod(const std::string &methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData) const;
#endif
	bool callMethod(const std::string &methodName, DBusConnectionRef connection, DBusVariantRef parameters, DBusMethodInvocationRef invocation, gpointer pUserData) const;

	// Internal method used to generate introspection XML used to describe our services on D-Bus
	virtual std::string generateIntrospectionXML(int depth) const;

protected:
	DBusObject &owner;
	std::string name;
	std::list<DBusMethod> methods;
};

}; // namespace bzp
