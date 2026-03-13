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
	using PropertyGetterCallHandler = std::function<DBusVariantRef(DBusPropertyCallRef)>;
	using PropertySetterCallHandler = std::function<bool(DBusPropertyCallRef)>;
}

// Representation of a GATT Property
struct GattProperty
{
	using GetterHandler = callbacks::PropertyGetterHandler;
	using SetterHandler = callbacks::PropertySetterHandler;
	using GetterCallHandler = callbacks::PropertyGetterCallHandler;
	using SetterCallHandler = callbacks::PropertySetterCallHandler;

	// Constructs a named property
	//
	// In general, properties should not be constructed directly as properties are typically instanticated by adding them to to an
	// interface using one of the the interface's `addProperty` methods.
	GattProperty(const std::string &name, DBusVariantRef value);
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use GattProperty(const std::string&, DBusVariantRef) instead of raw GVariant* values")
	GattProperty(const std::string &name, GVariant *pValue);
	BZP_DEPRECATED("Use GattProperty wrapper getter/setter handlers or the constructor without raw GDBus property callbacks")
	GattProperty(const std::string &name, GVariant *pValue, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr);
	BZP_DEPRECATED("Use GattProperty wrapper getter/setter handlers or the constructor without raw GDBus property callbacks")
	GattProperty(const std::string &name, DBusVariantRef value, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr);
	BZP_DEPRECATED("Use GattProperty(const std::string&, DBusVariantRef, GetterHandler, SetterHandler) instead of raw GVariant* values")
	GattProperty(const std::string &name, GVariant *pValue, const GetterHandler &getter, const SetterHandler &setter = {});
	BZP_DEPRECATED("Use GattProperty(const std::string&, DBusVariantRef, GetterCallHandler, SetterCallHandler) instead of raw GVariant* values")
	GattProperty(const std::string &name, GVariant *pValue, const GetterCallHandler &getter, const SetterCallHandler &setter = {});
#endif
	GattProperty(const std::string &name, DBusVariantRef value, const GetterHandler &getter, const SetterHandler &setter = {});
	GattProperty(const std::string &name, DBusVariantRef value, const GetterCallHandler &getter, const SetterCallHandler &setter = {});
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
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use GattProperty::getValueRef() instead of raw GVariant* access")
	const GVariant *getValue() const;
#endif
	DBusVariantRef getValueRef() const;

	// Sets the property's value
	//
	// In general, this method should not be called directly as properties are typically added to an interface using one of the the
	// interface's `addProperty` methods.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use GattProperty::setValue(DBusVariantRef) instead of raw GVariant* values")
	GattProperty &setValue(GVariant *pValue);
#endif
	GattProperty &setValue(DBusVariantRef value);

	//
	// Callbacks to get/set this property
	//

	// Internal use method to retrieve the getter delegate method used to return custom values for a property
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use GattProperty::getGetterCallHandler() or getGetterHandler() instead of raw GDBus property callbacks")
	RawPropertyGetterCallback getGetterFunc() const;
#endif
	const GetterHandler &getGetterHandler() const;
	const GetterCallHandler &getGetterCallHandler() const;

	// Internal use method to set the getter delegate method used to return custom values for a property
	//
	// In general, this method should not be called directly as properties are typically added to an interface using one of the the
	// interface's `addProperty` methods.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use GattProperty::setGetterCallHandler() or setGetterHandler() instead of raw GDBus property callbacks")
	GattProperty &setGetterFunc(RawPropertyGetterCallback func);
#endif
	GattProperty &setGetterHandler(const GetterHandler &handler);
	GattProperty &setGetterCallHandler(const GetterCallHandler &handler);

	// Internal use method to retrieve the setter delegate method used to return custom values for a property
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use GattProperty::getSetterCallHandler() or getSetterHandler() instead of raw GDBus property callbacks")
	RawPropertySetterCallback getSetterFunc() const;
#endif
	const SetterHandler &getSetterHandler() const;
	const SetterCallHandler &getSetterCallHandler() const;

	// Internal use method to set the setter delegate method used to return custom values for a property
	//
	// In general, this method should not be called directly as properties are typically added to an interface using one of the the
	// interface's `addProperty` methods.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use GattProperty::setSetterCallHandler() or setSetterHandler() instead of raw GDBus property callbacks")
	GattProperty &setSetterFunc(RawPropertySetterCallback func);
#endif
	GattProperty &setSetterHandler(const SetterHandler &handler);
	GattProperty &setSetterCallHandler(const SetterCallHandler &handler);

	// Internal method used to generate introspection XML used to describe our services on D-Bus
	std::string generateIntrospectionXML(int depth) const;

private:

	std::string name;
	DBusVariantRef value_;
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	RawPropertyGetterCallback getterFunc;
	RawPropertySetterCallback setterFunc;
#endif
	GetterHandler getterHandler;
	SetterHandler setterHandler;
	GetterCallHandler getterCallHandler;
	SetterCallHandler setterCallHandler;
};

}; // namespace bzp
