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
// This is our abstraction layer for GATT interfaces, used by GattService, GattCharacteristic & GattDescriptor
//
// >>
// >>>  DISCUSSION
// >>
//
// See the discussion at the top of GattInterface.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <bzp/GLibTypes.h>
#include <string>
#include <list>

#include <bzp/DBusInterface.h>
#include <bzp/DBusObject.h>
#include <bzp/GattProperty.h>
#include <bzp/GattUuid.h>
#include <bzp/Utils.h>

namespace bzp {

// ---------------------------------------------------------------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------------------------------------------------------------

struct GattInterface;
struct DBusObject;

// ---------------------------------------------------------------------------------------------------------------------------------
// Pure virtual representation of a Bluetooth GATT Interface, the base class for Services, Characteristics and Descriptors
// ---------------------------------------------------------------------------------------------------------------------------------

struct GattInterface : DBusInterface
{
	// Standard constructor
	GattInterface(DBusObject &owner, const std::string &name);
	virtual ~GattInterface();

	// Returns a string identifying the type of interface
	virtual const std::string getInterfaceType() const = 0;

	//
	// GATT Characteristic properties
	//

	// Returns the list of GATT properties
	const std::list<GattProperty> &getProperties() const;

	// Add a `GattProperty` to the interface
	//
	// There are helper methods for adding properties for common types as well as a generalized helper method for adding a
	// `GattProperty` of a generic GVariant * type.
	template<typename T>
	T &addProperty(const GattProperty &property)
	{
		properties.push_back(property);
		return *static_cast<T *>(this);
	}

	// Add a named property with a GVariant *
	//
	// There are helper methods for common types (UUIDs, strings, boolean, etc.) Use this method when no helper method exists for
	// the type you want to use. There is also a helper method for adding a named property of a pre-built `GattProperty`.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with DBusVariantRef instead of raw GVariant* values")
	T &addProperty(const std::string &name, GVariant *pValue)
	{
		return addProperty<T>(GattProperty(name, pValue));
	}

	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with DBusVariantRef and wrapper getter/setter handlers")
	T &addProperty(const std::string &name, GVariant *pValue, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr)
	{
		return addProperty<T>(GattProperty(name, pValue, getter, setter));
	}

	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with DBusVariantRef and wrapper handlers instead of raw GVariant* values")
	T &addProperty(const std::string &name, GVariant *pValue, const GattProperty::GetterHandler &getter, const GattProperty::SetterHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, pValue, getter, setter));
	}

	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with DBusVariantRef and call handlers instead of raw GVariant* values")
	T &addProperty(const std::string &name, GVariant *pValue, const GattProperty::GetterCallHandler &getter, const GattProperty::SetterCallHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, pValue, getter, setter));
	}
#endif

	template<typename T>
	T &addProperty(const std::string &name, DBusVariantRef value)
	{
		return addProperty<T>(GattProperty(name, value));
	}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with DBusVariantRef and wrapper getter/setter handlers")
	T &addProperty(const std::string &name, DBusVariantRef value, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr)
	{
		return addProperty<T>(GattProperty(name, value, getter, setter));
	}
#endif

	template<typename T>
	T &addProperty(const std::string &name, DBusVariantRef value, const GattProperty::GetterHandler &getter, const GattProperty::SetterHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, value, getter, setter));
	}

	template<typename T>
	T &addProperty(const std::string &name, DBusVariantRef value, const GattProperty::GetterCallHandler &getter, const GattProperty::SetterCallHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, value, getter, setter));
	}

	// Helper method for adding a named property with a `GattUuid`
	template<typename T>
	T &addProperty(const std::string &name, const GattUuid &uuid)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromString(uuid.toString128().c_str())));
	}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with wrapper getter/setter handlers")
	T &addProperty(const std::string &name, const GattUuid &uuid, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromString(uuid.toString128().c_str()), getter, setter));
	}
#endif

	template<typename T>
	T &addProperty(const std::string &name, const GattUuid &uuid, const GattProperty::GetterHandler &getter, const GattProperty::SetterHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromString(uuid.toString128().c_str()), getter, setter));
	}

	// Helper method for adding a named property with a `DBusObjectPath`
	template<typename T>
	T &addProperty(const std::string &name, const DBusObjectPath &path)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromObject(path)));
	}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with wrapper getter/setter handlers")
	T &addProperty(const std::string &name, const DBusObjectPath &path, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromObject(path), getter, setter));
	}
