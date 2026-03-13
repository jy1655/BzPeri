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
// This is an abstraction of a D-Bus object.
//
// >>
// >>>  DISCUSSION
// >>
//
// See the discussino at the top of DBusObject.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <bzp/GLibTypes.h>
#include <string>
#include <list>
#include <memory>
#include <optional>
#include <functional>

#include <BzPeri.h>
#include <bzp/DBusObjectPath.h>

namespace bzp {

struct GattProperty;
struct GattService;
struct GattUuid;
struct DBusInterface;
struct Server;

struct DBusObject
{
	// A convenience typedef for describing our list of interface
	typedef std::list<std::shared_ptr<DBusInterface> > InterfaceList;

	// Construct a root object with no parent
	//
	// We'll include a publish flag since only root objects can be published
	DBusObject(Server &server, const DBusObjectPath &path, bool publish = true);

	// Construct a node object
	//
	// Nodes inherit their parent's publish path
	DBusObject(DBusObject *pParent, const DBusObjectPath &pathElement);

	//
	// Accessors
	//

	// Returns the `publish` flag
	bool isPublished() const;

	// Returns the path node for this object within the hierarchy
	//
	// This method only returns the node. To get the full path, use `getPath()`
	const DBusObjectPath &getPathNode() const;

	// Returns the full path for this object within the hierarchy
	//
	// This method returns the full path. To get the current node, use `getPathNode()`
	DBusObjectPath getPath() const;

	// Returns whether this object has a parent
	bool hasParent() const noexcept;

	// Returns the parent object in the hierarchy (nullopt if root)
	std::optional<std::reference_wrapper<DBusObject>> getParent();

	// Returns the list of children objects
	const std::list<DBusObject> &getChildren() const;

	// Returns the server data getter/setter associated with this object tree.
	BZPServerDataGetter getDataGetter() const noexcept;
	BZPServerDataSetter getDataSetter() const noexcept;
	Server& getServer() const;

	// Returns the logical service name used for annotations and D-Bus naming.
	const std::string &getServiceName() const;

	// Add a child to this object
	DBusObject &addChild(const DBusObjectPath &pathElement);

	// Returns a list of interfaces for this object
	const InterfaceList &getInterfaces() const;

	// Templated method for adding typed interfaces to the object
	template<typename T>
	std::shared_ptr<T> addInterface(std::shared_ptr<T> interface)
	{
		interfaces.push_back(interface);
		return std::static_pointer_cast<T>(interfaces.back());
	}

	// Internal method used to generate introspection XML used to describe our services on D-Bus
	std::string generateIntrospectionXML(int depth = 0) const;

	// Convenience functions to add a GATT service to the hierarchy
	//
	// We simply add a new child at the given path and add an interface configured as a GATT service to it using the given UUID.
	//
	// To end a service, call `gattServiceEnd()`
	GattService &gattServiceBegin(const std::string &pathElement, const GattUuid &uuid);

	//
	// Helpful routines for searching objects
	//

	// Finds an interface by name within this D-Bus object
	std::shared_ptr<const DBusInterface> findInterface(const DBusObjectPath &path, const std::string &interfaceName, const DBusObjectPath &basePath = DBusObjectPath()) const;

	// Finds a BlueZ method by name within the specified D-Bus interface
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use DBusObject::callMethod(..., DBusMethodCallRef)")
	bool callMethod(const DBusObjectPath &path, const std::string &interfaceName, const std::string &methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData, const DBusObjectPath &basePath = DBusObjectPath()) const;
#endif
	bool callMethod(const DBusObjectPath &path, const std::string &interfaceName, const std::string &methodName, DBusMethodCallRef methodCall, const DBusObjectPath &basePath = DBusObjectPath()) const;
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use DBusObject::callMethod(..., DBusMethodCallRef)")
	bool callMethod(const DBusObjectPath &path, const std::string &interfaceName, const std::string &methodName, DBusConnectionRef connection, DBusVariantRef parameters, DBusMethodInvocationRef invocation, gpointer pUserData, const DBusObjectPath &basePath = DBusObjectPath()) const;
#endif

	// -----------------------------------------------------------------------------------------------------------------------------
	// D-Bus signals
	// -----------------------------------------------------------------------------------------------------------------------------

	// Emits a signal and returns whether GLib accepted it for delivery.
	BZP_DEPRECATED("Use DBusObject::emitSignalChecked(DBusSignalRef)")
	bool emitSignalChecked(GDBusConnection *pBusConnection, const std::string &interfaceName, const std::string &signalName, GVariant *pParameters);
	BZP_DEPRECATED("Use DBusObject::emitSignalChecked(DBusSignalRef)")
	bool emitSignalChecked(DBusConnectionRef busConnection, const std::string &interfaceName, const std::string &signalName, DBusVariantRef parameters);
	bool emitSignalChecked(DBusSignalRef signal);

	// Emits a signal on the bus from the given path, interface name and signal name, containing a GVariant set of parameters
	BZP_DEPRECATED("Use DBusObject::emitSignal(DBusSignalRef)")
	void emitSignal(GDBusConnection *pBusConnection, const std::string &interfaceName, const std::string &signalName, GVariant *pParameters);
	BZP_DEPRECATED("Use DBusObject::emitSignal(DBusSignalRef)")
	void emitSignal(DBusConnectionRef busConnection, const std::string &interfaceName, const std::string &signalName, DBusVariantRef parameters);
	void emitSignal(DBusSignalRef signal);

private:
	Server *server_;
	bool publish;
	DBusObjectPath path;
	InterfaceList interfaces;
	std::list<DBusObject> children;
	DBusObject *pParent;
};

}; // namespace bzp
