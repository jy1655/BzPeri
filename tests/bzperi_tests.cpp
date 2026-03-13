#include <gio/gio.h>
#include "config.h"

#include <BzPeri.h>
#include <bzp/BluezAdapter.h>
#include <bzp/DBusMethod.h>
#include <bzp/DBusInterface.h>
#include <bzp/GattCharacteristic.h>
#include <bzp/GattProperty.h>
#include <bzp/GattService.h>
#include <bzp/GattUuid.h>
#include <bzp/Server.h>

#include "../src/BluezAdvertisingSupport.h"
#include "../src/BluezAdapterCompat.h"
#include "../src/ServerCompat.h"
#include "../src/ServerUtils.h"
#include "../src/StructuredLogger.h"

#include <cstdlib>
#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace bzp {
void setServerRunState(BZPServerRunState newState);
void setServerHealth(BZPServerHealth newHealth);
}

namespace {

using bzp::DBusConnectionRef;
using bzp::DBusErrorRef;
using bzp::DBusInterface;
using bzp::DBusMethod;
using bzp::DBusMethodCallRef;
using bzp::DBusMethodInvocationRef;
using bzp::DBusNotificationRef;
using bzp::DBusObject;
using bzp::DBusObjectPath;
using bzp::DBusPropertyCallRef;
using bzp::DBusReplyRef;
using bzp::DBusSignalRef;
using bzp::DBusUpdateRef;
using bzp::DBusVariantRef;
using bzp::getActiveBluezAdapter;
using bzp::getActiveBluezAdapterPtr;
using bzp::getActiveServer;
using bzp::getActiveServerPtr;
using bzp::GattCharacteristic;
using bzp::GattProperty;
using bzp::GattService;
using bzp::GattUuid;
using bzp::Server;
using bzp::StructuredLogger;
using bzp::Utils;
using bzp::setActiveServer;
using bzp::setActiveServerForRuntime;
using bzp::setActiveBluezAdapterForRuntime;
using bzp::makeRuntimeBluezAdapterPtr;
using bzp::detail::canUseExtendedAdvertising;
using bzp::detail::collectGattServiceUUIDs;
using bzp::detail::selectAdvertisementServiceUUIDs;
using bzp::BluezCapabilities;

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
	bool updateUserDataMatched = false;
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

struct RunLoopInvokeState
{
	bool called = false;
	void *receivedUserData = nullptr;
	std::thread::id callbackThread;
};

struct LegacyRawCallbackState
{
	bool methodCalled = false;
	bool characteristicReadCalled = false;
	bool characteristicUpdatedCalled = false;
};

std::vector<std::string> capturedInfoLogs;
std::vector<std::string> capturedWarnLogs;
std::vector<std::string> capturedStatusLogs;
std::vector<std::string> capturedTraceLogs;

void ignoreGLog(const gchar *, GLogLevelFlags, const gchar *, gpointer)
{
}

void captureInfoLog(const char *message)
{
	capturedInfoLogs.emplace_back(message != nullptr ? message : "");
}

void captureWarnLog(const char *message)
{
	capturedWarnLogs.emplace_back(message != nullptr ? message : "");
}

void captureStatusLog(const char *message)
{
	capturedStatusLogs.emplace_back(message != nullptr ? message : "");
}

void captureTraceLog(const char *message)
{
	capturedTraceLogs.emplace_back(message != nullptr ? message : "");
}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
void legacyRawMethodHandler(const DBusInterface &, GDBusConnection *, const std::string &, GVariant *, GDBusMethodInvocation *, void *userData)
{
	auto &state = *static_cast<LegacyRawCallbackState *>(userData);
	state.methodCalled = true;
}

void legacyRawCharacteristicReadHandler(const GattCharacteristic &, GDBusConnection *, const std::string &, GVariant *, GDBusMethodInvocation *, void *userData)
{
	auto &state = *static_cast<LegacyRawCallbackState *>(userData);
	state.characteristicReadCalled = true;
}

bool legacyRawCharacteristicUpdateHandler(const GattCharacteristic &, GDBusConnection *, void *userData)
{
	auto &state = *static_cast<LegacyRawCallbackState *>(userData);
	state.characteristicUpdatedCalled = true;
	return true;
}
#endif

void runLoopInvokeHandler(void *userData)
{
	auto &state = *static_cast<RunLoopInvokeState *>(userData);
	state.called = true;
	state.receivedUserData = userData;
	state.callbackThread = std::this_thread::get_id();
}

void testGattPropertyRuleOfFive()
{
	GVariant *external = g_variant_ref_sink(g_variant_new_string("alpha"));
	require(!g_variant_is_floating(external), "external variant should be sinked before test");

	{
		GattProperty original("Name", DBusVariantRef(external));
		expectVariantString(original.getValueRef().get(), "alpha", "original property value");
		require(!g_variant_is_floating(original.getValueRef().get()), "property should keep a strong variant reference");

		GattProperty copied(original);
		expectVariantString(copied.getValueRef().get(), "alpha", "copied property value");

		GattProperty moved(std::move(copied));
		expectVariantString(moved.getValueRef().get(), "alpha", "moved property value");

		GattProperty assigned("Assigned", DBusVariantRef(g_variant_new_string("temp")));
		assigned = original;
		expectVariantString(assigned.getValueRef().get(), "alpha", "copy-assigned property value");

		GattProperty moveAssigned("MoveAssigned", DBusVariantRef(g_variant_new_string("temp2")));
		moveAssigned = std::move(moved);
		expectVariantString(moveAssigned.getValueRef().get(), "alpha", "move-assigned property value");

		GVariant *preserved = g_variant_ref(moveAssigned.getValueRef().get());
		moveAssigned.setValue(DBusVariantRef(g_variant_new_string("beta")));
		expectVariantString(moveAssigned.getValueRef().get(), "beta", "replaced property value");
		expectVariantString(preserved, "alpha", "preserved external reference after setValue");
		g_variant_unref(preserved);

		std::list<GattProperty> properties;
		properties.push_back(original);
		properties.emplace_back("Dynamic", DBusVariantRef(g_variant_new_string("gamma")));

		auto iterator = properties.begin();
		expectVariantString(iterator->getValueRef().get(), "alpha", "list copy property value");
		++iterator;
		expectVariantString(iterator->getValueRef().get(), "gamma", "list emplace property value");
	}

	expectVariantString(external, "alpha", "external reference after property destruction");
	g_variant_unref(external);
}

void testServerWrapperMethodDispatch()
{
	Server server("bzperi.tests.method", "", "", &nullGetter, &acceptingSetter);
	DBusObjectPath rootPath;
	GenericMethodState state;
	std::shared_ptr<DBusInterface> interface;

	server.configure([&](DBusObject &root) {
		rootPath = root.getPath();

		interface = root.addInterface(std::make_shared<DBusInterface>(root, "com.example.Test"));
		static const char *inArgs[] = {"s", nullptr};
		interface->addMethod("Ping", inArgs, "s",
			[](const DBusInterface &self, const std::string &methodName, DBusMethodCallRef methodCall) {
				auto &state = *static_cast<GenericMethodState *>(methodCall.userData());
				state.called = true;
				state.hadConnection = static_cast<bool>(methodCall.connection());
				state.hadInvocation = static_cast<bool>(methodCall.invocation());
				state.objectPath = self.getPath().toString();
				state.methodName = methodName;
				state.payload = variantString(methodCall.parameters().get());
			});
		interface->addMethod("PingWrapper", inArgs, "s",
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
	require(server.callMethod(rootPath, "com.example.Test", "Ping", DBusMethodCallRef(DBusConnectionRef(), DBusVariantRef(parameters), DBusMethodInvocationRef(), &state)),
		"Server::callMethod should dispatch DBusMethodCallRef-based interface methods");
	g_variant_unref(parameters);

	require(state.called, "DBusMethodCallRef method handler should be called");
	require(!state.hadConnection, "test dispatch should not provide a bus connection");
	require(!state.hadInvocation, "test dispatch should not provide a method invocation");
	require(state.objectPath == rootPath.toString(), "handler should see the root object path");
	require(state.methodName == "Ping", "handler should receive the dispatched method name");
	require(state.payload == "hello", "handler should receive the dispatched GVariant payload");

	GVariant *contextParameters = g_variant_ref_sink(g_variant_new_string("context-dispatch"));
	state.called = false;
	state.payload.clear();
	require(server.callMethod(rootPath, "com.example.Test", "Ping", DBusMethodCallRef(DBusConnectionRef(), DBusVariantRef(contextParameters), DBusMethodInvocationRef(), &state)),
		"Server::callMethod should dispatch through DBusMethodCallRef");
	g_variant_unref(contextParameters);
	require(state.called, "DBusMethodCallRef dispatch should invoke the method handler");
	require(state.payload == "context-dispatch", "DBusMethodCallRef dispatch should preserve payload");

	require(!server.callMethod(rootPath, "com.example.Test", "Missing", DBusMethodCallRef(DBusConnectionRef(), DBusVariantRef(), DBusMethodInvocationRef(), &state)),
		"Unknown methods should not dispatch");

	require(interface != nullptr, "Expected a typed DBusInterface for direct wrapper/raw callMethod tests");

	GVariant *directWrapperParameters = g_variant_ref_sink(g_variant_new_string("wrapper-direct"));
	state.called = false;
	state.payload.clear();
	require(interface->callMethod("Ping", DBusMethodCallRef(DBusConnectionRef(), DBusVariantRef(directWrapperParameters), DBusMethodInvocationRef(), &state)),
		"DBusInterface DBusMethodCallRef callMethod should dispatch directly");
	g_variant_unref(directWrapperParameters);
	require(state.called, "DBusInterface DBusMethodCallRef callMethod should invoke the method handler");
	require(state.payload == "wrapper-direct", "DBusInterface DBusMethodCallRef callMethod should preserve payload");

	GVariant *wrapperShimParameters = g_variant_ref_sink(g_variant_new_string("wrapper-shim"));
	state.called = false;
	state.payload.clear();
	require(server.callMethod(rootPath, "com.example.Test", "PingWrapper",
		DBusMethodCallRef(DBusConnectionRef(), DBusVariantRef(wrapperShimParameters), DBusMethodInvocationRef(), &state)),
		"Server wrapper callMethod path should still dispatch");
	g_variant_unref(wrapperShimParameters);
	require(state.called, "Server wrapper callMethod path should invoke the method handler");
	require(state.payload == "wrapper-shim", "Server wrapper callMethod path should preserve payload");

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
	GVariant *directRawParameters = g_variant_ref_sink(g_variant_new_string("raw-direct"));
	state.called = false;
	state.payload.clear();
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	require(interface->callMethod("Ping", nullptr, directRawParameters, nullptr, &state),
		"DBusInterface raw callMethod shim should still dispatch");
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	g_variant_unref(directRawParameters);
	require(state.called, "DBusInterface raw callMethod shim should invoke the method handler");
	require(state.payload == "raw-direct", "DBusInterface raw callMethod shim should preserve payload");
#endif
}

#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
void testLegacyRawCallbacksRemainCompatible()
{
	Server server("bzperi.tests.legacy-raw", "", "", &nullGetter, &acceptingSetter);
	DBusObjectPath rootPath;
	DBusObjectPath characteristicPath;
	LegacyRawCallbackState state;

	server.configure([&](DBusObject &root) {
		rootPath = root.getPath();
		auto interface = root.addInterface(std::make_shared<DBusInterface>(root, "com.example.Legacy"));
		static const char *inArgs[] = {"s", nullptr};
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
		interface->addMethod("Ping", inArgs, "s", &legacyRawMethodHandler);

		GattService &service = root.gattServiceBegin("svc", GattUuid("9910"));
		GattCharacteristic &characteristic = service.gattCharacteristicBegin("value", GattUuid("9911"), {"read", "notify"});
		characteristicPath = characteristic.getPath();
		characteristic.onReadValue(&legacyRawCharacteristicReadHandler);
		characteristic.onUpdatedValue(&legacyRawCharacteristicUpdateHandler);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	});

	GVariant *parameters = g_variant_ref_sink(g_variant_new_string("legacy"));
	require(server.callMethod(rootPath, "com.example.Legacy", "Ping", DBusMethodCallRef(DBusConnectionRef(), DBusVariantRef(parameters), DBusMethodInvocationRef(), &state)),
		"Legacy raw DBusInterface method handler should still dispatch");
	g_variant_unref(parameters);
	require(state.methodCalled, "Legacy raw DBusInterface method handler should run");

	GVariant *readParameters = g_variant_ref_sink(g_variant_new("()"));
	require(server.callMethod(characteristicPath, "org.bluez.GattCharacteristic1", "ReadValue", DBusMethodCallRef(DBusConnectionRef(), DBusVariantRef(readParameters), DBusMethodInvocationRef(), &state)),
		"Legacy raw GattCharacteristic read handler should still dispatch");
	g_variant_unref(readParameters);
	require(state.characteristicReadCalled, "Legacy raw GattCharacteristic read handler should run");

	auto characteristicInterface = std::dynamic_pointer_cast<const GattCharacteristic>(
		server.findInterface(characteristicPath, "org.bluez.GattCharacteristic1"));
	require(characteristicInterface != nullptr, "Expected to resolve characteristic interface for legacy update test");
	require(characteristicInterface->callOnUpdatedValue(DBusUpdateRef(DBusConnectionRef(), &state)),
		"Legacy raw GattCharacteristic updated handler should still dispatch");
	require(state.characteristicUpdatedCalled, "Legacy raw GattCharacteristic updated handler should run");
}
#endif

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
			[](const GattCharacteristic &self, const std::string &methodName, DBusMethodCallRef methodCall) {
				auto &state = *static_cast<CharacteristicState *>(methodCall.userData());
				state.readCalled = true;
				state.hadParameters = static_cast<bool>(methodCall.parameters());
				state.objectPath = self.getPath().toString();
				state.methodName = methodName;
			});

		characteristic.onUpdatedValue(
			[](const GattCharacteristic &self, DBusUpdateRef update) -> bool {
				auto &state = *static_cast<CharacteristicState *>(update.userData());
				state.updateCalled = true;
				state.updateUserDataMatched = update.userData() == &state;
				state.objectPath = self.getPath().toString();
				return true;
			});

		characteristic.addProperty<GattCharacteristic>(
			"DynamicValue",
			DBusVariantRef(g_variant_new_string("seed")),
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

		characteristic.addProperty<GattCharacteristic>(
			"DynamicValueCallRef",
			DBusVariantRef(g_variant_new_string("seed-call")),
			[](DBusPropertyCallRef call) -> DBusVariantRef {
				auto &state = *static_cast<PropertyState *>(call.userData());
				state.getterCalls += 1;
				GVariant *value = g_variant_ref_sink(g_variant_new_string("computed-call"));
				return DBusVariantRef(value);
			},
			[](DBusPropertyCallRef call) -> bool {
				auto &state = *static_cast<PropertyState *>(call.userData());
				state.setterCalls += 1;
				state.lastSetValue = variantString(call.value().get());
				return true;
			});
	});

	const GattProperty *serviceUuid = server.findProperty(servicePath, "org.bluez.GattService1", "UUID");
	require(serviceUuid != nullptr, "GattService UUID property should be discoverable");
	expectVariantString(serviceUuid->getValueRef().get(), GattUuid("1234").toString128(), "service UUID property value");

	GVariant *readParameters = g_variant_ref_sink(g_variant_new("()"));
	require(server.callMethod(characteristicPath, "org.bluez.GattCharacteristic1", "ReadValue", DBusMethodCallRef(DBusConnectionRef(), DBusVariantRef(readParameters), DBusMethodInvocationRef(), &characteristicState)),
		"Characteristic ReadValue should dispatch via DBusMethodCallRef path");
	g_variant_unref(readParameters);

	require(characteristicState.readCalled, "Characteristic method-call read handler should run");
	require(characteristicState.hadParameters, "Characteristic method-call read handler should receive parameters");
	require(characteristicState.objectPath == characteristicPath.toString(), "Characteristic read handler should see its object path");
	require(characteristicState.methodName == "ReadValue", "Characteristic read handler should see method name");

	auto characteristicInterface = std::dynamic_pointer_cast<const GattCharacteristic>(
		server.findInterface(characteristicPath, "org.bluez.GattCharacteristic1"));
	require(characteristicInterface != nullptr, "Characteristic interface lookup should return a typed interface");
	require(characteristicInterface->callOnUpdatedValue(DBusUpdateRef(DBusConnectionRef(), &characteristicState)),
		"Characteristic update handler should be callable through wrapper path");
	require(characteristicState.updateCalled, "Characteristic update handler should run");
	require(characteristicState.updateUserDataMatched, "Characteristic update handler should preserve user data through DBusUpdateRef");

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

	const GattProperty *dynamicCallProperty = server.findProperty(characteristicPath, "org.bluez.GattCharacteristic1", "DynamicValueCallRef");
	require(dynamicCallProperty != nullptr, "Dynamic characteristic property with call-ref handlers should be discoverable");
	require(static_cast<bool>(dynamicCallProperty->getGetterCallHandler()), "Dynamic call-ref property should retain getter call handler");
	require(static_cast<bool>(dynamicCallProperty->getSetterCallHandler()), "Dynamic call-ref property should retain setter call handler");

	GVariant *getterCallValue = dynamicCallProperty->getGetterCallHandler()(DBusPropertyCallRef(
		DBusConnectionRef(),
		std::string_view(),
		characteristicPath.toString(),
		"org.bluez.GattCharacteristic1",
		"DynamicValueCallRef",
		DBusVariantRef(),
		DBusErrorRef(&error),
		&propertyState)).get();
	require(error == nullptr, "Dynamic call-ref property getter should not set GError");
	expectVariantString(getterCallValue, "computed-call", "Dynamic call-ref property getter return value");
	require(propertyState.getterCalls == 2, "Dynamic call-ref property getter should update test state");
	g_variant_unref(getterCallValue);

	GVariant *setterCallValue = g_variant_ref_sink(g_variant_new_string("written-call"));
	require(dynamicCallProperty->getSetterCallHandler()(DBusPropertyCallRef(
		DBusConnectionRef(),
		std::string_view(),
		characteristicPath.toString(),
		"org.bluez.GattCharacteristic1",
		"DynamicValueCallRef",
		DBusVariantRef(setterCallValue),
		DBusErrorRef(&error),
		&propertyState)),
		"Dynamic call-ref property setter should report success");
	require(error == nullptr, "Dynamic call-ref property setter should not set GError");
	require(propertyState.setterCalls == 2, "Dynamic call-ref property setter should update test state");
	require(propertyState.lastSetValue == "written-call", "Dynamic call-ref property setter should see the provided value");
	g_variant_unref(setterCallValue);
}

void testBluezAdapterAccessors()
{
	decltype(auto) adapter = getActiveBluezAdapter();
	auto *adapterPtr = getActiveBluezAdapterPtr();
	auto *runtimeAdapterPtr = bzp::getRuntimeBluezAdapterPtr();

	require(adapterPtr != nullptr, "Active BlueZ adapter pointer accessor should never return null");
	require(adapterPtr == &adapter, "BlueZ adapter reference and pointer accessors should resolve to the same instance");
	require(adapterPtr == getActiveBluezAdapterPtr(), "BlueZ adapter pointer accessor should remain stable across calls");
	require(&getActiveBluezAdapter() == &adapter, "BlueZ adapter reference accessor should remain stable across calls");
	require(runtimeAdapterPtr == nullptr || runtimeAdapterPtr == adapterPtr,
		"Runtime BlueZ adapter pointer should either be absent or match the currently active adapter");

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	require(&bzp::BluezAdapter::getInstance() == &adapter,
	"Legacy BlueZ adapter singleton accessor should still resolve to the active adapter instance");
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif
}

void testBluezAdapterRuntimeOwnership()
{
	auto *originalAdapter = getActiveBluezAdapterPtr();
	auto *originalRuntimeAdapter = bzp::getRuntimeBluezAdapterPtr();
	auto runtimeAdapter = makeRuntimeBluezAdapterPtr();
	setActiveBluezAdapterForRuntime(runtimeAdapter.get());

	require(bzp::getRuntimeBluezAdapterPtr() == runtimeAdapter.get(),
		"Runtime adapter accessor should expose the runtime-owned adapter directly");
	require(getActiveBluezAdapterPtr() == runtimeAdapter.get(),
		"Runtime-owned adapter should become the active adapter pointer");
	require(&getActiveBluezAdapter() == runtimeAdapter.get(),
		"Runtime-owned adapter should become the active adapter reference");

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	require(&bzp::BluezAdapter::getInstance() == runtimeAdapter.get(),
		"Legacy BlueZ adapter singleton should mirror the runtime-owned adapter when compat is enabled");
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

	setActiveBluezAdapterForRuntime(originalRuntimeAdapter);
	require(bzp::getRuntimeBluezAdapterPtr() == originalRuntimeAdapter,
		"Restoring runtime ownership should restore the previous runtime adapter pointer");
	require(getActiveBluezAdapterPtr() == originalAdapter,
		"Restoring the previous active adapter should restore the original pointer");
}

void testServerAccessorCompatibilityStorage()
{
	auto originalServer = getActiveServer();
	auto originalRuntimeServer = bzp::getRuntimeServer();
	setActiveServerForRuntime(nullptr);
	setActiveServer(nullptr);

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
	auto legacyAssignedServer = std::make_shared<Server>("bzperi.tests.legacy-server", "", "", &nullGetter, &acceptingSetter);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	bzp::TheServer = legacyAssignedServer;
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

	require(getActiveServerPtr() == legacyAssignedServer.get(),
		"Legacy TheServer assignment should synchronize into active server accessors");
	require(getActiveServer().get() == legacyAssignedServer.get(),
		"getActiveServer() should reflect legacy global assignments");
#endif

	auto accessorAssignedServer = std::make_shared<Server>("bzperi.tests.accessor-server", "", "", &nullGetter, &acceptingSetter);
	setActiveServer(accessorAssignedServer);
	require(getActiveServerPtr() == accessorAssignedServer.get(),
		"Accessor-assigned server should become the active server");
#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	require(bzp::TheServer.get() == accessorAssignedServer.get(),
	"Accessor-assigned server should mirror back into the legacy global");
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

