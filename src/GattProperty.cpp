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

DBusVariantRef retainVariantRef(GVariant *variant)
{
	return DBusVariantRef(retainVariant(variant));
}

void releaseVariant(DBusVariantRef &variant)
{
	if (variant.get() != nullptr)
	{
		g_variant_unref(variant.get());
		variant = DBusVariantRef();
	}
}

GattProperty::GetterCallHandler makeGetterCallHandler(const GattProperty::GetterHandler &getter)
{
	if (!getter)
	{
		return {};
	}

	return [getter](DBusPropertyCallRef call) {
		return getter(
			call.connection(),
			call.sender(),
			call.objectPath(),
			call.interfaceName(),
			call.propertyName(),
			call.error(),
			call.userData());
	};
}

GattProperty::SetterCallHandler makeSetterCallHandler(const GattProperty::SetterHandler &setter)
{
	if (!setter)
	{
		return {};
	}

	return [setter](DBusPropertyCallRef call) {
		return setter(
			call.connection(),
			call.sender(),
			call.objectPath(),
			call.interfaceName(),
			call.propertyName(),
			call.value(),
			call.error(),
			call.userData());
	};
}

} // namespace

// Constructs a named property
//
// In general, properties should not be constructed directly as properties are typically instanticated by adding them to to an
// interface using one of the the interface's `addProperty` methods.
GattProperty::GattProperty(const std::string &name, DBusVariantRef value)
: name(name), value_(retainVariantRef(value.get()))
{
}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
GattProperty::GattProperty(const std::string &name, GVariant *pValue)
: name(name), value_(retainVariantRef(pValue))
{
}

GattProperty::GattProperty(const std::string &name, GVariant *pValue, RawPropertyGetterCallback getter, RawPropertySetterCallback setter)
: name(name), value_(retainVariantRef(pValue)), getterFunc(getter), setterFunc(setter)
{
}

GattProperty::GattProperty(const std::string &name, DBusVariantRef value, RawPropertyGetterCallback getter, RawPropertySetterCallback setter)
: name(name), value_(retainVariantRef(value.get())), getterFunc(getter), setterFunc(setter)
{
}
GattProperty::GattProperty(const std::string &name, GVariant *pValue, const GetterHandler &getter, const SetterHandler &setter)
: name(name), value_(retainVariantRef(pValue)), getterHandler(getter), setterHandler(setter),
  getterCallHandler(makeGetterCallHandler(getter)), setterCallHandler(makeSetterCallHandler(setter))
{
}
#endif

GattProperty::GattProperty(const std::string &name, DBusVariantRef value, const GetterHandler &getter, const SetterHandler &setter)
: name(name), value_(retainVariantRef(value.get())), getterHandler(getter), setterHandler(setter),
  getterCallHandler(makeGetterCallHandler(getter)), setterCallHandler(makeSetterCallHandler(setter))
{
}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
GattProperty::GattProperty(const std::string &name, GVariant *pValue, const GetterCallHandler &getter, const SetterCallHandler &setter)
: name(name), value_(retainVariantRef(pValue)), getterCallHandler(getter), setterCallHandler(setter)
{
}
#endif

GattProperty::GattProperty(const std::string &name, DBusVariantRef value, const GetterCallHandler &getter, const SetterCallHandler &setter)
: name(name), value_(retainVariantRef(value.get())), getterCallHandler(getter), setterCallHandler(setter)
{
}

GattProperty::GattProperty(const GattProperty &other)
: name(other.name),
  value_(retainVariantRef(other.value_.get())),
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
  getterFunc(other.getterFunc),
  setterFunc(other.setterFunc),
#endif
  getterHandler(other.getterHandler),
  setterHandler(other.setterHandler),
  getterCallHandler(other.getterCallHandler),
  setterCallHandler(other.setterCallHandler)
{
}

GattProperty::GattProperty(GattProperty &&other) noexcept
: name(std::move(other.name)),
  value_(other.value_),
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
  getterFunc(other.getterFunc),
  setterFunc(other.setterFunc),
#endif
  getterHandler(std::move(other.getterHandler)),
  setterHandler(std::move(other.setterHandler)),
  getterCallHandler(std::move(other.getterCallHandler)),
  setterCallHandler(std::move(other.setterCallHandler))
{
	other.value_ = DBusVariantRef();
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	other.getterFunc = nullptr;
	other.setterFunc = nullptr;
#endif
}

