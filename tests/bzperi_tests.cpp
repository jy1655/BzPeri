#include <gio/gio.h>

#include <bzp/DBusInterface.h>
#include <bzp/GattCharacteristic.h>
#include <bzp/GattProperty.h>
#include <bzp/GattService.h>
#include <bzp/GattUuid.h>
#include <bzp/Server.h>

#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using bzp::DBusConnectionRef;
using bzp::DBusErrorRef;
using bzp::DBusInterface;
using bzp::DBusMethodInvocationRef;
using bzp::DBusObject;
using bzp::DBusObjectPath;
using bzp::DBusVariantRef;
using bzp::GattCharacteristic;
using bzp::GattProperty;
using bzp::GattService;
using bzp::GattUuid;
using bzp::Server;

class TestFailure : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
};

void require(bool condition, const std::string &message)
{
	if (!condition)
	{
		throw TestFailure(message);
	}
}

std::string describeVariant(GVariant *variant)
{
	if (variant == nullptr)
	{
		return "<null>";
	}

	gchar *printed = g_variant_print(variant, TRUE);
	std::string description = printed != nullptr ? printed : "<print-failed>";
	g_free(printed);
	return description;
}

std::string variantString(GVariant *variant)
{
	require(variant != nullptr, "Expected non-null GVariant");
	require(g_variant_is_of_type(variant, G_VARIANT_TYPE_STRING), "Expected string GVariant, got " + describeVariant(variant));
	return g_variant_get_string(variant, nullptr);
}

void expectVariantString(const GVariant *variant, std::string_view expected, std::string_view context)
{
	const auto actual = variantString(const_cast<GVariant *>(variant));
	require(actual == expected, std::string(context) + ": expected '" + std::string(expected) + "', got '" + actual + "'");
}

const void *nullGetter(const char *)
{
	return nullptr;
}

int acceptingSetter(const char *, const void *)
{
	return 1;
}

struct GenericMethodState
{
	bool called = false;
	bool hadConnection = false;
	bool hadInvocation = false;
	std::string objectPath;
	std::string methodName;
	std::string payload;
};

struct CharacteristicState
{
	bool readCalled = false;
	bool updateCalled = false;
	bool hadParameters = false;
	std::string objectPath;
	std::string methodName;
};

struct PropertyState
{
	int getterCalls = 0;
	int setterCalls = 0;
	std::string lastSetValue;
};

void testGattPropertyRuleOfFive()
{
	GVariant *external = g_variant_ref_sink(g_variant_new_string("alpha"));
	require(!g_variant_is_floating(external), "external variant should be sinked before test");

	{
		GattProperty original("Name", external);
		expectVariantString(original.getValue(), "alpha", "original property value");
		require(!g_variant_is_floating(const_cast<GVariant *>(original.getValue())), "property should keep a strong variant reference");

		GattProperty copied(original);
		expectVariantString(copied.getValue(), "alpha", "copied property value");

		GattProperty moved(std::move(copied));
		expectVariantString(moved.getValue(), "alpha", "moved property value");

		GattProperty assigned("Assigned", g_variant_new_string("temp"));
		assigned = original;
		expectVariantString(assigned.getValue(), "alpha", "copy-assigned property value");

		GattProperty moveAssigned("MoveAssigned", g_variant_new_string("temp2"));
		moveAssigned = std::move(moved);
		expectVariantString(moveAssigned.getValue(), "alpha", "move-assigned property value");

		GVariant *preserved = g_variant_ref(const_cast<GVariant *>(moveAssigned.getValue()));
		moveAssigned.setValue(g_variant_new_string("beta"));
		expectVariantString(moveAssigned.getValue(), "beta", "replaced property value");
		expectVariantString(preserved, "alpha", "preserved external reference after setValue");
		g_variant_unref(preserved);

		std::list<GattProperty> properties;
		properties.push_back(original);
		properties.emplace_back("Dynamic", g_variant_new_string("gamma"));

		auto iterator = properties.begin();
		expectVariantString(iterator->getValue(), "alpha", "list copy property value");
		++iterator;
		expectVariantString(iterator->getValue(), "gamma", "list emplace property value");
	}

	expectVariantString(external, "alpha", "external reference after property destruction");
	g_variant_unref(external);
}