	setActiveServer(originalServer);
	setActiveServerForRuntime(originalRuntimeServer);
}

void testServerRuntimeOwnership()
{
	auto originalServer = getActiveServer();
	auto originalRuntimeServer = bzp::getRuntimeServer();
	setActiveServerForRuntime(nullptr);
	setActiveServer(nullptr);

	auto runtimeServer = std::make_shared<Server>("bzperi.tests.runtime-server", "", "", &nullGetter, &acceptingSetter);
	setActiveServerForRuntime(runtimeServer);

	require(bzp::getRuntimeServer().get() == runtimeServer.get(),
		"Runtime server shared accessor should expose the runtime-owned server directly");
	require(bzp::getRuntimeServerPtr() == runtimeServer.get(),
		"Runtime server accessor should expose the runtime-owned server directly");
	require(getActiveServerPtr() == runtimeServer.get(),
		"Runtime-owned server should become the active server pointer");
	require(getActiveServer().get() == runtimeServer.get(),
		"Runtime-owned server should become the active server reference");

	auto compatibilityServer = std::make_shared<Server>("bzperi.tests.compat-server", "", "", &nullGetter, &acceptingSetter);
	setActiveServer(compatibilityServer);

	require(getActiveServerPtr() == runtimeServer.get(),
		"Compatibility setter should not replace the runtime-owned active server");
	require(getActiveServer().get() == runtimeServer.get(),
		"Runtime-owned server should remain visible through accessors while active");

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	require(bzp::TheServer.get() == runtimeServer.get(),
		"Legacy TheServer mirror should follow the runtime-owned server while it is active");
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

	setActiveServerForRuntime(nullptr);
	require(bzp::getRuntimeServerPtr() == nullptr,
		"Clearing runtime ownership should clear the runtime server accessor");
	require(getActiveServerPtr() == compatibilityServer.get(),
		"Clearing runtime ownership should reveal the compatibility-assigned server again");

	setActiveServer(originalServer);
	setActiveServerForRuntime(originalRuntimeServer);
}

