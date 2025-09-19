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
// This is the top-level interface for the server. There is only one of these stored in the global `TheServer`. Use this object
// to configure your server's settings (there are surprisingly few of them.) It also contains the full server description and
// implementation.
//
// >>
// >>>  DISCUSSION
// >>
//
// See the discussion at the top of Server.cpp
//
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <gio/gio.h>
#include <string>
#include <string_view>
#include <vector>
#include <list>
#include <memory>
#include <span>
#include <functional>

#include "../include/BzPeri.h"
#include <bzp/DBusObject.h>

namespace bzp {

//
// Forward declarations
//

struct GattProperty;
struct GattCharacteristic;
struct DBusInterface;
struct DBusObjectPath;

//
// Implementation
//

struct Server
{
	//
	// Types
	//

	// Our server is a collection of D-Bus objects
	using Objects = std::list<DBusObject>;

	//
	// Accessors
	//

	// Returns the set of objects that each represent the root of an object tree describing a group of services we are providing
	const Objects& getObjects() const noexcept { return objects; }

	// Returns the root object for the server's D-Bus hierarchy
	DBusObject& getRootObject() noexcept { return *rootObject; }

	// Configure the server using a builder callback to mutate the root hierarchy
	void configure(const std::function<void(DBusObject&)>& builder);

	// Returns the requested setting for BR/EDR (true = enabled, false = disabled)
	[[nodiscard]] bool getEnableBREDR() const noexcept { return enableBREDR; }

	// Returns the requested setting for secure connections (true = enabled, false = disabled)
	[[nodiscard]] bool getEnableSecureConnection() const noexcept { return enableSecureConnection; }

	// Returns the requested setting the connectable state (true = enabled, false = disabled)
	[[nodiscard]] bool getEnableConnectable() const noexcept { return enableConnectable; }

	// Returns the requested setting the discoverable state (true = enabled, false = disabled)
	[[nodiscard]] bool getEnableDiscoverable() const noexcept { return enableDiscoverable; }

	// Returns the requested setting the LE advertising state (true = enabled, false = disabled)
	[[nodiscard]] bool getEnableAdvertising() const noexcept { return enableAdvertising; }

	// Returns the requested setting the bondable state (true = enabled, false = disabled)
	[[nodiscard]] bool getEnableBondable() const noexcept { return enableBondable; }

	// Returns our registered data getter
	[[nodiscard]] BZPServerDataGetter getDataGetter() const noexcept { return dataGetter; }

	// Returns our registered data setter
	[[nodiscard]] BZPServerDataSetter getDataSetter() const noexcept { return dataSetter; }

	// advertisingName: The name for this controller, as advertised over LE
	//
	// This is set from the constructor.
	//
	// IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
	// BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
	// name.
	[[nodiscard]] const std::string& getAdvertisingName() const noexcept { return advertisingName; }

	// advertisingShortName: The short name for this controller, as advertised over LE
	//
	// According to the spec, the short name is used in case the full name doesn't fit within Extended Inquiry Response (EIR) or
	// Advertising Data (AD).
	//
	// This is set from the constructor.
	//
	// IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
	// BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
	// name.
	[[nodiscard]] const std::string& getAdvertisingShortName() const noexcept { return advertisingShortName; }

	// serviceName: The name of our server (collectino of services)
	//
	// This is set from the constructor.
	//
	// This is used to build the path for our Bluetooth services (and we'll go ahead and use it as the owned name as well for
	// consistency.)
	[[nodiscard]] const std::string& getServiceName() const noexcept { return serviceName; }

	// Our owned name
	//
	// D-Bus uses owned names to locate servers on the bus. Think of this as a namespace within D-Bus. We building this with the
	// server name to keep things simple.
	[[nodiscard]] std::string getOwnedName() const { return std::string("com.") + getServiceName(); }

	//
	// Initialization
	//

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
	Server(const std::string &serviceName, const std::string &advertisingName, const std::string &advertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter, bool enableBondable = true);

	//
	// Utilitarian
	//

	// Find and call a D-Bus method within the given D-Bus object on the given D-Bus interface
	//
	// If the method was called, this method returns true, otherwise false.  There is no result from the method call itself.
	[[nodiscard]] std::shared_ptr<const DBusInterface> findInterface(const DBusObjectPath& objectPath, std::string_view interfaceName) const;

	// Find a D-Bus method within the given D-Bus object on the given D-Bus interface
	//
	// If the method was found, it is returned, otherwise nullptr is returned
	[[nodiscard]] bool callMethod(const DBusObjectPath& objectPath, std::string_view interfaceName, std::string_view methodName, GDBusConnection* pConnection, GVariant* pParameters, GDBusMethodInvocation* pInvocation, gpointer pUserData) const;

	// Find a GATT Property within the given D-Bus object on the given D-Bus interface
	//
	// If the property was found, it is returned, otherwise nullptr is returned
	[[nodiscard]] const GattProperty* findProperty(const DBusObjectPath& objectPath, std::string_view interfaceName, std::string_view propertyName) const;

private:

	// Our server's objects
	Objects objects;

	// Cached pointer to the root object for convenient access during configuration
	DBusObject* rootObject = nullptr;

	// BR/EDR requested state
	bool enableBREDR;

	// Secure connection requested state
	bool enableSecureConnection;

	// Connectable requested state
	bool enableConnectable;

	// Discoverable requested state
	bool enableDiscoverable;

	// LE advertising requested state
	bool enableAdvertising;

	// Bondable requested state
	bool enableBondable;

	// The getter callback that is responsible for returning current server data that is shared over Bluetooth
	BZPServerDataGetter dataGetter;

	// The setter callback that is responsible for storing current server data that is shared over Bluetooth
	BZPServerDataSetter dataSetter;

	// advertisingName: The name for this controller, as advertised over LE
	//
	// This is set from the constructor.
	//
	// IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
	// BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
	// name.
	std::string advertisingName;

	// advertisingShortName: The short name for this controller, as advertised over LE
	//
	// According to the spec, the short name is used in case the full name doesn't fit within Extended Inquiry Response (EIR) or
	// Advertising Data (AD).
	//
	// This is set from the constructor.
	//
	// IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
	// BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
	// name.
	std::string advertisingShortName;

	// serviceName: The name of our server (collectino of services)
	//
	// This is set from the constructor.
	//
	// This is used to build the path for our Bluetooth services (and we'll go ahead and use it as the owned name as well for
	// consistency.)
	std::string serviceName;
};

// Our one and only server. It's a global.
extern std::shared_ptr<Server> TheServer;

}; // namespace bzp
