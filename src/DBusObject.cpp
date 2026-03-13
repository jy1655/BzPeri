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
// A D-Bus object is a container for any number of functional interfaces to expose on the bus. Objects are referred to by their
// path ("/com/acme/widgets"). Here is a simple example of how D-Bus objects relate to Bluetooth services:
//
// Object (path)                               Interface (name)
//
// /com/acme/widget                            org.bluez.GattService1
// /com/acme/widget/manufacturer_name          org.bluez.GattCharacteristic1
// /com/acme/widget/serial_number              org.bluez.GattCharacteristic1
//
// In English, this would be read as "The Acme company has a widget, which has two characteristics defining the manufacturer name
// and serial number for the widget."
//
// Finally, we'll include a published flag. Here's what that's all about:
//
// BlueZ uses the GetManagedObjects method (from the org.freedesktop.DBus.ObjectManager interface) to interrogate our
// service(s). Our Server, however, includes all objects and interfaces, including the GetManagedObjects as well as the various
// interfaces we expose over Bluetooth. Therefore, we'll need a way to know which ones to expose over Bluetooth (which is, in
// general, everything EXCEPT the object containing the org.freedesktop.DBus.ObjectManager interface.) Since we manage our
// objects in a hierarchy, only the root object's publish flag matters.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <bzp/GattProperty.h>
#include <bzp/DBusInterface.h>
#include <bzp/GattService.h>
#include <bzp/DBusObject.h>
#include <bzp/Utils.h>
#include <bzp/GattUuid.h>
#include <bzp/Logger.h>
#include <bzp/Server.h>