void testUtilsVariantWrappers()
{
	GVariant *stringValue = g_variant_ref_sink(Utils::dbusVariantFromString("wrapper").get());
	expectVariantString(stringValue, "wrapper", "Utils::dbusVariantFromString");
	g_variant_unref(stringValue);

	GVariant *boolValue = g_variant_ref_sink(Utils::dbusVariantFromBoolean(true).get());
	require(g_variant_is_of_type(boolValue, G_VARIANT_TYPE_BOOLEAN), "Utils::dbusVariantFromBoolean should create a boolean variant");
	require(g_variant_get_boolean(boolValue), "Utils::dbusVariantFromBoolean should preserve the boolean payload");
	g_variant_unref(boolValue);

	GVariant *bytesValue = g_variant_ref_sink(Utils::dbusVariantFromByteArray(std::string("payload")).get());
	require(g_variant_is_of_type(bytesValue, G_VARIANT_TYPE_BYTESTRING), "Utils::dbusVariantFromByteArray should create a byte-string variant");
	require(Utils::stringFromGVariantByteArray(DBusVariantRef(bytesValue)) == "payload", "Utils::dbusVariantFromByteArray should preserve byte payloads");
	g_variant_unref(bytesValue);

	GVariant *signalPayload = g_variant_ref_sink(g_variant_new_string("signal"));
	DBusSignalRef signal(DBusConnectionRef(), "com.example.Test", "Changed", DBusVariantRef(signalPayload));
	require(!signal.connection(), "DBusSignalRef should preserve the wrapped connection state");
	require(signal.interfaceName() == "com.example.Test", "DBusSignalRef should preserve interface name");
	require(signal.signalName() == "Changed", "DBusSignalRef should preserve signal name");
	require(signal.parameters().get() == signalPayload, "DBusSignalRef should preserve the wrapped parameters");
	g_variant_unref(signalPayload);

	int updateSentinel = 0;
	void *updateUserData = &updateSentinel;
	DBusUpdateRef update(DBusConnectionRef(), updateUserData);
	require(!update.connection(), "DBusUpdateRef should preserve the wrapped connection state");
	require(update.userData() == updateUserData, "DBusUpdateRef should preserve user data");

	GVariant *notificationValue = g_variant_ref_sink(g_variant_new_string("notification"));
	DBusNotificationRef notification{DBusConnectionRef(), DBusVariantRef(notificationValue)};
	require(!notification.connection(), "DBusNotificationRef should preserve the wrapped connection state");
	require(notification.value().get() == notificationValue, "DBusNotificationRef should preserve the wrapped value");
	g_variant_unref(notificationValue);

	DBusReplyRef reply{DBusMethodInvocationRef()};
	require(!reply.invocation(), "DBusReplyRef should preserve the wrapped invocation state");

	DBusMethodCallRef methodCall(DBusConnectionRef(), DBusVariantRef(), DBusMethodInvocationRef(), nullptr);
	DBusReplyRef replyFromCall(methodCall);
	require(!replyFromCall.invocation(), "DBusReplyRef constructed from DBusMethodCallRef should preserve invocation state");
}

void testAdvertisingServiceUuidSelection()
{
	Server server("bzperi.tests.advertising", "", "", &nullGetter, &acceptingSetter);
	server.configure([](DBusObject &root) {
		root.gattServiceBegin("battery", GattUuid("180F")).gattServiceEnd();
		root.gattServiceBegin("vendor32", GattUuid("12345678")).gattServiceEnd();
		root.gattServiceBegin("custom", GattUuid("00000001-1E3C-FAD4-74E2-97A033F1BFAA")).gattServiceEnd();
	});

	const auto collected = collectGattServiceUUIDs(server);
	require(collected.size() == 3, "Expected three service UUIDs to be collected from the server tree");
	require(collected[0] == GattUuid("180F").toString128(), "Standard 16-bit service UUID should be normalized to 128-bit form");
	require(collected[1] == GattUuid("12345678").toString128(), "Standard 32-bit service UUID should be normalized to 128-bit form");
	require(collected[2] == GattUuid("00000001-1E3C-FAD4-74E2-97A033F1BFAA").toString128(),
		"Custom 128-bit service UUID should be preserved");

	BluezCapabilities legacyCaps;
	legacyCaps.maxAdvertisingDataLength = 31;
	require(!canUseExtendedAdvertising(legacyCaps), "31-byte advertising budget should remain on the legacy path");

	const auto legacySelected = selectAdvertisementServiceUUIDs(collected, legacyCaps);
	require(legacySelected.size() == 2, "Legacy advertising should only keep standard short UUIDs");
	require(legacySelected[0] == "180f", "Legacy advertising should shorten standard 16-bit UUIDs");
	require(legacySelected[1] == "12345678", "Legacy advertising should shorten standard 32-bit UUIDs");

	BluezCapabilities extendedCaps;
	extendedCaps.maxAdvertisingDataLength = 251;
	require(canUseExtendedAdvertising(extendedCaps), "Extended advertising should activate when MaxAdvLen exceeds 31 bytes");

	const auto extendedSelected = selectAdvertisementServiceUUIDs(collected, extendedCaps);
	require(extendedSelected == collected, "Extended advertising should keep the full 128-bit service UUID set");
}

void testManagedObjectsPayloadBuilder()
{
	Server server("bzperi.tests.managed-objects", "", "", &nullGetter, &acceptingSetter);
	server.configure([](DBusObject &root) {
		root.gattServiceBegin("battery", GattUuid("180F")).gattServiceEnd();
	});

	const auto payload = bzp::ServerUtils::buildManagedObjectsPayload(server);
	require(static_cast<bool>(payload), "Managed objects payload builder should return a variant");
	require(g_variant_is_of_type(payload.get(), G_VARIANT_TYPE_TUPLE),
		"Managed objects payload should be returned as a tuple");

	const std::string printed = describeVariant(payload.get());
	require(printed.find("org.bluez.GattService1") != std::string::npos,
		"Managed objects payload should include the configured GATT service interface");
	require(printed.find("/com/bzperi/tests/managed_objects/battery") != std::string::npos,
		"Managed objects payload should include sanitized object paths for configured services");
}

