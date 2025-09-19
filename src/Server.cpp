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
// This file provides the core BzPeri server infrastructure, handling D-Bus object management, service
// registration, and server lifecycle. For service implementations, see the samples/ directory or create
// your own service configurators using the BzPeriConfigurator API.
//
// >>
// >>>  DISCUSSION
// >>
//
// The use of the term 'server', as it is used here, refers a collection of BlueZ services, characteristics & Descripors (plus
// a little more.)
//
// Our server needs to be described in two ways. Why two? Well, think about it like this: We're communicating with Bluetooth
// clients through BlueZ, and we're communicating with BlueZ through D-Bus. In essence, BlueZ and D-Bus are acting as tunnels, one
// inside the other.
//
// Here are those two descriptions in a bit more detail:
//
// 1. We need to describe ourselves as a citizen on D-Bus: The objects we implement, interfaces we provide, methods we handle, etc.
//
//    To accomplish this, we need to build an XML description (called an 'Introspection' for the curious readers) of our DBus
//    object hierarchy. The code for the XML generation starts in DBusObject.cpp (see `generateIntrospectionXML`) and carries on
//    throughout the other DBus* files (and even a few Gatt* files).
//
// 2. We also need to describe ourselves as a Bluetooth citizen: The services we provide, our characteristics and descriptors.
//
//    To accomplish this, BlueZ requires us to implement a standard D-Bus interface ('org.freedesktop.DBus.ObjectManager'). This
//    interface includes a D-Bus method 'GetManagedObjects', which is just a standardized way for somebody (say... BlueZ) to ask a
//    D-Bus entity (say... this server) to enumerate itself. This is how BlueZ figures out what services we offer. BlueZ will
//    essentially forward this information to Bluetooth clients.
//
// Although these two descriptions work at different levels, the two need to be kept in sync. In addition, we will also need to act
// on the messages we receive from our Bluetooth clients (through BlueZ, through D-Bus.) This means that we'll have yet another
// synchronization issue to resolve, which is to ensure that whatever has been asked of us, makes its way to the correct code in
// our description so we do the right thing.
//
// I don't know about you, but when dealing with data and the concepts "multiple" and "kept in sync" come into play, my spidey
// sense starts to tingle. The best way to ensure sychronization is to remove the need to keep things sychronized.
//
// BzPeri now uses a modular service configuration system. Services are defined using service configurators
// that are registered with the ServiceRegistry. This allows for clean separation of concerns and
// easier testing and maintenance. The fluent DSL interface is still used within configurators to
// define services, characteristics, and descriptors.
//
// >>
// >>>  MANAGING SERVER DATA
// >>
//
// The purpose of the server is to serve data. Your application is responsible for providing that data to the server via two data
// accessors (a getter and a setter) that implemented in the form of delegates that are passed into the `bzpStart()` method.
//
// While the server is running, if data is updated via a write operation from the client the setter delegate will be called. If your
// application also generates or updates data periodically, it can push those updates to the server via call to
// `bzpNofifyUpdatedCharacteristic()` or `bzpNofifyUpdatedDescriptor()`.
//
// >>
// >>>  UNDERSTANDING THE UNDERLYING FRAMEWORKS
// >>
//
// Service configurators use the fluent DSL interface to provide a GATT-based interface in terms of GATT
// services, characteristics and descriptors. Here's how services are typically defined in configurators:
//
//     .gattServiceBegin("text", "00000001-1E3C-FAD4-74E2-97A033F1BFAA")
//         .gattCharacteristicBegin("string", "00000002-1E3C-FAD4-74E2-97A033F1BFAA", {"read", "write", "notify"})
//
//             .onReadValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
//             {
//                 // Abbreviated for simplicity
//                 self.methodReturnValue(pInvocation, myTextString, true);
//             })
//
//             .onWriteValue(CHARACTERISTIC_METHOD_CALLBACK_LAMBDA
//             {
//                 // Abbreviated for simplicity
//                 myTextString = ...
//             })
//
//             .gattDescriptorBegin("description", "2901", {"read"})
//                 .onReadValue(DESCRIPTOR_METHOD_CALLBACK_LAMBDA
//                 {
//                     self.methodReturnValue(pInvocation, "Returns a test string", true);
//                 })
//
//             .gattDescriptorEnd()
//         .gattCharacteristicEnd()
//     .gattServiceEnd()
//
// The first thing you may notice abpout the sample is that all of the lines begin with a dot. This is because we're chaining
// methods together. Each method returns the appropriate type to provide context. For example, The `gattServiceBegin` method returns
// a reference to a `GattService` object which provides the proper context to create a characteristic within that service.
// Similarly, the `gattCharacteristicBegin` method returns a reference to a `GattCharacteristic` object which provides the proper
// context for responding to requests to read the characterisic value or add descriptors to the characteristic.
//
// For every `*Begin` method, there is a corresponding `*End` method, which returns us to the previous context. Indentation helps us
// keep track of where we are.
//
// Also note the use of the lambda macros, `CHARACTERISTIC_METHOD_CALLBACK_LAMBDA` and `DESCRIPTOR_METHOD_CALLBACK_LAMBDA`. These
// macros simplify the process of including our implementation directly in the description.
//
// The first parameter to each of the `*Begin` methods is a path node name. As we build our hierarchy, we give each node a name,
// which gets appended to it's parent's node (which in turns gets appended to its parent's node, etc.) If our root path was
// "/com/bzperi", then our service would have the path "/com/bzperi/text" and the characteristic would have the path
// "/com/bzperi/text/string", and the descriptor would have the path "/com/bzperi/text/string/description". These paths
// are important as they act like an addressing mechanism similar to paths on a filesystem or in a URL.
//
// The second parameter to each of the `*Begin` methods is a UUID as defined by the Bluetooth standard. These UUIDs effectively
// refer to an interface. You will see two different kinds of UUIDs: a short UUID ("2901") and a long UUID
// ("00000002-1E3C-FAD4-74E2-97A033F1BFAA").
//
// For more information on UUDSs, see GattUuid.cpp.
//
// In the example above, our non-standard UUIDs ("00000001-1E3C-FAD4-74E2-97A033F1BFAA") are something we generate ourselves. In the
// case above, we have created a custom service that simply stores a mutable text string. When the client enumerates our services
// they'll see this UUID and, assuming we've documented our interface behind this UUID for client authors, they can use our service
// to read and write a text string maintained on our server.
//
// The third parameter (which only applies to dharacteristics and descriptors) are a set of flags. You will find the current set of
// flags for characteristics and descriptors in the "BlueZ D-Bus GATT API description" at:
//
//     https://git.kernel.org/pub/scm/bluetooth/bluez.git/plain/doc/gatt-api.txt
//
// In addition to these structural methods, there are a small handful of helper methods for performing common operations. These
// helper methods are available within a method (such as `onReadValue`) through the use of a `self` reference. The `self` reference
// refers to the object at which the method is invoked (either a `GattCharacteristic` object or a `GattDescriptor` object.)
//
//     methodReturnValue and methodReturnVariant
//         These methods provide a means for returning values from Characteristics and Descriptors. The `-Value` form accept a set
//         of common types (int, string, etc.) If you need to provide a custom return type, you can do so by building your own
//         GVariant (which is a GLib construct) and using the `-Variant` form of the method.
//
//     sendChangeNotificationValue and sendChangeNotificationVariant
//         These methods provide a means for notifying changes for Characteristics. The `-Value` form accept a set of common types
//         (int, string, etc.) If you need to notify a custom return type, you can do so by building your own GVariant (which is a
//         GLib construct) and using the `-Variant` form of the method.
//
// For information about GVariants (what they are and how to work with them), see the GLib documentation at:
//
//     https://www.freedesktop.org/software/gstreamer-sdk/data/docs/latest/glib/glib-GVariantType.html
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <algorithm>