#endif

	template<typename T>
	T &addProperty(const std::string &name, const DBusObjectPath &path, const GattProperty::GetterHandler &getter, const GattProperty::SetterHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromObject(path), getter, setter));
	}

	// Helper method for adding a named property with a std::strings
	template<typename T>
	T &addProperty(const std::string &name, const std::string &str)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromString(str)));
	}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with wrapper getter/setter handlers")
	T &addProperty(const std::string &name, const std::string &str, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromString(str), getter, setter));
	}
#endif

	template<typename T>
	T &addProperty(const std::string &name, const std::string &str, const GattProperty::GetterHandler &getter, const GattProperty::SetterHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromString(str), getter, setter));
	}

	// Helper method for adding a named property with an array of std::strings
	template<typename T>
	T &addProperty(const std::string &name, const std::vector<std::string> &arr)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromStringArray(arr)));
	}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with wrapper getter/setter handlers")
	T &addProperty(const std::string &name, const std::vector<std::string> &arr, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromStringArray(arr), getter, setter));
	}
#endif

	template<typename T>
	T &addProperty(const std::string &name, const std::vector<std::string> &arr, const GattProperty::GetterHandler &getter, const GattProperty::SetterHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromStringArray(arr), getter, setter));
	}

	// Helper method for adding a named property with an array of C strings
	template<typename T>
	T &addProperty(const std::string &name, const std::vector<const char *> &arr)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromStringArray(arr)));
	}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with wrapper getter/setter handlers")
	T &addProperty(const std::string &name, const std::vector<const char *> &arr, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromStringArray(arr), getter, setter));
	}
#endif

	template<typename T>
	T &addProperty(const std::string &name, const std::vector<const char *> &arr, const GattProperty::GetterHandler &getter, const GattProperty::SetterHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromStringArray(arr), getter, setter));
	}

	// Helper method for adding a named property with a given C string
	template<typename T>
	T &addProperty(const std::string &name, const char *pStr)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromString(pStr)));
	}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with wrapper getter/setter handlers")
	T &addProperty(const std::string &name, const char *pStr, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromString(pStr), getter, setter));
	}
#endif

	template<typename T>
	T &addProperty(const std::string &name, const char *pStr, const GattProperty::GetterHandler &getter, const GattProperty::SetterHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromString(pStr), getter, setter));
	}

	// Helper method for adding a named property with a given boolean value
	template<typename T>
	T &addProperty(const std::string &name, bool value)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromBoolean(value)));
	}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattInterface::addProperty() with wrapper getter/setter handlers")
	T &addProperty(const std::string &name, bool value, RawPropertyGetterCallback getter, RawPropertySetterCallback setter = nullptr)
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromBoolean(value), getter, setter));
	}
#endif

	template<typename T>
	T &addProperty(const std::string &name, bool value, const GattProperty::GetterHandler &getter, const GattProperty::SetterHandler &setter = {})
	{
		return addProperty<T>(GattProperty(name, Utils::dbusVariantFromBoolean(value), getter, setter));
	}

	// Return a data value from the server's registered data getter (BZPServerDataGetter)
	//
	// This method is for use with non-pointer types. For pointer types, use `getDataPointer()` instead.
	//
	// This method is intended to be used in the server description. An example usage would be:
	//
	//     uint8_t batteryLevel = self.getDataValue<uint8_t>("battery/level", 0);
	template<typename T>
	T getDataValue(const char *pName, const T defaultValue) const
	{
		const void *pData = owner.getDataGetter()(pName);
		return nullptr == pData ? defaultValue : *static_cast<const T *>(pData);
	}

	// Return a data pointer from the server's registered data getter (BZPServerDataGetter)
	//
	// This method is for use with pointer types. For non-pointer types, use `getDataValue()` instead.
	//
	// This method is intended to be used in the server description. An example usage would be:
	//
	//     const char *pTextString = self.getDataPointer<const char *>("text/string", "");
	template<typename T>
	T getDataPointer(const char *pName, const T defaultValue) const
	{
		const void *pData = owner.getDataGetter()(pName);
		return nullptr == pData ? defaultValue : static_cast<const T>(pData);
	}

	// Sends a data value from the server back to the application through the server's registered data setter
	// (BZPServerDataSetter)
	//
	// This method is for use with non-pointer types. For pointer types, use `setDataPointer()` instead.
	//
	// This method is intended to be used in the server description. An example usage would be:
	//
	//     self.setDataValue("battery/level", batteryLevel);
	template<typename T>
	bool setDataValue(const char *pName, const T value) const
	{
		return owner.getDataSetter()(pName, static_cast<const void *>(&value)) != 0;
	}

	// Sends a data pointer from the server back to the application through the server's registered data setter
	// (BZPServerDataSetter)
	//
	// This method is for use with pointer types. For non-pointer types, use `setDataValue()` instead.
	//
	// This method is intended to be used in the server description. An example usage would be:
	//
	//     self.setDataPointer("text/string", stringFromGVariantByteArray(DBusVariantRef(pAyBuffer)).c_str());
	template<typename T>
	bool setDataPointer(const char *pName, const T pointer) const
	{
		return owner.getDataSetter()(pName, static_cast<const void *>(pointer)) != 0;
	}

	// When responding to a ReadValue method, we need to return a GVariant value in the form "(ay)" (a tuple containing an array of
	// bytes). This method will simplify this slightly by wrapping a GVariant of the type "ay" and wrapping it in a tuple before
	// sending it off as the method response.
	//
	// This is the generalized form that accepts a GVariant *. There is a templated helper method (`methodReturnValue()`) that accepts
	// common types.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	BZP_DEPRECATED("Use GattInterface::methodReturnVariant(DBusReplyRef, DBusVariantRef)")
	void methodReturnVariant(GDBusMethodInvocation *pInvocation, GVariant *pVariant, bool wrapInTuple = false) const;