void testWaitHelpers()
{
	require(bzpGetServerRunState() == EUninitialized, "Unit tests should begin with the server in the uninitialized state");
	require(bzpTriggerShutdownEx() == BZP_SHUTDOWN_TRIGGER_NOT_RUNNING,
		"bzpTriggerShutdownEx should distinguish shutdown requests before startup");
	require(bzpIsServerRunning() == 0,
		"bzpIsServerRunning should report false before startup");
	require(bzpWaitForState(EUninitialized, 0) != 0, "bzpWaitForState should succeed immediately for the current state");
	require(bzpWaitForState(ERunning, 0) == 0, "bzpWaitForState should fail immediately for an unreached state");
	require(bzpWaitForStateEx(EUninitialized, 0) == BZP_WAIT_OK,
		"bzpWaitForStateEx should report OK when the requested state is already current");
	require(bzpWaitForStateEx(ERunning, 0) == BZP_WAIT_TIMEOUT,
		"bzpWaitForStateEx should report timeout for an unreached state with timeout=0");
	require(bzpWaitForState(static_cast<BZPServerRunState>(999), 0) == 0, "bzpWaitForState should reject invalid states");
	require(bzpWaitForStateEx(static_cast<BZPServerRunState>(999), 0) == BZP_WAIT_INVALID_STATE,
		"bzpWaitForStateEx should distinguish invalid states");
	require(bzpWaitEx() == BZP_WAIT_NOT_RUNNING,
		"bzpWaitEx should distinguish pre-start indefinite waits from a successful shutdown wait");
	require(bzpShutdownAndWaitEx() == BZP_WAIT_NOT_RUNNING,
		"bzpShutdownAndWaitEx should distinguish pre-start shutdown waits from a completed shutdown");
	require(bzpWaitForShutdown(0) == 0, "bzpWaitForShutdown should not report success before the server has started and stopped");
	require(bzpWaitForShutdownEx(0) == BZP_WAIT_TIMEOUT,
		"bzpWaitForShutdownEx should report timeout before the server has started and stopped");

	struct StateRestore
	{
		BZPServerRunState state;
		BZPServerHealth health;

		~StateRestore()
		{
			bzp::setServerHealth(health);
			bzp::setServerRunState(state);
		}
	} restore{bzpGetServerRunState(), bzpGetServerHealth()};

	bzp::setServerHealth(EOk);
	bzp::setServerRunState(EInitializing);
	require(bzpWaitForState(EInitializing, 0) != 0,
		"bzpWaitForState should observe the initializing state immediately");
	require(bzpWaitForState(ERunning, 0) == 0,
		"bzpWaitForState should still fail immediately while initialization is in progress");

	std::thread transitionThread([] {
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		bzp::setServerRunState(ERunning);
	});

	require(bzpWaitForState(ERunning, 250) != 0,
		"bzpWaitForState should detect a later transition to the running state");
	require(bzpWaitForStateEx(ERunning, 250) == BZP_WAIT_OK,
		"bzpWaitForStateEx should report OK for a later transition to the running state");
	transitionThread.join();

	require(bzpStartNoWait(nullptr, "", "", &nullGetter, &acceptingSetter) == 0,
		"bzpStartNoWait should preserve bzpStart argument validation");
	require(bzpStartWithBondableNoWait(nullptr, "", "", &nullGetter, &acceptingSetter, 1) == 0,
		"bzpStartWithBondableNoWait should preserve bzpStartWithBondable argument validation");
	require(bzpStartNoWaitEx(nullptr, "", "", &nullGetter, &acceptingSetter) == BZP_START_INVALID_ARGUMENT,
		"bzpStartNoWaitEx should distinguish invalid arguments");
	require(bzpStartWithBondableEx("bzperi.tests.invalid-timeout", "", "", &nullGetter, &acceptingSetter, 50, 1) == BZP_START_INVALID_TIMEOUT,
		"bzpStartWithBondableEx should distinguish invalid threaded startup timeouts");
	require(bzpStartWithBondableManualEx("bzperi.tests.manual-invalid-timeout", "", "", &nullGetter, &acceptingSetter, 1) == BZP_START_OK,
		"bzpStartWithBondableManualEx should succeed for manual startup when arguments are valid");
	bzpTriggerShutdown();
	require(bzpRunLoopDriveUntilShutdown(100) != 0,
		"Manual startup from bzpStartWithBondableManualEx should still shut down cleanly");
	require(bzpWaitForShutdownEx(0) == BZP_WAIT_OK,
		"bzpWaitForShutdownEx should report OK once manual shutdown cleanup completed");

	std::string longServiceName(256, 'a');
	require(bzpStartEx(longServiceName.c_str(), "", "", &nullGetter, &acceptingSetter, 0) == BZP_START_SERVICE_NAME_TOO_LONG,
		"bzpStartEx should distinguish service names that exceed the D-Bus limit");
}

void testManualRunLoopLifecycle()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();
		int glibCaptureEnabled = bzpGetGLibLogCaptureEnabled();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				bzpRunLoopIteration(0);
			}

			setActiveServer(activeServer);
			bzpSetGLibLogCaptureEnabled(glibCaptureEnabled);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	bzp::setServerHealth(EOk);
	bzp::setServerRunState(EUninitialized);

	require(bzpRunLoopIteration(0) == 0,
		"bzpRunLoopIteration should report no work before manual startup");
	require(bzpStartManual("bzperi.tests.manual-loop", "", "", &nullGetter, &acceptingSetter) != 0,
		"bzpStartManual should initialize the dedicated manual run loop");
	require(bzpGetServerRunState() == EInitializing,
		"bzpStartManual should leave the server in EInitializing until the host drives iterations");

	bzpTriggerShutdown();
	require(bzpGetServerRunState() == EStopping,
		"Manual run loop shutdown should transition through EStopping");
	require(bzpRunLoopDriveUntilShutdown(100) != 0,
		"bzpRunLoopDriveUntilShutdown should pump manual shutdown to completion");
	require(bzpGetServerRunState() == EStopped,
		"Manual run loop cleanup should transition the server to EStopped");
	require(bzpWaitForShutdown(0) != 0,
		"bzpWaitForShutdown should succeed immediately once manual cleanup completed");
	require(bzpRunLoopIteration(0) == 0,
		"bzpRunLoopIteration should report no work after the manual run loop has been cleaned up");
}

void testRunLoopInvoke()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				bzpRunLoopIteration(0);
			}

			setActiveServer(activeServer);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	RunLoopInvokeState state;
	require(bzpRunLoopInvoke(&runLoopInvokeHandler, &state) == 0,
		"bzpRunLoopInvoke should fail before the run loop is started");
	require(bzpStartManual("bzperi.tests.manual-invoke", "", "", &nullGetter, &acceptingSetter) != 0,
		"bzpStartManual should initialize the run loop before invoke testing");
	require(bzpRunLoopInvoke(&runLoopInvokeHandler, &state) != 0,
		"bzpRunLoopInvoke should queue work on the manual run loop");
	require(!state.called, "Run-loop invoke callback should not run until the host pumps the run loop");
	require(bzpRunLoopIteration(0) != 0,
		"bzpRunLoopIteration should dispatch queued run-loop invoke callbacks");
	require(state.called, "Queued run-loop invoke callback should run");
	require(state.receivedUserData == &state, "Run-loop invoke callback should preserve user data");
	require(state.callbackThread == std::this_thread::get_id(),
		"Manual run-loop invoke callback should execute on the pumping thread");

	bzpTriggerShutdown();
	while (bzpGetServerRunState() != EStopped)
	{
		if (!bzpRunLoopIterationFor(0))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

void testManualRunLoopThreadHandoff()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				while (bzpGetServerRunState() != EStopped)
				{
					if (!bzpRunLoopIteration(0))
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}
				}
			}

			setActiveServer(activeServer);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	bool started = false;
	std::thread starter([&started] {
		started = bzpStartManual("bzperi.tests.manual-handoff", "", "", &nullGetter, &acceptingSetter) != 0;
	});
	starter.join();
	require(started, "bzpStartManual should succeed on the starter thread");

	RunLoopInvokeState state;
	require(bzpGetServerRunState() == EInitializing,
		"Manual run-loop startup should still publish EInitializing before any iteration");
	require(bzpRunLoopInvoke(&runLoopInvokeHandler, &state) != 0,
		"bzpRunLoopInvoke should be queueable before the first manual iteration");
	require(!state.called,
		"Run-loop invoke callback should still be pending before the pumping thread iterates");
	require(bzpRunLoopIterationFor(25) != 0,
		"First bounded manual iteration should activate the run loop and dispatch queued work");
	require(state.called, "Queued callback should run after manual run-loop handoff");
	require(state.callbackThread == std::this_thread::get_id(),
		"Manual run-loop ownership should transfer to the thread performing the first iteration");

	bzpTriggerShutdown();
	while (bzpGetServerRunState() != EStopped)
	{
		if (!bzpRunLoopIterationFor(0))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

void testRunLoopIterationTimeout()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				while (bzpGetServerRunState() != EStopped)
				{
					if (!bzpRunLoopIterationFor(0))
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}
				}
			}

			setActiveServer(activeServer);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	require(bzpRunLoopIterationFor(0) == 0,
		"bzpRunLoopIterationFor should fail before manual startup");
	require(bzpStartManual("bzperi.tests.manual-timeout", "", "", &nullGetter, &acceptingSetter) != 0,
		"bzpStartManual should succeed before timeout iteration testing");

	RunLoopInvokeState state;
	require(bzpRunLoopInvoke(&runLoopInvokeHandler, &state) != 0,
		"bzpRunLoopInvoke should queue work before bounded iteration");
	require(bzpRunLoopIterationFor(25) != 0,
		"bzpRunLoopIterationFor should dispatch queued work before timing out");
	require(state.called, "Queued invoke callback should run through bounded iteration");

	bzpTriggerShutdown();
	while (bzpGetServerRunState() != EStopped)
	{
		if (!bzpRunLoopIterationFor(0))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

void testRunLoopAttachDetach()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				while (bzpGetServerRunState() != EStopped)
				{
					if (!bzpRunLoopIterationFor(0))
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}
				}
			}

			setActiveServer(activeServer);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	require(bzpRunLoopAttach() == 0, "bzpRunLoopAttach should fail before manual startup");
	require(bzpRunLoopDetach() == 0, "bzpRunLoopDetach should fail before manual startup");
	require(bzpRunLoopIsManualMode() == 0, "bzpRunLoopIsManualMode should report false before manual startup");
	require(bzpRunLoopHasOwner() == 0, "bzpRunLoopHasOwner should report false before manual startup");
	require(bzpRunLoopIsCurrentThreadOwner() == 0, "bzpRunLoopIsCurrentThreadOwner should report false before manual startup");
	require(bzpStartManual("bzperi.tests.manual-attach", "", "", &nullGetter, &acceptingSetter) != 0,
		"bzpStartManual should succeed before attach/detach testing");
	require(bzpRunLoopIsManualMode() != 0, "bzpRunLoopIsManualMode should report true after manual startup");
	require(bzpRunLoopHasOwner() == 0, "Manual startup should not assign an owner thread before attach/iterate");
	require(bzpRunLoopAttach() != 0,
		"bzpRunLoopAttach should explicitly bind the manual run loop to the current thread");
	require(bzpRunLoopHasOwner() != 0, "bzpRunLoopHasOwner should report true after explicit attach");
	require(bzpRunLoopIsCurrentThreadOwner() != 0, "bzpRunLoopIsCurrentThreadOwner should report true for the attached thread");
	require(bzpRunLoopDetach() != 0,
		"bzpRunLoopDetach should release the manual run loop from the current thread");
	require(bzpRunLoopHasOwner() == 0, "bzpRunLoopHasOwner should report false after detach");
	require(bzpRunLoopIsCurrentThreadOwner() == 0, "bzpRunLoopIsCurrentThreadOwner should report false after detach");
	require(bzpRunLoopDetach() == 0,
		"bzpRunLoopDetach should fail when no thread currently owns the manual run loop");

	RunLoopInvokeState state;
	require(bzpRunLoopInvoke(&runLoopInvokeHandler, &state) != 0,
		"bzpRunLoopInvoke should still queue work while the manual run loop is detached");

	bool workerAttach = false;
	bool workerDetach = false;
	bool workerIterated = false;
	bool workerSawOwner = false;
	bool workerSawCurrentOwner = false;
	std::thread::id workerId;
	std::thread worker([&] {
		workerId = std::this_thread::get_id();
		workerAttach = bzpRunLoopAttach() != 0;
		if (workerAttach)
		{
			workerSawOwner = bzpRunLoopHasOwner() != 0;
			workerSawCurrentOwner = bzpRunLoopIsCurrentThreadOwner() != 0;
			workerIterated = bzpRunLoopIterationFor(50) != 0;
			workerDetach = bzpRunLoopDetach() != 0;
		}
	});
	worker.join();

	require(workerAttach, "A second thread should be able to attach the detached manual run loop");
	require(workerSawOwner, "Attached worker thread should observe that the manual run loop has an owner");
	require(workerSawCurrentOwner, "Attached worker thread should observe itself as the current owner");
	require(workerIterated, "The attached worker thread should be able to drive one bounded run-loop iteration");
	require(workerDetach, "The worker thread should be able to detach the manual run loop after use");
	require(state.called, "Queued callback should execute after another thread attaches and pumps the run loop");
	require(state.callbackThread == workerId, "Queued callback should run on the thread that attached and pumped the manual run loop");
	require(bzpRunLoopHasOwner() == 0, "Main thread should observe no owner after worker detach");
	require(bzpRunLoopIsCurrentThreadOwner() == 0, "Main thread should not report ownership after worker detach");

	require(bzpRunLoopAttach() != 0,
		"Original thread should be able to re-attach the manual run loop after worker detach");
	require(bzpRunLoopIsCurrentThreadOwner() != 0,
		"Original thread should report ownership after re-attaching");
	require(bzpRunLoopDetach() != 0,
		"Original thread should be able to detach again after re-attaching");

	bzpTriggerShutdown();
	while (bzpGetServerRunState() != EStopped)
	{
		if (!bzpRunLoopIterationFor(0))
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

void testRunLoopDriveHelpers()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				bzpRunLoopDriveUntilShutdown(100);
			}

			setActiveServer(activeServer);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	require(bzpRunLoopDriveUntilState(static_cast<BZPServerRunState>(999), 0) == 0,
		"bzpRunLoopDriveUntilState should reject invalid states");
	require(bzpRunLoopDriveUntilShutdown(0) == 0,
		"bzpRunLoopDriveUntilShutdown should fail before manual startup");

	require(bzpStartManual("bzperi.tests.manual-drive", "", "", &nullGetter, &acceptingSetter) != 0,
		"bzpStartManual should succeed before drive-helper testing");
	require(bzpRunLoopDriveUntilState(EInitializing, 0) != 0,
		"bzpRunLoopDriveUntilState should succeed immediately for the current initializing state");

	bzpTriggerShutdown();
	require(bzpRunLoopDriveUntilShutdown(100) != 0,
		"bzpRunLoopDriveUntilShutdown should drive the manual loop until shutdown completes");
	require(bzpGetServerRunState() == EStopped,
		"Drive-until-shutdown helper should leave the server in EStopped");
}