GattProperty &GattProperty::operator=(const GattProperty &other)
{
	if (this == &other)
	{
		return *this;
	}

	releaseVariant(value_);
	name = other.name;
	value_ = retainVariantRef(other.value_.get());
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	getterFunc = other.getterFunc;
	setterFunc = other.setterFunc;
#endif
	getterHandler = other.getterHandler;
	setterHandler = other.setterHandler;
	getterCallHandler = other.getterCallHandler;
	setterCallHandler = other.setterCallHandler;
	return *this;
}

GattProperty &GattProperty::operator=(GattProperty &&other) noexcept
{
	if (this == &other)
	{
		return *this;
	}

	releaseVariant(value_);
	name = std::move(other.name);
	value_ = other.value_;
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	getterFunc = other.getterFunc;
	setterFunc = other.setterFunc;
#endif
	getterHandler = std::move(other.getterHandler);
	setterHandler = std::move(other.setterHandler);
	getterCallHandler = std::move(other.getterCallHandler);
	setterCallHandler = std::move(other.setterCallHandler);

	other.value_ = DBusVariantRef();
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	other.getterFunc = nullptr;
	other.setterFunc = nullptr;
#endif
	return *this;
}

GattProperty::~GattProperty()
{
	releaseVariant(value_);
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
DBusVariantRef GattProperty::getValueRef() const
{
	return value_;
}

// Sets the property's value
//
// In general, this method should not be called directly as properties are typically added to an interface using one of the the
// interface's `addProperty` methods.
GattProperty &GattProperty::setValue(DBusVariantRef value)
{
	releaseVariant(value_);
	value_ = retainVariantRef(value.get());
	return *this;
}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
const GVariant *GattProperty::getValue() const
{
	return value_.get();
}

GattProperty &GattProperty::setValue(GVariant *pValue)
{
	releaseVariant(value_);
	value_ = retainVariantRef(pValue);
	return *this;
}
#endif

//
// Callbacks to get/set this property
//

// Internal use method to retrieve the getter delegate method used to return custom values for a property
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
RawPropertyGetterCallback GattProperty::getGetterFunc() const
{
	return getterFunc;
}
#endif

const GattProperty::GetterHandler &GattProperty::getGetterHandler() const
{
	return getterHandler;
}

const GattProperty::GetterCallHandler &GattProperty::getGetterCallHandler() const
{
	return getterCallHandler;
}

// Internal use method to set the getter delegate method used to return custom values for a property
//
// In general, this method should not be called directly as properties are typically added to an interface using one of the the
// interface's `addProperty` methods.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
GattProperty &GattProperty::setGetterFunc(RawPropertyGetterCallback func)
{
	getterFunc = func;
	getterHandler = {};
	getterCallHandler = {};
	return *this;
}
#endif

GattProperty &GattProperty::setGetterHandler(const GetterHandler &handler)
{
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	getterFunc = nullptr;
#endif
	getterHandler = handler;
	getterCallHandler = makeGetterCallHandler(handler);
	return *this;
}

GattProperty &GattProperty::setGetterCallHandler(const GetterCallHandler &handler)
{
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	getterFunc = nullptr;
#endif
	getterHandler = {};
	getterCallHandler = handler;
	return *this;
}

// Internal use method to retrieve the setter delegate method used to return custom values for a property
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
RawPropertySetterCallback GattProperty::getSetterFunc() const
{
	return setterFunc;
}
#endif

const GattProperty::SetterHandler &GattProperty::getSetterHandler() const
{
	return setterHandler;
}

const GattProperty::SetterCallHandler &GattProperty::getSetterCallHandler() const
{
	return setterCallHandler;
}

// Internal use method to set the setter delegate method used to return custom values for a property
//
// In general, this method should not be called directly as properties are typically added to an interface using one of the the
// interface's `addProperty` methods.
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
GattProperty &GattProperty::setSetterFunc(RawPropertySetterCallback func)
{
	setterFunc = func;
	setterHandler = {};
	setterCallHandler = {};
	return *this;
}
#endif

GattProperty &GattProperty::setSetterHandler(const SetterHandler &handler)
{
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	setterFunc = nullptr;
#endif
	setterHandler = handler;
	setterCallHandler = makeSetterCallHandler(handler);
	return *this;
}

GattProperty &GattProperty::setSetterCallHandler(const SetterCallHandler &handler)
{
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	setterFunc = nullptr;
#endif
	setterHandler = {};
	setterCallHandler = handler;
	return *this;
}

// Internal method used to generate introspection XML used to describe our services on D-Bus
std::string GattProperty::generateIntrospectionXML(int depth) const
{
	std::string prefix;
	prefix.insert(0, depth * 2, ' ');

	std::string xml = std::string();

	GVariant *pValue = getValueRef().get();
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