namespace bzp {

// Construct a root object with no parent
//
// We'll include a publish flag since only root objects can be published
DBusObject::DBusObject(Server &server, const DBusObjectPath &path, bool publish)
: server_(&server), publish(publish), path(path), pParent(nullptr)
{
}

// Construct a node object
//
// Nodes inherit their parent's publish path
DBusObject::DBusObject(DBusObject *pParent, const DBusObjectPath &pathElement)
: server_(pParent->server_), publish(pParent->publish), path(pathElement), pParent(pParent)
{
}

//
// Accessors
//

// Returns the `publish` flag
bool DBusObject::isPublished() const
{
	return publish;
}

// Returns the path node for this object within the hierarchy
//
// This method only returns the node. To get the full path, use `getPath()`
const DBusObjectPath &DBusObject::getPathNode() const
{
	return path;
}

// Returns the full path for this object within the hierarchy
//
// This method returns the full path. To get the current node, use `getPathNode()`
DBusObjectPath DBusObject::getPath() const
{
	DBusObjectPath path = getPathNode();
	const DBusObject *pCurrent = pParent;

	// Traverse up my chain, adding nodes to the path until we have the full thing
	while(nullptr != pCurrent)
	{
		path = pCurrent->getPathNode() + path;
		pCurrent = pCurrent->pParent;
	}

	return path;
}

// Returns whether this object has a parent
bool DBusObject::hasParent() const noexcept
{
	return pParent != nullptr;
}

// Returns the parent object in the hierarchy (nullopt if root)
std::optional<std::reference_wrapper<DBusObject>> DBusObject::getParent()
{
	if (pParent == nullptr) return std::nullopt;
	return std::ref(*pParent);
}

// Returns the list of children objects
const std::list<DBusObject> &DBusObject::getChildren() const
{
	return children;
}

BZPServerDataGetter DBusObject::getDataGetter() const noexcept
{
	return server_ != nullptr ? server_->getDataGetter() : nullptr;
}

BZPServerDataSetter DBusObject::getDataSetter() const noexcept
{
	return server_ != nullptr ? server_->getDataSetter() : nullptr;
}

Server& DBusObject::getServer() const
{
	return *server_;
}

const std::string &DBusObject::getServiceName() const
{
	return server_->getServiceName();
}

// Add a child to this object
DBusObject &DBusObject::addChild(const DBusObjectPath &pathElement)
{
	children.push_back(DBusObject(this, pathElement));
	return children.back();
}

// Returns a list of interfaces for this object
const DBusObject::InterfaceList &DBusObject::getInterfaces() const
{
	return interfaces;
}

// Convenience functions to add a GATT service to the hierarchy
//
// We simply add a new child at the given path and add an interface configured as a GATT service to it using the given UUID.
GattService &DBusObject::gattServiceBegin(const std::string &pathElement, const GattUuid &uuid)
{
	DBusObject &child = addChild(DBusObjectPath(pathElement));
	GattService &service = *child.addInterface(std::make_shared<GattService>(child, "org.bluez.GattService1"));
	service.addProperty<GattService>("UUID", uuid);
	service.addProperty<GattService>("Primary", true);
	return service;
}

//
// Helpful routines for searching objects
//

// Finds an interface by name within this D-Bus object
std::shared_ptr<const DBusInterface> DBusObject::findInterface(const DBusObjectPath &path, const std::string &interfaceName, const DBusObjectPath &basePath) const
{
	if ((basePath + getPathNode()) == path)
	{
		for (std::shared_ptr<const DBusInterface> interface : interfaces)
		{
			if (interfaceName == interface->getName())
			{
				return interface;
			}
		}
	}

	for (const DBusObject &child : getChildren())
	{
		std::shared_ptr<const DBusInterface> pInterface = child.findInterface(path, interfaceName, basePath + getPathNode());
		if (nullptr != pInterface)
		{
			return pInterface;
		}
	}

	return nullptr;
}

// Finds a BlueZ method by name within the specified D-Bus interface
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
bool DBusObject::callMethod(const DBusObjectPath &path, const std::string &interfaceName, const std::string &methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData, const DBusObjectPath &basePath) const
{
	return callMethod(path, interfaceName, methodName, DBusMethodCallRef(pConnection, pParameters, pInvocation, pUserData), basePath);
}
#endif

bool DBusObject::callMethod(const DBusObjectPath &path, const std::string &interfaceName, const std::string &methodName, DBusMethodCallRef methodCall, const DBusObjectPath &basePath) const
{
	if ((basePath + getPathNode()) == path)
	{
		for (std::shared_ptr<const DBusInterface> interface : interfaces)
		{
			if (interfaceName == interface->getName())
			{
				if (interface->callMethod(methodName, methodCall))
				{
					return true;
				}
			}
		}
	}

	for (const DBusObject &child : getChildren())
	{
		if (child.callMethod(path, interfaceName, methodName, methodCall, basePath + getPathNode()))
		{
			return true;
		}
	}

	return false;
}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
bool DBusObject::callMethod(const DBusObjectPath &path, const std::string &interfaceName, const std::string &methodName, DBusConnectionRef connection, DBusVariantRef parameters, DBusMethodInvocationRef invocation, gpointer pUserData, const DBusObjectPath &basePath) const
{
	return callMethod(path, interfaceName, methodName, DBusMethodCallRef(connection, parameters, invocation, pUserData), basePath);
}
#endif

// ---------------------------------------------------------------------------------------------------------------------------------
// XML generation for a D-Bus introspection
// ---------------------------------------------------------------------------------------------------------------------------------

// Internal method used to generate introspection XML used to describe our services on D-Bus
std::string DBusObject::generateIntrospectionXML(int depth) const
{
	std::string prefix;
	prefix.insert(0, depth * 2, ' ');

	std::string xml = std::string();

	if (depth == 0)
	{
		xml += "<?xml version='1.0'?>\n";
		xml += "<!DOCTYPE node PUBLIC '-//freedesktop//DTD D-BUS Object Introspection 1.0//EN' 'http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd'>\n";
	}

	xml += prefix + "<node name='" + getPathNode().toString() + "'>\n";
	xml += prefix + "  <annotation name='" + getServiceName() + ".DBusObject.path' value='" + getPath().toString() + "' />\n";

	for (std::shared_ptr<const DBusInterface> interface : interfaces)
	{
		xml += interface->generateIntrospectionXML(depth + 1);
	}

	for (DBusObject child : getChildren())
	{
		xml += child.generateIntrospectionXML(depth + 1);
	}

	xml += prefix + "</node>\n";

	if (depth == 0)
	{
		Logger::debug("Generated XML:");
		Logger::debug(xml);
	}

	return xml;
}

// ---------------------------------------------------------------------------------------------------------------------------------
// D-Bus signals
// ---------------------------------------------------------------------------------------------------------------------------------

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
bool DBusObject::emitSignalChecked(GDBusConnection *pBusConnection, const std::string &interfaceName, const std::string &signalName, GVariant *pParameters)
{
	return emitSignalChecked(DBusSignalRef(DBusConnectionRef(pBusConnection), interfaceName, signalName, DBusVariantRef(pParameters)));
}
#endif

bool DBusObject::emitSignalChecked(DBusConnectionRef busConnection, const std::string &interfaceName, const std::string &signalName, DBusVariantRef parameters)
{
	return emitSignalChecked(DBusSignalRef(busConnection, interfaceName, signalName, parameters));
}

bool DBusObject::emitSignalChecked(DBusSignalRef signal)
{
	GError *pError = nullptr;
	const std::string interfaceName(signal.interfaceName());
	const std::string signalName(signal.signalName());
	gboolean result = g_dbus_connection_emit_signal
	(
		signal.connection().get(), // GDBusConnection *connection
		NULL,                    // const gchar *destination_bus_name
		getPath().c_str(),       // const gchar *object_path
		interfaceName.c_str(),   // const gchar *interface_name
		signalName.c_str(),      // const gchar *signal_name
		signal.parameters().get(), // GVariant *parameters
		&pError                  // GError **error
	);

	if (0 == result)
	{
		Logger::error(SSTR << "Failed to emit signal named '" << signalName << "': " << (nullptr == pError ? "Unknown" : pError->message));
		if (nullptr != pError)
		{
			g_error_free(pError);
		}
		return false;
	}

	if (nullptr != pError)
	{
		g_error_free(pError);
	}

	return true;
}

// Emits a signal on the bus from the given path, interface name and signal name, containing a GVariant set of parameters
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
void DBusObject::emitSignal(GDBusConnection *pBusConnection, const std::string &interfaceName, const std::string &signalName, GVariant *pParameters)
{
	(void)emitSignalChecked(DBusSignalRef(DBusConnectionRef(pBusConnection), interfaceName, signalName, DBusVariantRef(pParameters)));
}
#endif

void DBusObject::emitSignal(DBusConnectionRef busConnection, const std::string &interfaceName, const std::string &signalName, DBusVariantRef parameters)
{
	(void)emitSignalChecked(DBusSignalRef(busConnection, interfaceName, signalName, parameters));
}

void DBusObject::emitSignal(DBusSignalRef signal)
{
	(void)emitSignalChecked(signal);
}


}; // namespace bzp