void testRunLoopPollApi()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				bzpRunLoopDriveUntilShutdown(100);
			}

			setActiveServer(activeServer);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	auto drainHiddenPollUntil = [](RunLoopInvokeState &state, const char *failureMessage) {
		for (int attempt = 0; attempt < 16 && !state.called; ++attempt)
		{
			int timeoutMS = -1;
			int requiredFDCount = -1;
			int dispatchReady = 0;
			require(bzpRunLoopPollPrepare(&timeoutMS, &requiredFDCount, &dispatchReady) != 0,
				"bzpRunLoopPollPrepare should keep succeeding while hidden-poll work is pending");

			if (!dispatchReady)
			{
				require(bzpRunLoopPollCheck(nullptr, 0) == 0,
					"bzpRunLoopPollCheck should remain false when no poll descriptors were supplied");
				require(bzpRunLoopPollCancel() != 0,
					"bzpRunLoopPollCancel should release a non-ready hidden poll cycle");
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			require(bzpRunLoopPollDispatch() != 0,
				"bzpRunLoopPollDispatch should dispatch a ready hidden poll cycle");
		}

		require(state.called, failureMessage);
	};

	require(bzpRunLoopPollPrepare(nullptr, nullptr, nullptr) == 0,
		"bzpRunLoopPollPrepare should fail before manual startup");
	require(bzpRunLoopPollQuery(nullptr, 0, nullptr) == 0,
		"bzpRunLoopPollQuery should fail without an active poll cycle");
	require(bzpRunLoopPollCheck(nullptr, 0) == 0,
		"bzpRunLoopPollCheck should fail without an active poll cycle");
	require(bzpRunLoopPollDispatch() == 0,
		"bzpRunLoopPollDispatch should fail without an active poll cycle");
	require(bzpRunLoopPollCancel() == 0,
		"bzpRunLoopPollCancel should fail without an active poll cycle");

	require(bzpStartManual("bzperi.tests.manual-poll", "", "", &nullGetter, &acceptingSetter) != 0,
		"bzpStartManual should succeed before hidden poll API testing");

	RunLoopInvokeState firstDispatch;
	require(bzpRunLoopInvoke(&runLoopInvokeHandler, &firstDispatch) != 0,
		"bzpRunLoopInvoke should queue work before a hidden poll cycle");

	int timeoutMS = -2;
	int requiredFDCount = -1;
	int dispatchReady = 0;
	require(bzpRunLoopPollPrepare(&timeoutMS, &requiredFDCount, &dispatchReady) != 0,
		"bzpRunLoopPollPrepare should succeed after manual startup");
	require(bzpRunLoopHasOwner() != 0,
		"bzpRunLoopPollPrepare should implicitly bind the manual run loop to the current thread");
	require(bzpRunLoopIsCurrentThreadOwner() != 0,
		"bzpRunLoopPollPrepare should report the current thread as the owner");
	require(dispatchReady != 0,
		"Queued idle work should make the hidden poll cycle immediately dispatch-ready");

	int queriedFDCount = -1;
	require(bzpRunLoopPollQuery(nullptr, 0, &queriedFDCount) != 0,
		"bzpRunLoopPollQuery should support descriptor-count discovery");
	require(queriedFDCount >= 0,
		"Hidden poll descriptor discovery should report a non-negative descriptor count");
	std::vector<BZPPollFD> firstPollFDs(static_cast<size_t>(queriedFDCount));
	if (queriedFDCount > 0)
	{
		require(bzpRunLoopPollQuery(firstPollFDs.data(), queriedFDCount, &queriedFDCount) != 0,
			"bzpRunLoopPollQuery should populate the descriptor buffer when enough storage is provided");
	}
	require(bzpRunLoopPollCheck(queriedFDCount > 0 ? firstPollFDs.data() : nullptr, queriedFDCount) != 0,
		"bzpRunLoopPollCheck should report ready after a prepared hidden poll cycle");
	require(bzpRunLoopPollDispatch() != 0,
		"bzpRunLoopPollDispatch should dispatch queued hidden poll work");
	drainHiddenPollUntil(firstDispatch,
		"Hidden poll dispatch should eventually execute the queued run-loop callback");

	RunLoopInvokeState canceledDispatch;
	require(bzpRunLoopInvoke(&runLoopInvokeHandler, &canceledDispatch) != 0,
		"bzpRunLoopInvoke should queue work before cancel testing");
	require(bzpRunLoopPollPrepare(&timeoutMS, &requiredFDCount, &dispatchReady) != 0,
		"bzpRunLoopPollPrepare should allow a second hidden poll cycle");
	require(bzpRunLoopIteration(0) == 0,
		"bzpRunLoopIteration should reject mixed use while a hidden poll cycle is active");
	require(bzpRunLoopPollCancel() != 0,
		"bzpRunLoopPollCancel should release the active hidden poll cycle");
	require(!canceledDispatch.called,
		"Canceling a hidden poll cycle should not dispatch the queued callback");
	require(bzpRunLoopPollDispatch() == 0,
		"bzpRunLoopPollDispatch should fail after the active hidden poll cycle has been canceled");

	require(bzpRunLoopPollPrepare(&timeoutMS, &requiredFDCount, &dispatchReady) != 0,
		"bzpRunLoopPollPrepare should succeed again after canceling the previous cycle");
	int resumedFDCount = -1;
	require(bzpRunLoopPollQuery(nullptr, 0, &resumedFDCount) != 0,
		"bzpRunLoopPollQuery should support discovery after a canceled cycle");
	std::vector<BZPPollFD> resumedPollFDs(static_cast<size_t>(resumedFDCount));
	if (resumedFDCount > 0)
	{
		require(bzpRunLoopPollQuery(resumedPollFDs.data(), resumedFDCount, &resumedFDCount) != 0,
			"bzpRunLoopPollQuery should refill descriptors after a canceled cycle");
	}
	require(bzpRunLoopPollCheck(resumedFDCount > 0 ? resumedPollFDs.data() : nullptr, resumedFDCount) != 0,
		"bzpRunLoopPollCheck should report ready after re-preparing a canceled cycle");
	require(bzpRunLoopPollDispatch() != 0,
		"bzpRunLoopPollDispatch should still work after a canceled cycle");
	drainHiddenPollUntil(canceledDispatch,
		"Queued callback should remain pending until a later hidden poll dispatch");

	bzpTriggerShutdown();
	require(bzpRunLoopDriveUntilShutdown(100) != 0,
		"bzpRunLoopDriveUntilShutdown should still clean up after hidden poll API use");
}