void testServerWrapperMethodDispatch()
{
	Server server("bzperi.tests.method", "", "", &nullGetter, &acceptingSetter);
	DBusObjectPath rootPath;
	GenericMethodState state;

	server.configure([&](DBusObject &root) {
		rootPath = root.getPath();

		auto interface = root.addInterface(std::make_shared<DBusInterface>(root, "com.example.Test"));
		static const char *inArgs[] = {"s", nullptr};
		interface->addMethod("Ping", inArgs, "s",
			[](const DBusInterface &self, DBusConnectionRef connection, const std::string &methodName, DBusVariantRef parameters, DBusMethodInvocationRef invocation, void *userData) {
				auto &state = *static_cast<GenericMethodState *>(userData);
				state.called = true;
				state.hadConnection = static_cast<bool>(connection);
				state.hadInvocation = static_cast<bool>(invocation);
				state.objectPath = self.getPath().toString();
				state.methodName = methodName;
				state.payload = variantString(parameters.get());
			});
	});

	GVariant *parameters = g_variant_ref_sink(g_variant_new_string("hello"));
	require(server.callMethod(rootPath, "com.example.Test", "Ping", DBusConnectionRef(), DBusVariantRef(parameters), DBusMethodInvocationRef(), &state),
		"Server::callMethod should dispatch wrapper-based interface methods");
	g_variant_unref(parameters);

	require(state.called, "wrapper method handler should be called");
	require(!state.hadConnection, "test dispatch should not provide a bus connection");
	require(!state.hadInvocation, "test dispatch should not provide a method invocation");
	require(state.objectPath == rootPath.toString(), "handler should see the root object path");
	require(state.methodName == "Ping", "handler should receive the dispatched method name");
	require(state.payload == "hello", "handler should receive the dispatched GVariant payload");

	require(!server.callMethod(rootPath, "com.example.Test", "Missing", DBusConnectionRef(), DBusVariantRef(), DBusMethodInvocationRef(), &state),
		"Unknown methods should not dispatch");
}

