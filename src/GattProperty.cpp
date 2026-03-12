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
// This class is intended to be used within the server description. For an explanation of how this class is used, see the detailed
// description in Server.cpp.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <gio/gio.h>
#include <string>

#include <bzp/Utils.h>
#include <bzp/GattProperty.h>

namespace bzp {

namespace {

GVariant *retainVariant(GVariant *variant)
{
	return variant != nullptr ? g_variant_ref_sink(variant) : nullptr;
}

void releaseVariant(GVariant *&variant)
{
	if (variant != nullptr)
	{
		g_variant_unref(variant);
		variant = nullptr;
	}
}

} // namespace

// Constructs a named property
//
// In general, properties should not be constructed directly as properties are typically instanticated by adding them to to an
// interface using one of the the interface's `addProperty` methods.
GattProperty::GattProperty(const std::string &name, GVariant *pValue, GDBusInterfaceGetPropertyFunc getter, GDBusInterfaceSetPropertyFunc setter)
: name(name), pValue(retainVariant(pValue)), getterFunc(getter), setterFunc(setter)
{
}

GattProperty::GattProperty(const std::string &name, DBusVariantRef value, GDBusInterfaceGetPropertyFunc getter, GDBusInterfaceSetPropertyFunc setter)
: GattProperty(name, value.get(), getter, setter)
{
}

GattProperty::GattProperty(const std::string &name, GVariant *pValue, const GetterHandler &getter, const SetterHandler &setter)
: name(name), pValue(retainVariant(pValue)), getterFunc(nullptr), setterFunc(nullptr), getterHandler(getter), setterHandler(setter)
{
}

GattProperty::GattProperty(const std::string &name, DBusVariantRef value, const GetterHandler &getter, const SetterHandler &setter)
: GattProperty(name, value.get(), getter, setter)
{
}

GattProperty::GattProperty(const GattProperty &other)
: name(other.name),
  pValue(retainVariant(other.pValue)),
  getterFunc(other.getterFunc),
  setterFunc(other.setterFunc),
  getterHandler(other.getterHandler),
  setterHandler(other.setterHandler)
{
}

GattProperty::GattProperty(GattProperty &&other) noexcept
: name(std::move(other.name)),
  pValue(other.pValue),
  getterFunc(other.getterFunc),
  setterFunc(other.setterFunc),
  getterHandler(std::move(other.getterHandler)),
  setterHandler(std::move(other.setterHandler))
{
	other.pValue = nullptr;
	other.getterFunc = nullptr;
	other.setterFunc = nullptr;
}

GattProperty &GattProperty::operator=(const GattProperty &other)
{
	if (this == &other)
	{
		return *this;
	}

	releaseVariant(pValue);
	name = other.name;
	pValue = retainVariant(other.pValue);
	getterFunc = other.getterFunc;
	setterFunc = other.setterFunc;
	getterHandler = other.getterHandler;
	setterHandler = other.setterHandler;
	return *this;
}

GattProperty &GattProperty::operator=(GattProperty &&other) noexcept
{
	if (this == &other)
	{
		return *this;
	}

	releaseVariant(pValue);
	name = std::move(other.name);
	pValue = other.pValue;
	getterFunc = other.getterFunc;
	setterFunc = other.setterFunc;
	getterHandler = std::move(other.getterHandler);
	setterHandler = std::move(other.setterHandler);

	other.pValue = nullptr;
	other.getterFunc = nullptr;
	other.setterFunc = nullptr;
	return *this;
}

GattProperty::~GattProperty()
{
	releaseVariant(pValue);
}

//
// Name
//

// Returns the name of the property
const std::string &GattProperty::getName() const
{
	return name;
}

// Sets the name of the property
//
// In general, this method should not be called directly as properties are typically added to an interface using one of the the
// interface's `addProperty` methods.
GattProperty &GattProperty::setName(const std::string &name)
{
	this->name = name;
	return *this;
}

//
// Value
//

// Returns the property's value
const GVariant *GattProperty::getValue() const
{
	return pValue;
}

DBusVariantRef GattProperty::getValueRef() const
{
	return DBusVariantRef(pValue);
}

// Sets the property's value
//
// In general, this method should not be called directly as properties are typically added to an interface using one of the the
// interface's `addProperty` methods.
GattProperty &GattProperty::setValue(GVariant *pValue)
{
	releaseVariant(this->pValue);
	this->pValue = retainVariant(pValue);
	return *this;
}

GattProperty &GattProperty::setValue(DBusVariantRef value)
{
	return setValue(value.get());
}

//
// Callbacks to get/set this property
//

// Internal use method to retrieve the getter delegate method used to return custom values for a property
GDBusInterfaceGetPropertyFunc GattProperty::getGetterFunc() const
{
	return getterFunc;
}

const GattProperty::GetterHandler &GattProperty::getGetterHandler() const
{
	return getterHandler;
}

// Internal use method to set the getter delegate method used to return custom values for a property
//
// In general, this method should not be called directly as properties are typically added to an interface using one of the the
// interface's `addProperty` methods.
GattProperty &GattProperty::setGetterFunc(GDBusInterfaceGetPropertyFunc func)
{
	getterFunc = func;
	getterHandler = {};
	return *this;
}

GattProperty &GattProperty::setGetterHandler(const GetterHandler &handler)
{
	getterFunc = nullptr;
	getterHandler = handler;
	return *this;
}

// Internal use method to retrieve the setter delegate method used to return custom values for a property
GDBusInterfaceSetPropertyFunc GattProperty::getSetterFunc() const
{
	return setterFunc;
}

const GattProperty::SetterHandler &GattProperty::getSetterHandler() const
{
	return setterHandler;
}

// Internal use method to set the setter delegate method used to return custom values for a property
//
// In general, this method should not be called directly as properties are typically added to an interface using one of the the
// interface's `addProperty` methods.
GattProperty &GattProperty::setSetterFunc(GDBusInterfaceSetPropertyFunc func)
{
	setterFunc = func;
	setterHandler = {};
	return *this;
}

GattProperty &GattProperty::setSetterHandler(const SetterHandler &handler)
{
	setterFunc = nullptr;
	setterHandler = handler;
	return *this;
}

// Internal method used to generate introspection XML used to describe our services on D-Bus
std::string GattProperty::generateIntrospectionXML(int depth) const
{
	std::string prefix;
	prefix.insert(0, depth * 2, ' ');

	std::string xml = std::string();

	GVariant *pValue = const_cast<GVariant *>(getValue());
	const gchar *pType = g_variant_get_type_string(pValue);
	xml += prefix + "<property name='" + getName() + "' type='" + pType + "' access='read'>\n";

	if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_BOOLEAN))
	{
		xml += prefix + "  <annotation name='name' value='" + (g_variant_get_boolean(pValue) != 0 ? "true":"false") + "' />\n";
	}
	else if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_INT16))
	{
		xml += prefix + "  <annotation name='name' value='" + std::to_string(g_variant_get_int16(pValue)) + "' />\n";
	}
	else if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_UINT16))
	{
		xml += prefix + "  <annotation name='name' value='" + std::to_string(g_variant_get_uint16(pValue)) + "' />\n";
	}
	else if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_INT32))
	{
		xml += prefix + "  <annotation name='name' value='" + std::to_string(g_variant_get_int32(pValue)) + "' />\n";
	}
	else if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_UINT32))
	{
		xml += prefix + "  <annotation name='name' value='" + std::to_string(g_variant_get_uint32(pValue)) + "' />\n";
	}
	else if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_INT64))
	{
		xml += prefix + "  <annotation name='name' value='" + std::to_string(g_variant_get_int64(pValue)) + "' />\n";
	}
	else if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_UINT64))
	{
		xml += prefix + "  <annotation name='name' value='" + std::to_string(g_variant_get_uint64(pValue)) + "' />\n";
	}
	else if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_DOUBLE))
	{
		xml += prefix + "  <annotation value='" + std::to_string(g_variant_get_double(pValue)) + "' />\n";
	}
	else if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_STRING))
	{
		xml += prefix + "  <annotation name='name' value='" + g_variant_get_string(pValue, nullptr) + "' />\n";
	}
	else if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_OBJECT_PATH))
	{
		xml += prefix + "  <annotation name='name' value='" + g_variant_get_string(pValue, nullptr) + "' />\n";
	}
	else if (g_variant_is_of_type(pValue, G_VARIANT_TYPE_BYTESTRING))
	{
		xml += prefix + "  <annotation name='name' value='" + g_variant_get_bytestring(pValue) + "' />\n";
	}

	xml += prefix + "</property>\n";

	return xml;
}

}; // namespace bzp