void testRunLoopExResults()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				bzpRunLoopDriveUntilShutdown(100);
			}

			setActiveServer(activeServer);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	RunLoopInvokeState state;
	require(bzpRunLoopIterationEx(0) == BZP_RUN_LOOP_NOT_MANUAL_MODE,
		"bzpRunLoopIterationEx should distinguish pre-start manual-mode misuse");
	require(bzpRunLoopAttachEx() == BZP_RUN_LOOP_NOT_MANUAL_MODE,
		"bzpRunLoopAttachEx should distinguish pre-start manual-mode misuse");
	require(bzpRunLoopDetachEx() == BZP_RUN_LOOP_NOT_MANUAL_MODE,
		"bzpRunLoopDetachEx should distinguish pre-start manual-mode misuse");
	require(bzpRunLoopInvokeEx(nullptr, nullptr) == BZP_RUN_LOOP_INVALID_ARGUMENT,
		"bzpRunLoopInvokeEx should distinguish null callbacks");
	require(bzpRunLoopInvokeEx(&runLoopInvokeHandler, &state) == BZP_RUN_LOOP_NOT_ACTIVE,
		"bzpRunLoopInvokeEx should distinguish the lack of an active run loop before startup");
	require(bzpRunLoopPollPrepareEx(nullptr, nullptr, nullptr) == BZP_RUN_LOOP_NOT_MANUAL_MODE,
		"bzpRunLoopPollPrepareEx should distinguish pre-start manual-mode misuse");
	require(bzpRunLoopPollQueryEx(nullptr, 0, nullptr) == BZP_RUN_LOOP_NO_POLL_CYCLE,
		"bzpRunLoopPollQueryEx should distinguish the lack of an active poll cycle");
	require(bzpRunLoopPollDispatchEx() == BZP_RUN_LOOP_NO_POLL_CYCLE,
		"bzpRunLoopPollDispatchEx should distinguish the lack of an active poll cycle");
	require(bzpRunLoopDriveUntilStateEx(static_cast<BZPServerRunState>(999), 0) == BZP_RUN_LOOP_INVALID_STATE,
		"bzpRunLoopDriveUntilStateEx should distinguish invalid target states");
	require(bzpRunLoopDriveUntilShutdownEx(0) == BZP_RUN_LOOP_NOT_MANUAL_MODE,
		"bzpRunLoopDriveUntilShutdownEx should distinguish pre-start manual-mode misuse");

	require(bzpStartManual("bzperi.tests.manual-ex-results", "", "", &nullGetter, &acceptingSetter) != 0,
		"bzpStartManual should succeed before run-loop Ex testing");
	require(bzpRunLoopDriveUntilStateEx(EInitializing, 0) == BZP_RUN_LOOP_OK,
		"bzpRunLoopDriveUntilStateEx should succeed immediately for the current initializing state");
	require(bzpRunLoopAttachEx() == BZP_RUN_LOOP_OK,
		"bzpRunLoopAttachEx should succeed after manual startup");

	BZPRunLoopResult wrongThreadDetach = BZP_RUN_LOOP_OK;
	std::thread wrongThread([&wrongThreadDetach] {
		wrongThreadDetach = bzpRunLoopDetachEx();
	});
	wrongThread.join();
	require(wrongThreadDetach == BZP_RUN_LOOP_WRONG_THREAD,
		"bzpRunLoopDetachEx should distinguish wrong-thread detach attempts");

	require(bzpRunLoopDetachEx() == BZP_RUN_LOOP_OK,
		"bzpRunLoopDetachEx should succeed for the current owner thread");
	require(bzpRunLoopDetachEx() == BZP_RUN_LOOP_NOT_ATTACHED,
		"bzpRunLoopDetachEx should distinguish missing ownership");

	require(bzpRunLoopInvokeEx(&runLoopInvokeHandler, &state) == BZP_RUN_LOOP_OK,
		"bzpRunLoopInvokeEx should queue work after manual startup");
	require(bzpRunLoopIterationForEx(25) == BZP_RUN_LOOP_OK,
		"bzpRunLoopIterationForEx should report dispatched work");
	require(state.called,
		"bzpRunLoopIterationForEx should still dispatch queued invoke work");

	RunLoopInvokeState pollState;
	require(bzpRunLoopInvokeEx(&runLoopInvokeHandler, &pollState) == BZP_RUN_LOOP_OK,
		"bzpRunLoopInvokeEx should queue work before hidden poll Ex testing");

	int timeoutMS = -1;
	int requiredFDCount = -1;
	int dispatchReady = 0;
	require(bzpRunLoopPollPrepareEx(&timeoutMS, &requiredFDCount, &dispatchReady) == BZP_RUN_LOOP_OK,
		"bzpRunLoopPollPrepareEx should succeed after manual startup");
	require(bzpRunLoopIterationEx(0) == BZP_RUN_LOOP_POLL_CYCLE_ACTIVE,
		"bzpRunLoopIterationEx should distinguish mixed use while a hidden poll cycle is active");

	int queriedFDCount = -1;
	require(bzpRunLoopPollQueryEx(nullptr, 0, &queriedFDCount) == BZP_RUN_LOOP_OK,
		"bzpRunLoopPollQueryEx should support descriptor-count discovery");
	if (queriedFDCount > 0)
	{
		std::vector<BZPPollFD> tooSmallPollFDs(static_cast<size_t>(std::max(1, queriedFDCount - 1)));
		require(bzpRunLoopPollQueryEx(
			tooSmallPollFDs.data(),
			queriedFDCount - 1,
			&queriedFDCount) == BZP_RUN_LOOP_BUFFER_TOO_SMALL,
			"bzpRunLoopPollQueryEx should distinguish descriptor buffers that are too small");
	}

	require(bzpRunLoopPollCancelEx() == BZP_RUN_LOOP_OK,
		"bzpRunLoopPollCancelEx should succeed for an active hidden poll cycle");
	require(!pollState.called,
		"Canceling the hidden poll cycle should keep queued work pending");
	require(bzpRunLoopDriveUntilShutdownEx(100) == BZP_RUN_LOOP_OK,
		"bzpRunLoopDriveUntilShutdownEx should drive manual shutdown to completion");
}

void testShutdownTriggerEx()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				bzpRunLoopDriveUntilShutdown(100);
			}

			setActiveServer(activeServer);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	require(bzpTriggerShutdownEx() == BZP_SHUTDOWN_TRIGGER_NOT_RUNNING,
		"bzpTriggerShutdownEx should report NOT_RUNNING before startup");

	require(bzpStartManual("bzperi.tests.shutdown-trigger", "", "", &nullGetter, &acceptingSetter) != 0,
		"bzpStartManual should succeed before shutdown-trigger testing");
	require(bzpTriggerShutdownEx() == BZP_SHUTDOWN_TRIGGER_OK,
		"bzpTriggerShutdownEx should report OK for the first shutdown request");
	require(bzpGetServerRunState() == EStopping,
		"bzpTriggerShutdownEx should transition the server into EStopping");
	require(bzpTriggerShutdownEx() == BZP_SHUTDOWN_TRIGGER_ALREADY_STOPPING,
		"bzpTriggerShutdownEx should distinguish repeated shutdown requests while stopping");
	require(bzpRunLoopDriveUntilShutdownEx(100) == BZP_RUN_LOOP_OK,
		"Manual shutdown should still drain to completion after trigger testing");
	require(bzpTriggerShutdownEx() == BZP_SHUTDOWN_TRIGGER_NOT_RUNNING,
		"bzpTriggerShutdownEx should report NOT_RUNNING after shutdown completes");
}

void testQueryExHelpers()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();
		BZPGLibLogCaptureMode mode = bzpGetGLibLogCaptureMode();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				bzpRunLoopDriveUntilShutdown(100);
			}

			bzpUpdateQueueClear();
			bzpSetGLibLogCaptureMode(mode);
			setActiveServer(activeServer);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	int value = -1;

	require(bzpGetGLibLogCaptureEnabledEx(nullptr) == BZP_QUERY_INVALID_ARGUMENT,
		"bzpGetGLibLogCaptureEnabledEx should reject null outputs");
	require(bzpGetGLibLogCaptureEnabledEx(&value) == BZP_QUERY_OK,
		"bzpGetGLibLogCaptureEnabledEx should succeed with a valid output");
	require(value == bzpGetGLibLogCaptureEnabled(),
		"bzpGetGLibLogCaptureEnabledEx should match the legacy boolean getter");

	require(bzpIsGLibLogCaptureInstalledEx(nullptr) == BZP_QUERY_INVALID_ARGUMENT,
		"bzpIsGLibLogCaptureInstalledEx should reject null outputs");
	require(bzpIsGLibLogCaptureInstalledEx(&value) == BZP_QUERY_OK,
		"bzpIsGLibLogCaptureInstalledEx should succeed with a valid output");
	require(value == bzpIsGLibLogCaptureInstalled(),
		"bzpIsGLibLogCaptureInstalledEx should match the legacy boolean getter");

	bzpUpdateQueueClear();
	require(bzpUpdateQueueIsEmptyEx(nullptr) == BZP_QUERY_INVALID_ARGUMENT,
		"bzpUpdateQueueIsEmptyEx should reject null outputs");
	require(bzpUpdateQueueSizeEx(nullptr) == BZP_QUERY_INVALID_ARGUMENT,
		"bzpUpdateQueueSizeEx should reject null outputs");
	require(bzpUpdateQueueClearEx(nullptr) == BZP_QUERY_INVALID_ARGUMENT,
		"bzpUpdateQueueClearEx should reject null outputs");
	require(bzpUpdateQueueIsEmptyEx(&value) == BZP_QUERY_OK && value != 0,
		"bzpUpdateQueueIsEmptyEx should report an empty queue after clearing");
	require(bzpUpdateQueueSizeEx(&value) == BZP_QUERY_OK && value == 0,
		"bzpUpdateQueueSizeEx should report zero entries after clearing");

	require(bzpNotifyUpdatedCharacteristic("/com/example/query-ex") != 0,
		"Legacy enqueue API should still populate the queue for Ex query testing");
	require(bzpUpdateQueueIsEmptyEx(&value) == BZP_QUERY_OK && value == 0,
		"bzpUpdateQueueIsEmptyEx should report a non-empty queue after enqueue");
	require(bzpUpdateQueueSizeEx(&value) == BZP_QUERY_OK && value == 1,
		"bzpUpdateQueueSizeEx should report the enqueued entry count");
	require(bzpUpdateQueueClearEx(&value) == BZP_QUERY_OK && value == 1,
		"bzpUpdateQueueClearEx should report the number of cleared entries");
	require(bzpUpdateQueueIsEmptyEx(&value) == BZP_QUERY_OK && value != 0,
		"bzpUpdateQueueClearEx should leave the queue empty");

	char element[256] = {};
	require(bzpNotifyUpdatedCharacteristic("/com/example/query-ex") != 0,
		"Legacy enqueue API should still repopulate the queue after clear testing");
	require(bzpPopUpdateQueueEx(element, sizeof(element), 0) == BZP_UPDATE_QUEUE_OK,
		"bzpPopUpdateQueueEx should still interoperate with the queue query helpers");

	require(bzpIsServerRunningEx(nullptr) == BZP_QUERY_INVALID_ARGUMENT,
		"bzpIsServerRunningEx should reject null outputs");
	require(bzpIsServerRunningEx(&value) == BZP_QUERY_OK,
		"bzpIsServerRunningEx should succeed with a valid output");
	require(value == bzpIsServerRunning(),
		"bzpIsServerRunningEx should match the legacy running predicate");

	require(bzpRunLoopIsManualModeEx(nullptr) == BZP_QUERY_INVALID_ARGUMENT,
		"bzpRunLoopIsManualModeEx should reject null outputs");
	require(bzpRunLoopHasOwnerEx(nullptr) == BZP_QUERY_INVALID_ARGUMENT,
		"bzpRunLoopHasOwnerEx should reject null outputs");
	require(bzpRunLoopIsCurrentThreadOwnerEx(nullptr) == BZP_QUERY_INVALID_ARGUMENT,
		"bzpRunLoopIsCurrentThreadOwnerEx should reject null outputs");

	require(bzpRunLoopIsManualModeEx(&value) == BZP_QUERY_OK && value == 0,
		"bzpRunLoopIsManualModeEx should report false before manual startup");
	require(bzpRunLoopHasOwnerEx(&value) == BZP_QUERY_OK && value == 0,
		"bzpRunLoopHasOwnerEx should report false before manual startup");
	require(bzpRunLoopIsCurrentThreadOwnerEx(&value) == BZP_QUERY_OK && value == 0,
		"bzpRunLoopIsCurrentThreadOwnerEx should report false before manual startup");

	require(bzpStartManual("bzperi.tests.query-ex", "", "", &nullGetter, &acceptingSetter) != 0,
		"bzpStartManual should succeed before Ex query helper testing");
	require(bzpRunLoopIsManualModeEx(&value) == BZP_QUERY_OK && value != 0,
		"bzpRunLoopIsManualModeEx should report true after manual startup");
	require(bzpRunLoopHasOwnerEx(&value) == BZP_QUERY_OK && value == 0,
		"bzpRunLoopHasOwnerEx should report no owner before attachment");
	require(bzpRunLoopAttachEx() == BZP_RUN_LOOP_OK,
		"bzpRunLoopAttachEx should succeed before owner-state query testing");
	require(bzpRunLoopHasOwnerEx(&value) == BZP_QUERY_OK && value != 0,
		"bzpRunLoopHasOwnerEx should report true after attachment");
	require(bzpRunLoopIsCurrentThreadOwnerEx(&value) == BZP_QUERY_OK && value != 0,
		"bzpRunLoopIsCurrentThreadOwnerEx should report true for the attached owner thread");
	require(bzpTriggerShutdownEx() == BZP_SHUTDOWN_TRIGGER_OK,
		"Shutdown should still be requestable during Ex query helper testing");
	require(bzpRunLoopDriveUntilShutdownEx(100) == BZP_RUN_LOOP_OK,
		"Manual shutdown should still drain after Ex query helper testing");
}