#endif
	BZP_DEPRECATED("Use GattInterface::methodReturnVariant(DBusReplyRef, DBusVariantRef)")
	void methodReturnVariant(DBusMethodCallRef methodCall, DBusVariantRef variant, bool wrapInTuple = false) const;
	BZP_DEPRECATED("Use GattInterface::methodReturnVariant(DBusReplyRef, DBusVariantRef)")
	void methodReturnVariant(DBusMethodInvocationRef invocation, DBusVariantRef variant, bool wrapInTuple = false) const;
	void methodReturnVariant(DBusReplyRef reply, DBusVariantRef variant, bool wrapInTuple = false) const;

	// When responding to a ReadValue method, we need to return a GVariant value in the form "(ay)" (a tuple containing an array of
	// bytes). This method will simplify this slightly by wrapping a GVariant of the type "ay" and wrapping it in a tuple before
	// sending it off as the method response.
	//
	// This is a templated helper method that only works with common types. For a more generic form which can be used for custom
	// types, see `methodReturnVariant()'.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	template<typename T>
	BZP_DEPRECATED("Use GattInterface::methodReturnValue() wrapper overload with DBusReplyRef")
	void methodReturnValue(GDBusMethodInvocation *pInvocation, T value, bool wrapInTuple = false) const
	{
		methodReturnVariant(DBusReplyRef(pInvocation), Utils::dbusVariantFromByteArray(value), wrapInTuple);
	}
#endif

	template<typename T>
	BZP_DEPRECATED("Use GattInterface::methodReturnValue() wrapper overload with DBusReplyRef")
	void methodReturnValue(DBusMethodCallRef methodCall, T value, bool wrapInTuple = false) const
	{
		methodReturnVariant(DBusReplyRef(methodCall), Utils::dbusVariantFromByteArray(value), wrapInTuple);
	}

	template<typename T>
	BZP_DEPRECATED("Use GattInterface::methodReturnValue() wrapper overload with DBusReplyRef")
	void methodReturnValue(DBusMethodInvocationRef invocation, T value, bool wrapInTuple = false) const
	{
		methodReturnVariant(DBusReplyRef(invocation), Utils::dbusVariantFromByteArray(value), wrapInTuple);
	}

	template<typename T>
	void methodReturnValue(DBusReplyRef reply, T value, bool wrapInTuple = false) const
	{
		methodReturnVariant(reply, Utils::dbusVariantFromByteArray(value), wrapInTuple);
	}

	// Locates a `GattProperty` within the interface
	//
	// This method returns a pointer to the property or nullptr if not found
	const GattProperty *findProperty(const std::string &name) const;

	// Internal method used to generate introspection XML used to describe our services on D-Bus
	virtual std::string generateIntrospectionXML(int depth) const;

protected:

	std::list<GattProperty> properties;
};

}; // namespace bzp