#include "../include/bzp/Server.h"
#include "ServerUtils.h"
#include "../include/bzp/Utils.h"
#include "../include/bzp/Globals.h"
#include "../include/bzp/DBusObject.h"
#include "../include/bzp/DBusInterface.h"
#include "../include/bzp/GattProperty.h"
#include "../include/bzp/GattService.h"
#include "../include/bzp/GattUuid.h"
#include "../include/bzp/GattCharacteristic.h"
#include "../include/bzp/GattDescriptor.h"
#include "Logger.h"

namespace bzp {

// There's a good chance there will be a bunch of unused parameters from the lambda macros
#if defined(__GNUC__) && defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
#if defined(__GNUC__) && !defined(__clang__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

// ---------------------------------------------------------------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------------------------------------------------------------

// Our one and only server. It's global.
std::shared_ptr<Server> TheServer = nullptr;

// ---------------------------------------------------------------------------------------------------------------------------------
// Object implementation
// ---------------------------------------------------------------------------------------------------------------------------------

// Our constructor builds our entire server description
//
// serviceName: The name of our server (collectino of services)
//
//     This is used to build the path for our Bluetooth services. It also provides the base for the D-Bus owned name (see
//     getOwnedName.)
//
//     This value will be stored as lower-case only.
//
//     Retrieve this value using the `getName()` method.
//
// advertisingName: The name for this controller, as advertised over LE
//
//     IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
//     BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
//     name.
//
//     Retrieve this value using the `getAdvertisingName()` method.
//
// advertisingShortName: The short name for this controller, as advertised over LE
//
//     According to the spec, the short name is used in case the full name doesn't fit within Extended Inquiry Response (EIR) or
//     Advertising Data (AD).
//
//     IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
//     BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
//     name.
//
//     Retrieve this value using the `getAdvertisingShortName()` method.
//
// enableBondable: Enable or disable device bonding/pairing capability
//
//     When true (default), the adapter will accept pairing requests from client devices and allow them to bond.
//     When false, pairing requests will be rejected, which may cause immediate disconnection for devices that
//     require security/authentication.
//
//     Modern BLE applications typically require bonding for security, so this should generally be left as true
//     unless you specifically need an open, non-authenticated connection.
//
//     Retrieve this value using the `getEnableBondable()` method.
//
Server::Server(const std::string &serviceName, const std::string &advertisingName, const std::string &advertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, bool enableBondable)
{
	// Validate and save service name
	std::string lowerServiceName = serviceName;
	std::transform(lowerServiceName.begin(), lowerServiceName.end(), lowerServiceName.begin(), ::tolower);

	// Enforce com.bzperi namespace for D-Bus compatibility
	if (lowerServiceName != "bzperi" && lowerServiceName.find("bzperi.") != 0) {
		throw std::invalid_argument("Service name must be 'bzperi' or start with 'bzperi.' (e.g., 'bzperi.myapp')");
	}

	this->serviceName = lowerServiceName;
	this->advertisingName = advertisingName;
	this->advertisingShortName = advertisingShortName;

	// Register getter & setter for server data
	dataGetter = getter;
	dataSetter = setter;

	// Adapter configuration flags - set these flags based on how you want the adapter configured
	enableBREDR = false;
	enableSecureConnection = false;
	enableConnectable = true;
	enableDiscoverable = true;
	enableAdvertising = true;
	this->enableBondable = enableBondable;  // Use parameter value instead of hardcoded false

	//
	// Define the server
	//

	// Create the root D-Bus object and push it into the list
	// Convert dots in service name to slashes for valid D-Bus object path
	// e.g., "bzperi.myapp" becomes "/com/bzperi/myapp"
	std::string pathServiceName = getServiceName();
	std::replace(pathServiceName.begin(), pathServiceName.end(), '.', '/');
	objects.push_back(DBusObject(DBusObjectPath() + "com" + pathServiceName));

	// Cache the root node for downstream configurators (example services, application-defined services, etc.)
	rootObject = &objects.back();

	// No GATT services are installed here. Consumers can register configurators that will populate the hierarchy
	// using ServiceRegistry prior to the server thread being launched.

	//  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -
	//                                                ____ _____ ___  _____
	//                                               / ___|_   _/ _ \|  _  |
	//                                               \___ \ | || | | | |_) |
	//                                                ___) || || |_| |  __/
	//                                               |____/ |_| \___/|_|
	//
	// You probably shouldn't mess with stuff beyond this point. It is required to meet BlueZ's requirements for a GATT Service.
	//
	// >>
	// >>  WHAT IT IS
	// >>
	//
	// From the BlueZ D-Bus GATT API description (https://git.kernel.org/pub/scm/bluetooth/bluez.git/plain/doc/gatt-api.txt):
	//
	//     "To make service registration simple, BlueZ requires that all objects that belong to a GATT service be grouped under a
	//     D-Bus Object Manager that solely manages the objects of that service. Hence, the standard DBus.ObjectManager interface
	//     must be available on the root service path."
	//
	// The code below does exactly that. Notice that we're doing much of the same work that our Server description does except that
	// instead of defining our own interfaces, we're following a pre-defined standard.
	//
	// The object types and method names used in the code below may look unfamiliar compared to what you're used to seeing in the
	// Server desecription. That's because the server description uses higher level types that define a more GATT-oriented framework
	// to build your GATT services. That higher level functionality was built using a set of lower-level D-Bus-oriented framework,
	// which is used in the code below.
	//  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -

	// Create the root object and push it into the list. We're going to build off of this object, so we need to get a reference
	// to the instance of the object as it resides in the list (and not the object that would be added to the list.)
	//
	// This is a non-published object (as specified by the 'false' parameter in the DBusObject constructor.) This way, we can
	// include this within our server hieararchy (i.e., within the `objects` list) but it won't be exposed by BlueZ as a Bluetooth
	// service to clietns.
	objects.push_back(DBusObject(DBusObjectPath(), false));

	// Get a reference to the new object as it resides in the list
	DBusObject &objectManager = objects.back();

	// Create an interface of the standard type 'org.freedesktop.DBus.ObjectManager'
	//
	// See: https://dbus.freedesktop.org/doc/dbus-specification.html#standard-interfaces-objectmanager
	auto omInterface = std::make_shared<DBusInterface>(objectManager, "org.freedesktop.DBus.ObjectManager");

	// Add the interface to the object manager
	objectManager.addInterface(omInterface);

	// Finally, we setup the interface. We do this by adding the `GetManagedObjects` method as specified by D-Bus for the
	// 'org.freedesktop.DBus.ObjectManager' interface.
	const char *pInArgs[] = { nullptr };
	const char *pOutArgs = "a{oa{sa{sv}}}";
	omInterface->addMethod("GetManagedObjects", pInArgs, pOutArgs, [](const DBusInterface& self, GDBusConnection* pConnection, const std::string& methodName, GVariant* pParameters, GDBusMethodInvocation* pInvocation, void* pUserData) {
		ServerUtils::getManagedObjects(pInvocation);
	});

}


// ---------------------------------------------------------------------------------------------------------------------------------
// Utilitarian
// ---------------------------------------------------------------------------------------------------------------------------------

void Server::configure(const std::function<void(DBusObject&)>& builder)
{
	if (!builder || rootObject == nullptr)
	{
		return;
	}

	builder(*rootObject);
}

// Find a D-Bus interface within the given D-Bus object
//
// If the interface was found, it is returned, otherwise nullptr is returned
std::shared_ptr<const DBusInterface> Server::findInterface(const DBusObjectPath &objectPath, std::string_view interfaceName) const
{
	for (const DBusObject &object : objects)
	{
		std::shared_ptr<const DBusInterface> pInterface = object.findInterface(objectPath, std::string(interfaceName));
		if (pInterface != nullptr)
		{
			return pInterface;
		}
	}

	return nullptr;
}

// Find and call a D-Bus method within the given D-Bus object on the given D-Bus interface
//
// If the method was called, this method returns true, otherwise false. There is no result from the method call itself.
bool Server::callMethod(const DBusObjectPath &objectPath, std::string_view interfaceName, std::string_view methodName, GDBusConnection *pConnection, GVariant *pParameters, GDBusMethodInvocation *pInvocation, gpointer pUserData) const
{
	for (const DBusObject &object : objects)
	{
		if (object.callMethod(objectPath, std::string(interfaceName), std::string(methodName), pConnection, pParameters, pInvocation, pUserData))
		{
			return true;
		}
	}

	return false;
}

// Find a GATT Property within the given D-Bus object on the given D-Bus interface
//
// If the property was found, it is returned, otherwise nullptr is returned
const GattProperty *Server::findProperty(const DBusObjectPath &objectPath, std::string_view interfaceName, std::string_view propertyName) const
{
	std::shared_ptr<const DBusInterface> pInterface = findInterface(objectPath, interfaceName);

	// Try each of the GattInterface types that support properties?
	if (std::shared_ptr<const GattInterface> pGattInterface = TRY_GET_CONST_INTERFACE_OF_TYPE(pInterface, GattInterface))
	{
		return pGattInterface->findProperty(std::string(propertyName));
	}
	else if (std::shared_ptr<const GattService> pGattInterface = TRY_GET_CONST_INTERFACE_OF_TYPE(pInterface, GattService))
	{
		return pGattInterface->findProperty(std::string(propertyName));
	}
	else if (std::shared_ptr<const GattCharacteristic> pGattInterface = TRY_GET_CONST_INTERFACE_OF_TYPE(pInterface, GattCharacteristic))
	{
		return pGattInterface->findProperty(std::string(propertyName));
	}

	return nullptr;
}

}; // namespace bzp