void testDBusMethodTypeMismatchIsSafe()
{
	Server server("bzperi.tests.method-mismatch", "", "", &nullGetter, &acceptingSetter);
	std::shared_ptr<DBusInterface> interface;
	bool called = false;

	server.configure([&](DBusObject &root) {
		interface = root.addInterface(std::make_shared<DBusInterface>(root, "com.example.Mismatch"));
	});

	require(interface != nullptr, "Expected a test DBusInterface");

	static const char *inArgs[] = {"s", nullptr};
	DBusMethod method(
		interface.get(),
		"Ping",
		inArgs,
		"s",
		[&called](const DBusInterface &, DBusConnectionRef, const std::string &, DBusVariantRef, DBusMethodInvocationRef, void *) {
			called = true;
		});

	GVariant *parameters = g_variant_ref_sink(g_variant_new_string("payload"));
	method.call<GattCharacteristic>(
		DBusMethodCallRef(DBusConnectionRef(), DBusVariantRef(parameters), DBusMethodInvocationRef(), nullptr),
		interface->getPath(),
		interface->getName(),
		"Ping",
		"com.example.Mismatch.NotImplemented");
	g_variant_unref(parameters);

	require(!called, "Type-mismatched DBusMethod dispatch should not invoke the handler");
}

void testGLibLogCaptureToggle()
{
	const int original = bzpGetGLibLogCaptureEnabled();
	const auto originalMode = bzpGetGLibLogCaptureMode();
	const auto originalTargets = bzpGetGLibLogCaptureTargets();
	const auto originalDomains = bzpGetGLibLogCaptureDomains();
	const auto configuredMode = bzpGetConfiguredGLibLogCaptureMode();
	const auto configuredTargets = bzpGetConfiguredGLibLogCaptureTargets();
	const auto configuredDomains = bzpGetConfiguredGLibLogCaptureDomains();
	require(configuredMode == static_cast<BZPGLibLogCaptureMode>(BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE_VALUE),
		"Configured GLib log capture mode should match the build-time default");
	require(configuredTargets == static_cast<unsigned int>(BZP_DEFAULT_GLIB_LOG_CAPTURE_TARGETS_VALUE),
		"Configured GLib log capture targets should match the build-time default");
	require(configuredDomains == static_cast<unsigned int>(BZP_DEFAULT_GLIB_LOG_CAPTURE_DOMAINS_VALUE),
		"Configured GLib log capture domains should match the build-time default");

	struct RestoreState
	{
		BZPGLibLogCaptureMode mode;
		unsigned int targets;
		unsigned int domains;

		~RestoreState()
		{
			while (bzpIsGLibLogCaptureInstalled() != 0)
			{
				if (!bzpRestoreGLibLogCapture())
				{
					break;
				}
			}

			bzpSetGLibLogCaptureTargets(targets);
			bzpSetGLibLogCaptureDomains(domains);
			bzpSetGLibLogCaptureMode(mode);
		}
	} restore{originalMode, originalTargets, originalDomains};

	bzpSetGLibLogCaptureMode(configuredMode);
	require(bzpGetGLibLogCaptureMode() == configuredMode,
		"Current GLib log capture mode should be settable back to the configured default");
	bzpSetGLibLogCaptureTargets(configuredTargets);
	require(bzpGetGLibLogCaptureTargets() == configuredTargets,
		"Current GLib log capture targets should be settable back to the configured default");
	bzpSetGLibLogCaptureDomains(configuredDomains);
	require(bzpGetGLibLogCaptureDomains() == configuredDomains,
		"Current GLib log capture domains should be settable back to the configured default");
	require(bzpSetGLibLogCaptureModeEx(static_cast<BZPGLibLogCaptureMode>(99)) == BZP_GLIB_LOG_CAPTURE_MODE_SET_INVALID_MODE,
		"GLib log capture mode Ex setter should distinguish invalid modes");
	require(bzpSetGLibLogCaptureTargetsEx(0) == BZP_GLIB_LOG_CAPTURE_TARGETS_SET_INVALID_TARGETS,
		"GLib log capture targets Ex setter should reject an empty capture mask");
	require(bzpSetGLibLogCaptureTargetsEx(0x80U) == BZP_GLIB_LOG_CAPTURE_TARGETS_SET_INVALID_TARGETS,
		"GLib log capture targets Ex setter should reject unknown capture bits");
	require(bzpSetGLibLogCaptureDomainsEx(0) == BZP_GLIB_LOG_CAPTURE_DOMAINS_SET_INVALID_DOMAINS,
		"GLib log capture domains Ex setter should reject an empty domain mask");
	require(bzpSetGLibLogCaptureDomainsEx(0x80U) == BZP_GLIB_LOG_CAPTURE_DOMAINS_SET_INVALID_DOMAINS,
		"GLib log capture domains Ex setter should reject unknown domain bits");
	require(bzpSetGLibLogCaptureTargetsEx(BZP_GLIB_LOG_CAPTURE_TARGET_LOG) == BZP_GLIB_LOG_CAPTURE_TARGETS_SET_OK,
		"GLib log capture targets Ex setter should accept log-only capture");
	require(bzpGetGLibLogCaptureTargets() == BZP_GLIB_LOG_CAPTURE_TARGET_LOG,
		"GLib log capture target getter should expose the configured mask");
	require(bzpSetGLibLogCaptureDomainsEx(BZP_GLIB_LOG_CAPTURE_DOMAIN_BLUEZ) == BZP_GLIB_LOG_CAPTURE_DOMAINS_SET_OK,
		"GLib log capture domains Ex setter should accept a BlueZ-only filter");
	require(bzpGetGLibLogCaptureDomains() == BZP_GLIB_LOG_CAPTURE_DOMAIN_BLUEZ,
		"GLib log capture domain getter should expose the configured mask");

	bzpSetGLibLogCaptureEnabled(0);
	require(bzpGetGLibLogCaptureEnabled() == 0, "GLib log capture toggle should disable capture");

	bzpSetGLibLogCaptureEnabled(1);
	require(bzpGetGLibLogCaptureEnabled() == 1, "GLib log capture toggle should enable capture");
	require(bzpGetGLibLogCaptureMode() == BZP_GLIB_LOG_CAPTURE_AUTOMATIC,
		"Legacy GLib log capture toggle should map enabled=true to automatic mode");

	bzpSetGLibLogCaptureMode(BZP_GLIB_LOG_CAPTURE_HOST_MANAGED);
	require(bzpSetGLibLogCaptureModeEx(BZP_GLIB_LOG_CAPTURE_HOST_MANAGED) == BZP_GLIB_LOG_CAPTURE_MODE_SET_OK,
		"GLib log capture mode Ex setter should report success for host-managed mode");
	require(bzpGetGLibLogCaptureMode() == BZP_GLIB_LOG_CAPTURE_HOST_MANAGED,
		"Explicit GLib log capture mode should support host-managed integration");
	require(bzpGetGLibLogCaptureEnabled() == 0,
		"Legacy enabled query should report false when host-managed mode disables automatic startup capture");
	require(bzpRestoreGLibLogCaptureEx() == BZP_GLIB_LOG_CAPTURE_RESULT_NOT_INSTALLED,
		"Explicit GLib log capture restore should report not-installed before any host-managed install");
	struct RestoreDefaultHandler
	{
		GLogFunc previousHandler = g_log_set_default_handler(&ignoreGLog, nullptr);

		~RestoreDefaultHandler()
		{
			g_log_set_default_handler(previousHandler, nullptr);
		}
	} defaultHandlerRestore;
	require(bzpInstallGLibLogCaptureEx() == BZP_GLIB_LOG_CAPTURE_RESULT_OK,
		"Host-managed GLib log capture Ex API should install explicitly");
	require(bzpInstallGLibLogCapture() != 0,
		"Legacy host-managed GLib log capture install wrapper should succeed");
	require(bzpIsGLibLogCaptureInstalled() != 0,
		"Host-managed GLib log capture should report installed after explicit install");

	struct RestoreWarnReceiver
	{
		~RestoreWarnReceiver()
		{
			bzpLogRegisterWarn(nullptr);
		}
	} warnReceiverRestore;

	capturedWarnLogs.clear();
	bzpLogRegisterWarn(&captureWarnLog);
	g_log("BlueZ", G_LOG_LEVEL_WARNING, "bluez-domain-match");
	g_log("App.Other", G_LOG_LEVEL_WARNING, "other-domain-skip");
	require(capturedWarnLogs.size() == 1,
		"GLib log capture domain filtering should only route matching domains through BzPeri receivers");
	require(capturedWarnLogs.front().find("BlueZ: bluez-domain-match") != std::string::npos,
		"GLib log capture domain filtering should preserve the matching BlueZ domain payload");

	require(bzpRestoreGLibLogCapture() != 0,
		"Legacy host-managed GLib log capture restore wrapper should succeed");
	require(bzpRestoreGLibLogCaptureEx() == BZP_GLIB_LOG_CAPTURE_RESULT_OK,
		"Host-managed GLib log capture Ex API should restore explicitly");
	require(bzpIsGLibLogCaptureInstalled() == 0,
		"Host-managed GLib log capture should report not installed after explicit restore");
	require(bzpSetGLibLogCaptureTargetsEx(BZP_GLIB_LOG_CAPTURE_TARGET_LOG | BZP_GLIB_LOG_CAPTURE_TARGET_PRINTERR)
		== BZP_GLIB_LOG_CAPTURE_TARGETS_SET_OK,
		"GLib log capture targets Ex setter should allow mixed target masks");
	require(bzpGetGLibLogCaptureTargets()
		== (BZP_GLIB_LOG_CAPTURE_TARGET_LOG | BZP_GLIB_LOG_CAPTURE_TARGET_PRINTERR),
		"GLib log capture target getter should preserve mixed target masks");
	require(bzpSetGLibLogCaptureDomainsEx(BZP_GLIB_LOG_CAPTURE_DOMAIN_GLIB | BZP_GLIB_LOG_CAPTURE_DOMAIN_GIO)
		== BZP_GLIB_LOG_CAPTURE_DOMAINS_SET_OK,
		"GLib log capture domains Ex setter should allow mixed domain masks");
	require(bzpGetGLibLogCaptureDomains()
		== (BZP_GLIB_LOG_CAPTURE_DOMAIN_GLIB | BZP_GLIB_LOG_CAPTURE_DOMAIN_GIO),
		"GLib log capture domain getter should preserve mixed domain masks");

	bzpSetGLibLogCaptureMode(BZP_GLIB_LOG_CAPTURE_DISABLED);
	require(bzpInstallGLibLogCaptureEx() == BZP_GLIB_LOG_CAPTURE_RESULT_WRONG_MODE,
		"Explicit GLib log capture install should report wrong-mode outside host-managed mode");
	require(bzpInstallGLibLogCapture() == 0,
		"Explicit GLib log capture install should fail outside host-managed mode");
	require(bzpRestoreGLibLogCaptureEx() == BZP_GLIB_LOG_CAPTURE_RESULT_WRONG_MODE,
		"Explicit GLib log capture restore should report wrong-mode outside host-managed mode");

	bzpSetGLibLogCaptureMode(BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN);
	require(bzpSetGLibLogCaptureModeEx(BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN) == BZP_GLIB_LOG_CAPTURE_MODE_SET_OK,
		"GLib log capture mode Ex setter should report success for startup-and-shutdown mode");
	require(bzpGetGLibLogCaptureMode() == BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN,
		"Explicit GLib log capture mode should support startup-and-shutdown integration");
	require(bzpGetGLibLogCaptureEnabled() == 1,
		"Legacy enabled query should report true when startup-and-shutdown automatic capture is enabled");

	bzpSetGLibLogCaptureEnabled(original);
	require(bzpGetGLibLogCaptureEnabled() == original, "GLib log capture toggle should restore the original state");
	bzpSetGLibLogCaptureTargets(originalTargets);
	require(bzpGetGLibLogCaptureTargets() == originalTargets,
		"GLib log capture targets should restore the original mask");
	bzpSetGLibLogCaptureDomains(originalDomains);
	require(bzpGetGLibLogCaptureDomains() == originalDomains,
		"GLib log capture domains should restore the original mask");
	bzpSetGLibLogCaptureMode(originalMode);
	require(bzpGetGLibLogCaptureMode() == originalMode, "GLib log capture mode should restore the original mode");
}

