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
// A GATT Property is simply a name/value pair.
//
// >>
// >>>  DISCUSSION
// >>
//
// See the discussion at the top of GattProperty.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <bzp/GLibTypes.h>
#include <functional>
#include <string>
#include <string_view>

namespace bzp {

struct DBusObjectPath;

namespace callbacks {
	using PropertyGetterHandler = std::function<DBusVariantRef(DBusConnectionRef, std::string_view, std::string_view, std::string_view, std::string_view, DBusErrorRef, void*)>;
	using PropertySetterHandler = std::function<bool(DBusConnectionRef, std::string_view, std::string_view, std::string_view, std::string_view, DBusVariantRef, DBusErrorRef, void*)>;
}

// Representation of a GATT Property
struct GattProperty
{
	using GetterHandler = callbacks::PropertyGetterHandler;
	using SetterHandler = callbacks::PropertySetterHandler;

	// Constructs a named property
	//
	// In general, properties should not be constructed directly as properties are typically instanticated by adding them to to an
	// interface using one of the the interface's `addProperty` methods.
	GattProperty(const std::string &name, GVariant *pValue, GDBusInterfaceGetPropertyFunc getter = nullptr, GDBusInterfaceSetPropertyFunc setter = nullptr);
	GattProperty(const std::string &name, DBusVariantRef value, GDBusInterfaceGetPropertyFunc getter = nullptr, GDBusInterfaceSetPropertyFunc setter = nullptr);
	GattProperty(const std::string &name, GVariant *pValue, const GetterHandler &getter, const SetterHandler &setter = {});
	GattProperty(const std::string &name, DBusVariantRef value, const GetterHandler &getter, const SetterHandler &setter = {});
	GattProperty(const GattProperty &other);
	GattProperty(GattProperty &&other) noexcept;
	GattProperty &operator=(const GattProperty &other);
	GattProperty &operator=(GattProperty &&other) noexcept;

	// Destructor — releases the GVariant reference
	~GattProperty();

	//
	// Name
	//

	// Returns the name of the property
	const std::string &getName() const;

	// Sets the name of the property
	//
	// In general, this method should not be called directly as properties are typically added to an interface using one of the the
	// interface's `addProperty` methods.
	GattProperty &setName(const std::string &name);

	//
	// Value
	//

	// Returns the property's value
	const GVariant *getValue() const;
	DBusVariantRef getValueRef() const;

	// Sets the property's value
	//
	// In general, this method should not be called directly as properties are typically added to an interface using one of the the
	// interface's `addProperty` methods.
	GattProperty &setValue(GVariant *pValue);
	GattProperty &setValue(DBusVariantRef value);

	//
	// Callbacks to get/set this property
	//

	// Internal use method to retrieve the getter delegate method used to return custom values for a property
	GDBusInterfaceGetPropertyFunc getGetterFunc() const;
	const GetterHandler &getGetterHandler() const;

	// Internal use method to set the getter delegate method used to return custom values for a property
	//
	// In general, this method should not be called directly as properties are typically added to an interface using one of the the
	// interface's `addProperty` methods.
	GattProperty &setGetterFunc(GDBusInterfaceGetPropertyFunc func);
	GattProperty &setGetterHandler(const GetterHandler &handler);

	// Internal use method to retrieve the setter delegate method used to return custom values for a property
	GDBusInterfaceSetPropertyFunc getSetterFunc() const;
	const SetterHandler &getSetterHandler() const;

	// Internal use method to set the setter delegate method used to return custom values for a property
	//
	// In general, this method should not be called directly as properties are typically added to an interface using one of the the
	// interface's `addProperty` methods.
	GattProperty &setSetterFunc(GDBusInterfaceSetPropertyFunc func);
	GattProperty &setSetterHandler(const SetterHandler &handler);

	// Internal method used to generate introspection XML used to describe our services on D-Bus
	std::string generateIntrospectionXML(int depth) const;

private:

	std::string name;
	GVariant *pValue;
	GDBusInterfaceGetPropertyFunc getterFunc;
	GDBusInterfaceSetPropertyFunc setterFunc;
	GetterHandler getterHandler;
	SetterHandler setterHandler;
};

}; // namespace bzp