void testCharacteristicAndPropertyWrappers()
{
	Server server("bzperi.tests.characteristic", "", "", &nullGetter, &acceptingSetter);
	DBusObjectPath servicePath;
	DBusObjectPath characteristicPath;
	CharacteristicState characteristicState;
	PropertyState propertyState;

	server.configure([&](DBusObject &root) {
		GattService &service = root.gattServiceBegin("svc", GattUuid("1234"));
		servicePath = service.getPath();

		GattCharacteristic &characteristic = service.gattCharacteristicBegin("value", GattUuid("1235"), {"read", "write", "notify"});
		characteristicPath = characteristic.getPath();

		characteristic.onReadValue(
			[](const GattCharacteristic &self, DBusConnectionRef, const std::string &methodName, DBusVariantRef parameters, DBusMethodInvocationRef, void *userData) {
				auto &state = *static_cast<CharacteristicState *>(userData);
				state.readCalled = true;
				state.hadParameters = static_cast<bool>(parameters);
				state.objectPath = self.getPath().toString();
				state.methodName = methodName;
			});

		characteristic.onUpdatedValue(
			[](const GattCharacteristic &self, DBusConnectionRef, void *userData) -> bool {
				auto &state = *static_cast<CharacteristicState *>(userData);
				state.updateCalled = true;
				state.objectPath = self.getPath().toString();
				return true;
			});

		characteristic.addProperty<GattCharacteristic>(
			"DynamicValue",
			g_variant_new_string("seed"),
			[](DBusConnectionRef, std::string_view, std::string_view, std::string_view, std::string_view, DBusErrorRef, void *userData) -> DBusVariantRef {
				auto &state = *static_cast<PropertyState *>(userData);
				state.getterCalls += 1;
				GVariant *value = g_variant_ref_sink(g_variant_new_string("computed"));
				return DBusVariantRef(value);
			},
			[](DBusConnectionRef, std::string_view, std::string_view, std::string_view, std::string_view, DBusVariantRef value, DBusErrorRef, void *userData) -> bool {
				auto &state = *static_cast<PropertyState *>(userData);
				state.setterCalls += 1;
				state.lastSetValue = variantString(value.get());
				return true;
			});
	});

	const GattProperty *serviceUuid = server.findProperty(servicePath, "org.bluez.GattService1", "UUID");
	require(serviceUuid != nullptr, "GattService UUID property should be discoverable");
	expectVariantString(serviceUuid->getValue(), GattUuid("1234").toString128(), "service UUID property value");

	GVariant *readParameters = g_variant_ref_sink(g_variant_new("()"));
	require(server.callMethod(characteristicPath, "org.bluez.GattCharacteristic1", "ReadValue", DBusConnectionRef(), DBusVariantRef(readParameters), DBusMethodInvocationRef(), &characteristicState),
		"Characteristic ReadValue should dispatch via wrapper path");
	g_variant_unref(readParameters);

	require(characteristicState.readCalled, "Characteristic wrapper read handler should run");
	require(characteristicState.hadParameters, "Characteristic wrapper read handler should receive parameters");
	require(characteristicState.objectPath == characteristicPath.toString(), "Characteristic read handler should see its object path");
	require(characteristicState.methodName == "ReadValue", "Characteristic read handler should see method name");

	auto characteristicInterface = std::dynamic_pointer_cast<const GattCharacteristic>(
		server.findInterface(characteristicPath, "org.bluez.GattCharacteristic1"));
	require(characteristicInterface != nullptr, "Characteristic interface lookup should return a typed interface");
	require(characteristicInterface->callOnUpdatedValue(DBusConnectionRef(), &characteristicState),
		"Characteristic update handler should be callable through wrapper path");
	require(characteristicState.updateCalled, "Characteristic update handler should run");

	const GattProperty *dynamicProperty = server.findProperty(characteristicPath, "org.bluez.GattCharacteristic1", "DynamicValue");
	require(dynamicProperty != nullptr, "Dynamic characteristic property should be discoverable");
	require(static_cast<bool>(dynamicProperty->getGetterHandler()), "Dynamic property should retain wrapper getter handler");
	require(static_cast<bool>(dynamicProperty->getSetterHandler()), "Dynamic property should retain wrapper setter handler");

	GError *error = nullptr;
	GVariant *getterValue = dynamicProperty->getGetterHandler()(
		DBusConnectionRef(),
		std::string_view(),
		characteristicPath.toString(),
		"org.bluez.GattCharacteristic1",
		"DynamicValue",
		DBusErrorRef(&error),
		&propertyState).get();
	require(error == nullptr, "Dynamic property getter should not set GError");
	expectVariantString(getterValue, "computed", "Dynamic property getter return value");
	require(propertyState.getterCalls == 1, "Dynamic property getter should update test state");
	g_variant_unref(getterValue);

	GVariant *setterValue = g_variant_ref_sink(g_variant_new_string("written"));
	require(dynamicProperty->getSetterHandler()(
		DBusConnectionRef(),
		std::string_view(),
		characteristicPath.toString(),
		"org.bluez.GattCharacteristic1",
		"DynamicValue",
		DBusVariantRef(setterValue),
		DBusErrorRef(&error),
		&propertyState),
		"Dynamic property setter should report success");
	require(error == nullptr, "Dynamic property setter should not set GError");
	require(propertyState.setterCalls == 1, "Dynamic property setter should update test state");
	require(propertyState.lastSetValue == "written", "Dynamic property setter should see the provided value");
	g_variant_unref(setterValue);
}

struct TestCase
{
	const char *name;
	std::function<void()> run;
};

int runTest(const TestCase &testCase)
{
	try
	{
		testCase.run();
		std::cout << "[PASS] " << testCase.name << '\n';
		return EXIT_SUCCESS;
	}
	catch (const std::exception &error)
	{
		std::cerr << "[FAIL] " << testCase.name << ": " << error.what() << '\n';
		return EXIT_FAILURE;
	}
	catch (...)
	{
		std::cerr << "[FAIL] " << testCase.name << ": unknown error\n";
		return EXIT_FAILURE;
	}
}

} // namespace

int main()
{
	const std::vector<TestCase> tests = {
		{"GattProperty rule-of-five", testGattPropertyRuleOfFive},
		{"Server wrapper method dispatch", testServerWrapperMethodDispatch},
		{"Characteristic/property wrapper dispatch", testCharacteristicAndPropertyWrappers},
	};

	int failures = 0;
	for (const auto &testCase : tests)
	{
		failures += runTest(testCase);
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