void testGLibLogCaptureStartupAndShutdownMode()
{
	struct RestoreState
	{
		std::shared_ptr<Server> activeServer = getActiveServer();
		BZPServerRunState runState = bzpGetServerRunState();
		BZPServerHealth health = bzpGetServerHealth();
		BZPGLibLogCaptureMode mode = bzpGetGLibLogCaptureMode();

		~RestoreState()
		{
			if (bzpGetServerRunState() != EStopped && bzpGetServerRunState() != EUninitialized)
			{
				bzpTriggerShutdown();
				bzpRunLoopDriveUntilShutdown(100);
			}

			bzpSetGLibLogCaptureMode(BZP_GLIB_LOG_CAPTURE_DISABLED);
			bzpSetGLibLogCaptureMode(mode);
			setActiveServer(activeServer);
			bzp::setServerHealth(health);
			bzp::setServerRunState(runState);
		}
	} restore;

	bzpSetGLibLogCaptureMode(BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN);
	require(bzpStartManual("bzperi.tests.capture-startup-shutdown", "", "", &nullGetter, &acceptingSetter) != 0,
		"bzpStartManual should succeed before transient GLib capture testing");
	require(bzpIsGLibLogCaptureInstalled() != 0,
		"Startup-and-shutdown mode should install GLib capture during initialization");

	bzp::setServerRunState(ERunning);
	require(bzpIsGLibLogCaptureInstalled() == 0,
		"Startup-and-shutdown mode should release GLib capture once the server reaches ERunning");

	require(bzpTriggerShutdownEx() == BZP_SHUTDOWN_TRIGGER_OK,
		"Shutdown should still be requestable while transient GLib capture mode is active");
	require(bzpIsGLibLogCaptureInstalled() != 0,
		"Startup-and-shutdown mode should re-install GLib capture for shutdown");
	require(bzpRunLoopDriveUntilShutdownEx(100) == BZP_RUN_LOOP_OK,
		"Manual shutdown should still complete in startup-and-shutdown capture mode");
	require(bzpIsGLibLogCaptureInstalled() == 0,
		"Startup-and-shutdown mode should release GLib capture after shutdown cleanup");
}

void testStructuredLoggerLevelRouting()
{
	struct RestoreReceivers
	{
		~RestoreReceivers()
		{
			bzpLogRegisterInfo(nullptr);
			bzpLogRegisterWarn(nullptr);
			bzpLogRegisterStatus(nullptr);
			bzpLogRegisterTrace(nullptr);
		}
	} restore;

	capturedInfoLogs.clear();
	capturedWarnLogs.clear();
	capturedStatusLogs.clear();
	capturedTraceLogs.clear();

	bzpLogRegisterInfo(&captureInfoLog);
	bzpLogRegisterWarn(&captureWarnLog);
	bzpLogRegisterStatus(&captureStatusLog);
	bzpLogRegisterTrace(&captureTraceLog);

	StructuredLogger logger("LoggerTest");
	logger.logConnectionEvent("/org/bluez/hci0/dev_DE_AD_BE_EF", true, 1);
	logger.log().op("Reconnect").result("Retry").status();
	logger.log().op("PollCycle").extra("prepared").trace();
	logger.log().op("Reconnect").result("Failed").error("boom").warn();

	require(capturedInfoLogs.size() == 1, "StructuredLogger info routing should capture one info message");
	require(capturedInfoLogs.front().find("[LoggerTest] op=Connection path=/org/bluez/hci0/dev_DE_AD_BE_EF result=Connected active=1") != std::string::npos,
		"StructuredLogger connection logs should use the consistent structured format");

	require(capturedStatusLogs.size() == 1, "StructuredLogger status routing should capture one status message");
	require(capturedStatusLogs.front().find("[LoggerTest] op=Reconnect result=Retry") != std::string::npos,
		"StructuredLogger status logs should preserve structured fields");

	require(capturedTraceLogs.size() == 1, "StructuredLogger trace routing should capture one trace message");
	require(capturedTraceLogs.front().find("[LoggerTest] op=PollCycle prepared") != std::string::npos,
		"StructuredLogger trace logs should preserve the structured payload");

	require(capturedWarnLogs.size() == 1, "StructuredLogger warn routing should capture one warn message");
	require(capturedWarnLogs.front().find("[LoggerTest] op=Reconnect result=Failed err=boom") != std::string::npos,
		"StructuredLogger warn logs should preserve the error payload");
}

void testUpdateEnqueueExHelpers()
{
	bzpUpdateQueueClear();

	require(bzpNotifyUpdatedCharacteristicEx(nullptr) == BZP_UPDATE_ENQUEUE_INVALID_ARGUMENT,
		"Characteristic enqueue Ex should reject null paths");
	require(bzpNotifyUpdatedDescriptorEx(nullptr) == BZP_UPDATE_ENQUEUE_INVALID_ARGUMENT,
		"Descriptor enqueue Ex should reject null paths");
	require(bzpPushUpdateQueueEx("/com/example/test", nullptr) == BZP_UPDATE_ENQUEUE_INVALID_ARGUMENT,
		"Push enqueue Ex should reject null interface names");

	require(bzpNotifyUpdatedCharacteristicEx("/com/example/test") == BZP_UPDATE_ENQUEUE_NOT_RUNNING,
		"Characteristic enqueue Ex should report not-running before startup");
	require(bzpPushUpdateQueueEx("/com/example/test", "org.bluez.GattCharacteristic1") == BZP_UPDATE_ENQUEUE_NOT_RUNNING,
		"Push enqueue Ex should report not-running before startup");

	require(bzpNotifyUpdatedCharacteristic("/com/example/legacy") != 0,
		"Legacy enqueue API should preserve its pre-start behavior");
	require(bzpUpdateQueueSize() == 1, "Legacy enqueue API should still push into the update queue");

	char element[256] = {};
	require(bzpPopUpdateQueueEx(element, sizeof(element), 0) == BZP_UPDATE_QUEUE_OK,
		"Legacy enqueue should be observable through the detailed pop API");
	require(std::string(element) == "/com/example/legacy|org.bluez.GattCharacteristic1",
		"Detailed pop API should preserve the queued object path and interface");
	require(bzpUpdateQueueIsEmpty() != 0, "Update queue should be empty after popping the test element");
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
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
		{"Legacy raw callback compatibility", testLegacyRawCallbacksRemainCompatible},
#endif
		{"Characteristic/property wrapper dispatch", testCharacteristicAndPropertyWrappers},
		{"BlueZ adapter accessors", testBluezAdapterAccessors},
		{"BlueZ adapter runtime ownership", testBluezAdapterRuntimeOwnership},
		{"Server accessor compatibility storage", testServerAccessorCompatibilityStorage},
		{"Server runtime ownership", testServerRuntimeOwnership},
		{"Utils wrapper variants", testUtilsVariantWrappers},
		{"Advertising service UUID selection", testAdvertisingServiceUuidSelection},
		{"Managed objects payload builder", testManagedObjectsPayloadBuilder},
		{"Wait helper APIs", testWaitHelpers},
		{"Manual run-loop lifecycle", testManualRunLoopLifecycle},
		{"Run-loop invoke", testRunLoopInvoke},
		{"Manual run-loop thread handoff", testManualRunLoopThreadHandoff},
		{"Run-loop iteration timeout", testRunLoopIterationTimeout},
		{"Run-loop attach/detach", testRunLoopAttachDetach},
		{"Run-loop drive helpers", testRunLoopDriveHelpers},
		{"Run-loop hidden poll API", testRunLoopPollApi},
		{"Run-loop Ex result helpers", testRunLoopExResults},
		{"Shutdown trigger Ex helper", testShutdownTriggerEx},
		{"Generic query Ex helpers", testQueryExHelpers},
		{"DBusMethod type mismatch safety", testDBusMethodTypeMismatchIsSafe},
		{"GLib log capture toggle", testGLibLogCaptureToggle},
		{"GLib log startup-shutdown mode", testGLibLogCaptureStartupAndShutdownMode},
		{"Structured logger level routing", testStructuredLoggerLevelRouting},
		{"Update enqueue Ex helpers", testUpdateEnqueueExHelpers},
	};

	int failures = 0;
	for (const auto &testCase : tests)
	{
		failures += runTest(testCase);
	}

	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
