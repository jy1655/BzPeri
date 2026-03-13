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
// Herein lies the code that manages the full initialization (including the running) of the server
//
// >>
// >>>  DISCUSSION
// >>
//
// This file contains the highest-level framework for our server:
//
//    Initialization
//    Adapter configuration (mode, settings, name, etc.)
//    GATT server registration with BlueZ
//    Signal handling (such as CTRL-C)
//    Event management
//    Graceful shutdown
//
// Want to poke around and see how things work? Here's a tip: Start at the bottom of the file and work upwards. It'll make a lot
// more sense, I promise.
//
// Want to become your own boss while working from home? (Just kidding.)
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <gio/gio.h>
#include <glib-unix.h>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <new>

#include <bzp/Server.h>
#include <bzp/BluezAdapter.h>
// Mgmt.h removed - replaced with modern BlueZ D-Bus interface
#include <bzp/DBusObject.h>
#include <bzp/DBusInterface.h>
#include <bzp/GattCharacteristic.h>
#include <bzp/GattProperty.h>
#include <bzp/Logger.h>
#include "Init.h"

namespace bzp {

//
// Constants
//

static const int kPeriodicTimerFrequencySeconds = 1;
static const int kRetryDelaySeconds = 2;
static const int kIdleFrequencyMS = 10;

//
// Retries
//

static time_t retryTimeStart = 0;

//
// Adapter configuration
//

GDBusConnection *pBusConnection = nullptr;
static guint ownedNameId = 0;
static guint periodicTimeoutId = 0;
static guint updateProcessorSourceId = 0;
static guint sigtermSourceId = 0;
static guint sigintSourceId = 0;
static std::vector<guint> registeredObjectIds;
static std::atomic<GMainLoop *> pMainLoop(nullptr);
static GMainContext *pMainContext = nullptr;
static bool bManualRunLoopMode = false;
static bool bRunLoopActivated = false;
static bool bRunLoopInstallsSignalHandlers = false;
static bool bThreadDefaultContextPushed = false;
static std::thread::id mainContextOwnerThread;
static GDBusObjectManager *pBluezObjectManager = nullptr;
static GDBusObject *pBluezAdapterObject = nullptr;
static GDBusObject *pBluezDeviceObject = nullptr;
static GDBusProxy *pBluezGattManagerProxy = nullptr;
static GDBusProxy *pBluezAdapterInterfaceProxy = nullptr;
static GDBusProxy *pBluezDeviceInterfaceProxy = nullptr;
static GDBusProxy *pBluezAdapterPropertiesInterfaceProxy = nullptr;
static bool bOwnedNameAcquired = false;
static bool bAdapterConfigured = false;
static bool bApplicationRegistered = false;
static std::string bluezGattManagerInterfaceName = "";
static Server* pServerContext = nullptr;
static BluezAdapter* pAdapterContext = nullptr;

//
// Externs
//

extern void setServerRunState(enum BZPServerRunState newState);
extern void setServerHealth(enum BZPServerHealth newHealth);
extern void restoreGLibHandlers();

//
// Forward declarations
//

static void initializationStateProcessor();
bool idleFunc(void *pUserData);
void uninit();

static Server& serverContext()
{
	return *pServerContext;
}

static BluezAdapter& adapterContext()
{
	return *pAdapterContext;
}

static GMainContext *mainContextForSources()
{
	if (pMainContext != nullptr)
	{
		return pMainContext;
	}

	if (GMainContext *threadDefault = g_main_context_get_thread_default(); threadDefault != nullptr)
	{
		return threadDefault;
	}

	return g_main_context_default();
}

static guint attachTimeoutSource(guint intervalMS, GSourceFunc callback, gpointer userData)
{
	GSource *source = g_timeout_source_new(intervalMS);
	g_source_set_callback(source, callback, userData, nullptr);
	const guint sourceId = g_source_attach(source, mainContextForSources());
	g_source_unref(source);
	return sourceId;
}

static guint attachTimeoutSecondsSource(guint intervalSeconds, GSourceFunc callback, gpointer userData)
{
	GSource *source = g_timeout_source_new_seconds(intervalSeconds);
	g_source_set_callback(source, callback, userData, nullptr);
	const guint sourceId = g_source_attach(source, mainContextForSources());
	g_source_unref(source);
	return sourceId;
}

static guint attachUnixSignalSource(int signalNumber, GSourceFunc callback, gpointer userData)
{
	GSource *source = g_unix_signal_source_new(signalNumber);
	g_source_set_callback(source, callback, userData, nullptr);
	const guint sourceId = g_source_attach(source, mainContextForSources());
	g_source_unref(source);
	return sourceId;
}

struct RunLoopInvocation
{
	void (*callback)(void *);
	void *userData;
};

struct RunLoopTimeoutWake
{
	guint sourceId = 0;
	bool fired = false;
};

struct RunLoopPollCycle
{
	bool active = false;
	bool preparedReady = false;
	bool ready = false;
	int maxPriority = G_PRIORITY_DEFAULT;
	int timeoutMS = -1;
	int requiredFDCount = 0;
	std::thread::id ownerThread;
};

static RunLoopPollCycle runLoopPollCycle;

static void resetRunLoopPollCycle()
{
	runLoopPollCycle = RunLoopPollCycle();
}

static bool hasActiveRunLoopPollCycle()
{
	return runLoopPollCycle.active;
}

static BZPRunLoopResult ensureRunLoopPollCycleThread(const char *context)
{
	if (!runLoopPollCycle.active)
	{
		Logger::warn(SSTR << context << " requires an active manual run-loop poll cycle");
		return BZP_RUN_LOOP_NO_POLL_CYCLE;
	}

	if (runLoopPollCycle.ownerThread != std::this_thread::get_id())
	{
		Logger::warn(SSTR << context << " must be called from the thread that prepared the current manual run-loop poll cycle");
		return BZP_RUN_LOOP_WRONG_THREAD;
	}

	return BZP_RUN_LOOP_OK;
}

static void releaseRunLoopPollCycle()
{
	if (runLoopPollCycle.active && pMainContext != nullptr)
	{
		g_main_context_release(pMainContext);
	}

	resetRunLoopPollCycle();
}

static void attachUpdateProcessor()
{
	updateProcessorSourceId = attachTimeoutSource
	(
		kIdleFrequencyMS,
		[](gpointer pUserData) -> gboolean
		{
			if (bzpGetServerRunState() > ERunning)
			{
				updateProcessorSourceId = 0;
				return G_SOURCE_REMOVE;
			}

			idleFunc(pUserData);
			return G_SOURCE_CONTINUE;
		},
		nullptr
	);

	if (updateProcessorSourceId == 0)
	{
		Logger::error(SSTR << "Unable to add update timer to main loop");
	}
}

static void attachShutdownSignalHandlers()
{
	sigtermSourceId = attachUnixSignalSource(SIGTERM, [](gpointer data) -> gboolean {
		sigtermSourceId = 0;
		Logger::info("SIGTERM received, initiating graceful shutdown");
		g_main_loop_quit(static_cast<GMainLoop*>(data));
		return G_SOURCE_REMOVE;
	}, pMainLoop.load(std::memory_order_acquire));
	sigintSourceId = attachUnixSignalSource(SIGINT, [](gpointer data) -> gboolean {
		sigintSourceId = 0;
		Logger::info("SIGINT received, initiating graceful shutdown");
		g_main_loop_quit(static_cast<GMainLoop*>(data));
		return G_SOURCE_REMOVE;
	}, pMainLoop.load(std::memory_order_acquire));
}

static bool activateRunLoopOnCurrentThread()
{
	if (bThreadDefaultContextPushed)
	{
		return mainContextOwnerThread == std::this_thread::get_id();
	}

	if (pMainContext == nullptr)
	{
		Logger::error("BzPeri run loop cannot be activated without a GLib main context");
		return false;
	}

	mainContextOwnerThread = std::this_thread::get_id();
	g_main_context_push_thread_default(pMainContext);
	bThreadDefaultContextPushed = true;

	if (!bRunLoopActivated)
	{
		initializationStateProcessor();
		attachUpdateProcessor();

		if (bRunLoopInstallsSignalHandlers)
		{
			attachShutdownSignalHandlers();
		}

		bRunLoopActivated = true;
	}

	return true;
}

static BZPRunLoopResult detachRunLoopFromCurrentThread()
{
	if (!bManualRunLoopMode)
	{
		Logger::warn("detachRunLoopFromCurrentThread() is only valid in manual run-loop mode");
		return BZP_RUN_LOOP_NOT_MANUAL_MODE;
	}

	if (pMainContext == nullptr)
	{
		Logger::warn("detachRunLoopFromCurrentThread() called without an active manual run loop");
		return BZP_RUN_LOOP_NOT_ACTIVE;
	}

	if (!bThreadDefaultContextPushed || mainContextOwnerThread == std::thread::id())
	{
		Logger::warn("detachRunLoopFromCurrentThread() called with no attached owner thread");
		return BZP_RUN_LOOP_NOT_ATTACHED;
	}

	if (mainContextOwnerThread != std::this_thread::get_id())
	{
		Logger::warn("detachRunLoopFromCurrentThread() must be called from the current manual run-loop owner thread");
		return BZP_RUN_LOOP_WRONG_THREAD;
	}

	if (hasActiveRunLoopPollCycle())
	{
		Logger::warn("detachRunLoopFromCurrentThread() cannot detach while a manual run-loop poll cycle is active");
		return BZP_RUN_LOOP_POLL_CYCLE_ACTIVE;
	}

	g_main_context_pop_thread_default(pMainContext);
	bThreadDefaultContextPushed = false;
	mainContextOwnerThread = std::thread::id();
	return BZP_RUN_LOOP_OK;
}

static bool initializeRunLoop(Server *serverContextPtr, BluezAdapter *adapterContextPtr, bool installSignalHandlers, bool activateImmediately)
{
	if (pMainContext != nullptr || pMainLoop.load(std::memory_order_acquire) != nullptr)
	{
		Logger::error("BzPeri run loop is already initialized");
		return false;
	}

	pServerContext = serverContextPtr;
	pAdapterContext = adapterContextPtr;
	pMainContext = g_main_context_new();
	if (pMainContext == nullptr)
	{
		Logger::error("Unable to create a dedicated GLib main context");
		pServerContext = nullptr;
		pAdapterContext = nullptr;
		return false;
	}

	Logger::debug(SSTR << "Creating GLib main loop");
	GMainLoop *mainLoop = g_main_loop_new(pMainContext, FALSE);
	if (mainLoop == nullptr)
	{
		Logger::error("Unable to create the GLib main loop");
		g_main_context_unref(pMainContext);
		pMainContext = nullptr;
		pServerContext = nullptr;
		pAdapterContext = nullptr;
		mainContextOwnerThread = std::thread::id();
		return false;
	}

	pMainLoop.store(mainLoop, std::memory_order_release);
	mainContextOwnerThread = std::thread::id();
	bRunLoopInstallsSignalHandlers = installSignalHandlers;
	bThreadDefaultContextPushed = false;
	bRunLoopActivated = false;

	return !activateImmediately || activateRunLoopOnCurrentThread();
}

static void finalizeRunLoop()
{
	Logger::info("BzPeri server main loop stopped; cleaning up");
	uninit();
	bManualRunLoopMode = false;
	mainContextOwnerThread = std::thread::id();

	setServerRunState(EStopped);
	Logger::info("BzPeri server stopped");
	restoreGLibHandlers();
}

static BZPRunLoopResult ensureRunLoopOwnerThread(const char *context)
{
	if (mainContextOwnerThread == std::thread::id() || mainContextOwnerThread == std::this_thread::get_id())
	{
		return BZP_RUN_LOOP_OK;
	}

	Logger::warn(SSTR << context << " must be called from the thread that owns the manual BzPeri run loop");
	return BZP_RUN_LOOP_WRONG_THREAD;
}

static bool finalizeManualRunLoopIfStopped()
{
	if (!bManualRunLoopMode || pMainContext == nullptr || bzpGetServerRunState() <= ERunning)
	{
		return false;
	}

	finalizeRunLoop();
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ___    _ _           __      _       _                                             _
// |_ _|__| | | ___     / /   __| | __ _| |_ __ _    _ __  _ __ ___   ___ ___  ___ ___(_)_ __   __ _
//  | |/ _` | |/ _ \   / /   / _` |/ _` | __/ _` |  | '_ \| '__/ _ \ / __/ _ \/ __/ __| | '_ \ / _` |
//  | | (_| | |  __/  / /   | (_| | (_| | || (_| |  | |_) | | | (_) | (_|  __/\__ \__ \ | | | | (_| |
// |___\__,_|_|\___| /_/     \__,_|\__,_|\__\__,_|  | .__/|_|  \___/ \___\___||___/___/_|_| |_|\__, |
//                                                  |_|                                        |___/
//
// Our idle funciton is what processes data updates. We handle this in a simple way. We update the data directly in our global
// active server object, then call `bzpPushUpdateQueue` to trigger that data to be updated (in whatever way the service responsible
// for that data() sees fit.
//
// This is done using the `bzpPushUpdateQueue` / `bzpPopUpdateQueue` methods to manage the queue of pending update messages. Each
// entry represents an interface that needs to be updated. The idleFunc calls the interface's `onUpdatedValue` method for each
// update.
//
// The idle processor will perform one update per idle tick, however, it will notify that there is more data so the idle ticks
// do not lag behind.
// ---------------------------------------------------------------------------------------------------------------------------------

// Our idle function
//
// This method is used to process data on the same thread as our main loop. This allows us to communicate with our service from
// the outside.
//
// IMPORTANT: This method must return 'true' if any work was performed, otherwise it must return 'false'. Returning 'true' will
// cause the idle loop to continue to call this method to process data at the maximum rate (which can peg the CPU at 100%.) By
// returning false when there is no work to do, we are nicer to the system.
bool idleFunc(void *pUserData)
{

	// Don't do anything unless we're running
	if (bzpGetServerRunState() != ERunning)
	{
		return false;
	}

	// Try to get an update
	const int kQueueEntryLen = 1024;
	char queueEntry[kQueueEntryLen];
	if (bzpPopUpdateQueue(queueEntry, kQueueEntryLen, 0) != 1)
	{
		return false;
	}

	std::string entryString = queueEntry;
	auto token = entryString.find('|');
	if (token == std::string::npos)
	{
		Logger::error("Queue entry was not formatted properly - could not find separating token");
		return false;
	}

	DBusObjectPath objectPath = DBusObjectPath(entryString.substr(0, token));
	std::string interfaceName = entryString.substr(token+1);

	// We have an update - call the onUpdatedValue method on the interface
	std::shared_ptr<const DBusInterface> pInterface = serverContext().findInterface(objectPath, interfaceName);
	if (nullptr == pInterface)
	{
		Logger::warn(SSTR << "Unable to find interface for update: path[" << objectPath << "], name[" << interfaceName << "]");
	}
	else
	{
		// Is it a characteristic?
		if (std::shared_ptr<const GattCharacteristic> pCharacteristic = TRY_GET_CONST_INTERFACE_OF_TYPE(pInterface, GattCharacteristic))
		{
			Logger::debug(SSTR << "Processing updated value for interface '" << interfaceName << "' at path '" << objectPath << "'");
			pCharacteristic->callOnUpdatedValue(DBusUpdateRef(pBusConnection, pUserData));
			return true;
		}
	}

	return false;
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____       _       _ _   _       _ _          _   _
// |  _ \  ___(_)_ __ (_) |_(_) __ _| (_)______ _| |_(_) ___  _ ___
// | | | |/ _ \ | '_ \| | __| |/ _` | | |_  / _` | __| |/ _ \| '_  |
// | |_| |  __/ | | | | | |_| | (_| | | |/ / (_| | |_| | (_) | | | |
// |____/ \___|_|_| |_|_|\__|_|\__,_|_|_/___\__,_|\__|_|\___/|_| |_|
//
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Perform final cleanup of various resources that were allocated while the server was initialized and/or running
void uninit()
{
	GMainLoop *mainLoop = pMainLoop.exchange(nullptr);
	GMainContext *mainContext = pMainContext;
	if (runLoopPollCycle.active && mainContext != nullptr)
	{
		g_main_context_release(mainContext);
	}
	resetRunLoopPollCycle();
	pMainContext = nullptr;
	pServerContext = nullptr;
	pAdapterContext = nullptr;

	if (nullptr != pBluezAdapterObject)
	{
		g_object_unref(pBluezAdapterObject);
		pBluezAdapterObject = nullptr;
	}

	if (nullptr != pBluezDeviceObject)
	{
		g_object_unref(pBluezDeviceObject);
		pBluezDeviceObject = nullptr;
	}

	if (nullptr != pBluezAdapterInterfaceProxy)
	{
		g_object_unref(pBluezAdapterInterfaceProxy);
		pBluezAdapterInterfaceProxy = nullptr;
	}

	if (nullptr != pBluezDeviceInterfaceProxy)
	{
		g_object_unref(pBluezDeviceInterfaceProxy);
		pBluezDeviceInterfaceProxy = nullptr;
	}

	if (nullptr != pBluezAdapterPropertiesInterfaceProxy)
	{
		g_object_unref(pBluezAdapterPropertiesInterfaceProxy);
		pBluezAdapterPropertiesInterfaceProxy = nullptr;
	}

	if (nullptr != pBluezGattManagerProxy)
	{
		g_object_unref(pBluezGattManagerProxy);
		pBluezGattManagerProxy = nullptr;
	}

	if (nullptr != pBluezObjectManager)
	{
		g_object_unref(pBluezObjectManager);
		pBluezObjectManager = nullptr;
	}

	if (!registeredObjectIds.empty())
	{
		for (guint id : registeredObjectIds)
		{
			g_dbus_connection_unregister_object(pBusConnection, id);
		}
		registeredObjectIds.clear();
	}

	if (0 != periodicTimeoutId)
	{
		g_source_remove(periodicTimeoutId);
		periodicTimeoutId = 0;
	}

	if (0 != updateProcessorSourceId)
	{
		g_source_remove(updateProcessorSourceId);
		updateProcessorSourceId = 0;
	}

	if (0 != sigtermSourceId)
	{
		g_source_remove(sigtermSourceId);
		sigtermSourceId = 0;
	}

	if (0 != sigintSourceId)
	{
		g_source_remove(sigintSourceId);
		sigintSourceId = 0;
	}

  	if (ownedNameId > 0)
  	{
		g_bus_unown_name(ownedNameId);
		ownedNameId = 0;
	}

	if (nullptr != pBusConnection)
	{
		g_object_unref(pBusConnection);
		pBusConnection = nullptr;
	}

	if (nullptr != mainLoop)
	{
		g_main_loop_unref(mainLoop);
	}

	if (nullptr != mainContext)
	{
		if (bThreadDefaultContextPushed)
		{
			g_main_context_pop_thread_default(mainContext);
		}
		g_main_context_unref(mainContext);
	}

	bThreadDefaultContextPushed = false;
	bRunLoopActivated = false;
	bRunLoopInstallsSignalHandlers = false;
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____  _           _      _
// / ___|| |__  _   _| |_ __| | _____      ___ ___
// \___ \| '_ \| | | | __/ _` |/ _ \ \ /\ / / '_  |
//  ___) | | | | |_| | || (_| | (_) \ V  V /| | | |
// |____/|_| |_|\__,_|\__\__,_|\___/ \_/\_/ |_| |_|
//
// This is how we shutdown our server gracefully.
// ---------------------------------------------------------------------------------------------------------------------------------

// Trigger a graceful, asynchronous shutdown of the server
//
// This method is non-blocking and as such, will only trigger the shutdown process but not wait for it
BZPShutdownTriggerResult shutdownEx()
{
	if (bzpGetServerRunState() == EUninitialized || bzpGetServerRunState() == EStopped)
	{
		Logger::warn("Ignoring call to shutdown (the server is not running)");
		return BZP_SHUTDOWN_TRIGGER_NOT_RUNNING;
	}

	if (bzpGetServerRunState() == EStopping)
	{
		Logger::warn("Ignoring call to shutdown (we are already shutting down)");
		return BZP_SHUTDOWN_TRIGGER_ALREADY_STOPPING;
	}

	(void)ensureAutomaticGLibCaptureForShutdown();

	// Our new state: shutting down
	setServerRunState(EStopping);

	// Shutdown our BluezAdapter
	adapterContext().shutdown();

	// If we still have a main loop, ask it to quit
	if (nullptr != pMainLoop)
	{
		g_main_loop_quit(pMainLoop);
	}

	if (pMainContext != nullptr)
	{
		g_main_context_wakeup(pMainContext);
	}

	return BZP_SHUTDOWN_TRIGGER_OK;
}

void shutdown()
{
	(void)shutdownEx();
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____           _           _ _        _   _
// |  _ \ ___ _ __(_) ___   __| (_) ___  | |_(_)_ __ ___   ___ _ __
// | |_) / _ \ '__| |/ _ \ / _` | |/ __| | __| | '_ ` _ \ / _ \ '__|
// |  __/  __/ |  | | (_) | (_| | | (__  | |_| | | | | | |  __/ |
// |_|   \___|_|  |_|\___/ \__,_|_|\___|  \__|_|_| |_| |_|\___|_|
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Periodic timer handler
//
// A periodic timer is a timer fires every so often (see kPeriodicTimerFrequencySeconds.) This is used for our initialization
// failure retries. TickEvent system removed - use g_timeout_add() for periodic operations.
gboolean onPeriodicTimer(gpointer pUserData)
{
	(void)pUserData; // Suppress unused parameter warning
	// If we're shutting down, don't do anything and stop the periodic timer
	if (bzpGetServerRunState() > ERunning)
	{
		periodicTimeoutId = 0;
		return FALSE;
	}

	// Deal with retry timers
	if (0 != retryTimeStart)
	{
		Logger::debug(SSTR << "Ticking retry timer");

		// Has the retry time expired?
		int secondsRemaining = time(nullptr) - retryTimeStart - kRetryDelaySeconds;
		if (secondsRemaining >= 0)
		{
			retryTimeStart = 0;
			initializationStateProcessor();
		}
	}

	// Note: TickEvent system removed in modernization - periodic operations should use g_timeout_add directly

	return TRUE;
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  _____                 _
// | ____|_   _____ _ __ | |_ ___
// |  _| \ \ / / _ \ '_ \| __/ __|
// | |___ \ V /  __/ | | | |_\__ )
// |_____| \_/ \___|_| |_|\__|___/
//
// Our event handlers. These are generic, as they parcel out the work to the appropriate server objects (see 'Server::Server()' for
// the code that manages event handlers.)
// ---------------------------------------------------------------------------------------------------------------------------------

// Handle D-Bus method calls
void onMethodCall
(
	GDBusConnection *pConnection,
	const gchar *pSender,
	const gchar *pObjectPath,
	const gchar *pInterfaceName,
	const gchar *pMethodName,
	GVariant *pParameters,
	GDBusMethodInvocation *pInvocation,
	gpointer pUserData
)
{
	// Convert our input path into our custom type for path management
	DBusObjectPath objectPath(pObjectPath);

	if (!serverContext().callMethod(
		objectPath,
		pInterfaceName,
		pMethodName,
		DBusMethodCallRef(pConnection, pParameters, pInvocation, pUserData)
	))
	{
		Logger::error(SSTR << " + Method not found: [" << pSender << "]:[" << objectPath << "]:[" << pInterfaceName << "]:[" << pMethodName << "]");
		const std::string notImplementedErrorName = serverContext().getOwnedName() + ".NotImplemented";
		DBusMethodInvocationRef(pInvocation).returnDbusError(notImplementedErrorName.c_str(), "This method is not implemented");
		return;
	}

	return;
}

// Handle D-Bus requests to get a property
GVariant *onGetProperty
(
	GDBusConnection  *pConnection,
	const gchar      *pSender,
	const gchar      *pObjectPath,
	const gchar      *pInterfaceName,
	const gchar      *pPropertyName,
	GError           **ppError,
	gpointer         pUserData
)
{
	// Convert our input path into our custom type for path management
	DBusObjectPath objectPath(pObjectPath);

	const GattProperty *pProperty = serverContext().findProperty(objectPath, pInterfaceName, pPropertyName);

	std::string propertyPath = std::string("[") + pSender + "]:[" + objectPath.toString() + "]:[" + pInterfaceName + "]:[" + pPropertyName + "]";
	if (!pProperty)
	{
		Logger::error(SSTR << "Property(get) not found: " << propertyPath);
	    g_set_error(ppError, G_IO_ERROR, G_IO_ERROR_FAILED, ("Property(get) not found: " + propertyPath).c_str(), pSender);
		return nullptr;
	}

	const DBusPropertyCallRef propertyCall(
		DBusConnectionRef(pConnection),
		pSender != nullptr ? std::string_view(pSender) : std::string_view(),
		objectPath.toString(),
		pInterfaceName != nullptr ? std::string_view(pInterfaceName) : std::string_view(),
		pPropertyName != nullptr ? std::string_view(pPropertyName) : std::string_view(),
		DBusVariantRef(),
		DBusErrorRef(ppError),
		pUserData);

	const auto &getterCallHandler = pProperty->getGetterCallHandler();
	const auto &getterHandler = pProperty->getGetterHandler();
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	const auto rawGetterFunc = pProperty->getGetterFunc();
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#else
	const auto rawGetterFunc = static_cast<GDBusInterfaceGetPropertyFunc>(nullptr);
#endif
	if (!getterCallHandler && !getterHandler && !rawGetterFunc)
	{
		Logger::error(SSTR << "Property(get) func not found: " << propertyPath);
	    g_set_error(ppError, G_IO_ERROR, G_IO_ERROR_FAILED, ("Property(get) func not found: " + propertyPath).c_str(), pSender);
		return nullptr;
	}

	Logger::info(SSTR << "Calling property getter: " << propertyPath);
	GVariant *pResult = nullptr;
	if (getterCallHandler)
	{
		pResult = getterCallHandler(propertyCall).get();
	}
	else if (getterHandler)
	{
		pResult = getterHandler(
			propertyCall.connection(),
			propertyCall.sender(),
			propertyCall.objectPath(),
			propertyCall.interfaceName(),
			propertyCall.propertyName(),
			propertyCall.error(),
			propertyCall.userData()).get();
	}
	else
	{
		pResult = rawGetterFunc(pConnection, pSender, objectPath.c_str(), pInterfaceName, pPropertyName, ppError, pUserData);
	}

	if (nullptr == pResult)
	{
		if (ppError != nullptr && *ppError != nullptr)
		{
			return nullptr;
		}
	    g_set_error(ppError, G_IO_ERROR, G_IO_ERROR_FAILED, ("Property(get) failed: " + propertyPath).c_str(), pSender);
	    return nullptr;
	}

	return pResult;
}

// Handle D-Bus requests to set a property
gboolean onSetProperty
(
	GDBusConnection  *pConnection,
	const gchar      *pSender,
	const gchar      *pObjectPath,
	const gchar      *pInterfaceName,
	const gchar      *pPropertyName,
	GVariant         *pValue,
	GError           **ppError,
	gpointer         pUserData
)
{
	// Convert our input path into our custom type for path management
	DBusObjectPath objectPath(pObjectPath);

	const GattProperty *pProperty = serverContext().findProperty(objectPath, pInterfaceName, pPropertyName);

	std::string propertyPath = std::string("[") + pSender + "]:[" + objectPath.toString() + "]:[" + pInterfaceName + "]:[" + pPropertyName + "]";
	if (!pProperty)
	{
		Logger::error(SSTR << "Property(set) not found: " << propertyPath);
	    g_set_error(ppError, G_IO_ERROR, G_IO_ERROR_FAILED, ("Property(set) not found: " + propertyPath).c_str(), pSender);
		return false;
	}

	const DBusPropertyCallRef propertyCall(
		DBusConnectionRef(pConnection),
		pSender != nullptr ? std::string_view(pSender) : std::string_view(),
		objectPath.toString(),
		pInterfaceName != nullptr ? std::string_view(pInterfaceName) : std::string_view(),
		pPropertyName != nullptr ? std::string_view(pPropertyName) : std::string_view(),
		DBusVariantRef(pValue),
		DBusErrorRef(ppError),
		pUserData);

	const auto &setterCallHandler = pProperty->getSetterCallHandler();
	const auto &setterHandler = pProperty->getSetterHandler();
#if BZP_ENABLE_LEGACY_RAW_GLIB_COMPAT
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
	const auto rawSetterFunc = pProperty->getSetterFunc();
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#else
	const auto rawSetterFunc = static_cast<GDBusInterfaceSetPropertyFunc>(nullptr);
#endif
	if (!setterCallHandler && !setterHandler && !rawSetterFunc)
	{
		Logger::error(SSTR << "Property(set) func not found: " << propertyPath);
	    g_set_error(ppError, G_IO_ERROR, G_IO_ERROR_FAILED, ("Property(set) func not found: " + propertyPath).c_str(), pSender);
		return false;
	}

	Logger::info(SSTR << "Calling property setter: " << propertyPath);
	const bool success = setterCallHandler
		? setterCallHandler(propertyCall)
		: setterHandler
			? setterHandler(
				propertyCall.connection(),
				propertyCall.sender(),
				propertyCall.objectPath(),
				propertyCall.interfaceName(),
				propertyCall.propertyName(),
				propertyCall.value(),
				propertyCall.error(),
				propertyCall.userData())
			: rawSetterFunc(pConnection, pSender, objectPath.c_str(), pInterfaceName, pPropertyName, pValue, ppError, pUserData);
	if (!success)
	{
		if (ppError != nullptr && *ppError != nullptr)
		{
			return false;
		}
	    g_set_error(ppError, G_IO_ERROR, G_IO_ERROR_FAILED, ("Property(set) failed: " + propertyPath).c_str(), pSender);
	    return false;
	}

	return true;
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  _____     _ _                                                                              _
// |  ___|_ _(_) |_   _ _ __ ___   _ __ ___   __ _ _ __   __ _  __ _  ___ _ __ ___   ___ _ __ | |_
// | |_ / _` | | | | | | '__/ _ \ | '_ ` _ \ / _` | '_ \ / _` |/ _` |/ _ \ '_ ` _ \ / _ \ '_ \| __|
// |  _| (_| | | | |_| | | |  __/ | | | | | | (_| | | | | (_| | (_| |  __/ | | | | |  __/ | | | |_
// |_|  \__,_|_|_|\__,_|_|  \___| |_| |_| |_|\__,_|_| |_|\__,_|\__, |\___|_| |_| |_|\___|_| |_|\__|
//                                                             |___/
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Convenience method for setting a retry timer so that operations can be continuously retried until we eventually succeed
void setRetry()
{
	retryTimeStart = time(nullptr);
}

// Convenience method for setting a retry timer so that failures (related to initialization) can be continuously retried until we
// eventually succeed.
void setRetryFailure()
{
	setRetry();
	Logger::warn(SSTR << "  + Will retry the failed operation in about " << kRetryDelaySeconds << " seconds");
}

// ---------------------------------------------------------------------------------------------------------------------------------
//   ____    _  _____ _____                  _     _             _   _
//  / ___|  / \|_   _|_   _|  _ __ ___  __ _(_)___| |_ _ __ __ _| |_(_) ___  _ ___
// | |  _  / _ \ | |   | |   | '__/ _ \/ _` | / __| __| '__/ _` | __| |/ _ \| '_  |
// | |_| |/ ___ \| |   | |   | | |  __/ (_| | \__ \ |_| | | (_| | |_| | (_) | | | |
//  \____/_/   \_\_|   |_|   |_|  \___|\__, |_|___/\__|_|  \__,_|\__|_|\___/|_| |_|
//                                     |___/
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Use the BlueZ GATT Manager proxy to register our GATT application with BlueZ
void doRegisterApplication()
{
	g_auto(GVariantBuilder) builder;
	g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
	GVariant *pParams = g_variant_new("(oa{sv})", "/", &builder);

	g_dbus_proxy_call
	(
		pBluezGattManagerProxy,         // GDBusProxy *proxy
		"RegisterApplication",          // const gchar *method_name   (ex: "GetManagedObjects")
		pParams,                        // GVariant *parameters
		G_DBUS_CALL_FLAGS_NONE,         // GDBusCallFlags flags
		-1,                             // gint timeout_msec
		nullptr,                        // GCancellable *cancellable

		// GAsyncReadyCallback callback
		[] (GObject * /*pSourceObject*/, GAsyncResult *pAsyncResult, gpointer /*pUserData*/)
		{
			GError *pError = nullptr;
			GVariant *pVariant = g_dbus_proxy_call_finish(pBluezGattManagerProxy, pAsyncResult, &pError);
			if (nullptr == pVariant)
			{
				Logger::error(SSTR << "Failed to register application: " << (nullptr == pError ? "Unknown" : pError->message));
				setRetryFailure();
			}
			else
			{
				g_variant_unref(pVariant);
				Logger::debug(SSTR << "GATT application registered with BlueZ");
				bApplicationRegistered = true;
			}

			// Keep going...
			initializationStateProcessor();
		},

		nullptr                         // gpointer user_data
	);
}

// ---------------------------------------------------------------------------------------------------------------------------------
//   ___  _     _           _                    _     _             _   _
//  / _ \| |__ (_) ___  ___| |_   _ __ ___  __ _(_)___| |_ _ __ __ _| |_(_) ___  _ ___
// | | | | '_ \| |/ _ \/ __| __| | '__/ _ \/ _` | / __| __| '__/ _` | __| |/ _ \| '_  |
// | |_| | |_) | |  __/ (__| |_  | | |  __/ (_| | \__ \ |_| | | (_| | |_| | (_) | | | |
//  \___/|_.__// |\___|\___|\__| |_|  \___|\__, |_|___/\__|_|  \__,_|\__|_|\___/|_| |_|
//           |__/                          |___/
//
// Before we can register our service(s) with BlueZ, we must first register ourselves with D-Bus. The easiest way to do this is to
// use an XML description of our D-Bus objects.
// ---------------------------------------------------------------------------------------------------------------------------------

void registerNodeHierarchy(GDBusNodeInfo *pNode, const DBusObjectPath &basePath = DBusObjectPath(), int depth = 1)
{
	std::string prefix;
	prefix.insert(0, depth * 2, ' ');

	static GDBusInterfaceVTable interfaceVtable;
	interfaceVtable.method_call = onMethodCall;
	interfaceVtable.get_property = onGetProperty;
	interfaceVtable.set_property = onSetProperty;

	GDBusInterfaceInfo **ppInterface = pNode->interfaces;

	Logger::debug(SSTR << prefix << "+ " << pNode->path);

	while(nullptr != *ppInterface)
	{
		GError *pError = nullptr;
		Logger::debug(SSTR << prefix << "    (iface: " << (*ppInterface)->name << ")");
		guint registeredObjectId = g_dbus_connection_register_object
		(
			pBusConnection,             // GDBusConnection *connection
			basePath.c_str(),           // const gchar *object_path
			*ppInterface,               // GDBusInterfaceInfo *interface_info
			&interfaceVtable,           // const GDBusInterfaceVTable *vtable
			nullptr,                    // gpointer user_data
			nullptr,                    // GDestroyNotify user_data_free_func
			&pError                     // GError **error
		);

		if (0 == registeredObjectId)
		{
			Logger::error(SSTR << "Failed to register object: " << (nullptr == pError ? "Unknown" : pError->message));

			// Cleanup and pretend like we were never here
			g_dbus_node_info_unref(pNode);
			registeredObjectIds.clear();

			// Try again later
			setRetryFailure();
			return;
		}

		// Save the registered object Id so we can clean it up later
		registeredObjectIds.push_back(registeredObjectId);

		++ppInterface;
	}

	GDBusNodeInfo **ppChild = pNode->nodes;
	while(nullptr != *ppChild)
	{
		registerNodeHierarchy(*ppChild, basePath + (*ppChild)->path, depth + 1);

		++ppChild;
	}
}

void registerObjects()
{
	// Parse each object into an XML interface tree
	for (const DBusObject &object : serverContext().getObjects())
	{
		GError *pError = nullptr;
		std::string xmlString = object.generateIntrospectionXML();
		GDBusNodeInfo *pNode = g_dbus_node_info_new_for_xml(xmlString.c_str(), &pError);
		if (nullptr == pNode)
		{
			Logger::error(SSTR << "Failed to introspect XML: " << (nullptr == pError ? "Unknown" : pError->message));
			setRetryFailure();
			return;
		}

		Logger::debug(SSTR << "Registering object hierarchy with D-Bus hierarchy");

		// Register the node hierarchy
		registerNodeHierarchy(pNode, DBusObjectPath(pNode->path));

		// Cleanup the node
		g_dbus_node_info_unref(pNode);
	}

	// Keep going
	initializationStateProcessor();
}

// ---------------------------------------------------------------------------------------------------------------------------------
//     _       _             _                               __ _                       _   _
//    / \   __| | __ _ _ __ | |_ ___ _ __    ___ ___  _ __  / _(_) __ _ _   _ _ __ __ _| |_(_) ___  _ ___
//   / _ \ / _` |/ _` | '_ \| __/ _ \ '__|  / __/ _ \| '_ \| |_| |/ _` | | | | '__/ _` | __| |/ _ \| '_  |
//  / ___ \ (_| | (_| | |_) | ||  __/ |    | (_| (_) | | | |  _| | (_| | |_| | | | (_| | |_| | (_) | | | |
// /_/   \_\__,_|\__,_| .__/ \__\___|_|     \___\___/|_| |_|_| |_|\__, |\__,_|_|  \__,_|\__|_|\___/|_| |_|
//                    |_|                                         |___/
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Configure an adapter to ensure it is setup the way we need. We turn things on that we need and turn everything else off
// (to maximize security.)
//
// Note that this only works for the first adapter (index 0). Search for kControllerIndex for information.
//
// See also: https://git.kernel.org/pub/scm/bluetooth/bluez.git/tree/doc/mgmt-api.txt
void configureAdapter()
{
	// Check for environment variables from standalone app
	const char* preferredAdapter = std::getenv("BLUEZ_ADAPTER");
	const char* listAdapters = std::getenv("BLUEZ_LIST_ADAPTERS");

	std::string adapterName = preferredAdapter ? preferredAdapter : "";
	adapterContext().setServiceNameContext(serverContext().getServiceName());
	adapterContext().setServerContext(&serverContext());

	// Initialize the modern BlueZ adapter with discovery
	auto result = adapterContext().initialize(adapterName);
	if (result.hasError())
	{
		Logger::error(SSTR << "Failed to initialize BluezAdapter: " << result.errorMessage());

		// If adapter listing was requested, try to show available adapters anyway
		if (listAdapters)
		{
			auto adapters = adapterContext().discoverAdapters();
			if (adapters.isSuccess())
			{
				Logger::info("Available BlueZ adapters:");
				for (const auto& adapter : adapters.value())
				{
					Logger::info(SSTR << "  " << adapter.path << " (" << adapter.address << ") - Powered: " << adapter.powered);
				}
			}
		}

		setRetry();
		return;
	}

	// List adapters if requested
	if (listAdapters)
	{
		auto adapters = adapterContext().discoverAdapters();
		if (adapters.isSuccess())
		{
			Logger::info("Available BlueZ adapters:");
			for (const auto& adapter : adapters.value())
			{
				Logger::info(SSTR << "  " << adapter.path << " (" << adapter.address << ") - Powered: " << adapter.powered);
			}
		}
	}

	// Get our properly truncated advertising names
	// Note: Using Mgmt for now, but these could be moved to Utils
	std::string advertisingName = Utils::truncateName(serverContext().getAdvertisingName());
	std::string advertisingShortName = Utils::truncateShortName(serverContext().getAdvertisingShortName());

	BluezAdapter& adapter = adapterContext();

	// Configure adapter settings using modern D-Bus API
	// Note: Modern BlueZ automatically handles LE when needed, no explicit LE enabling required

	// Set adapter name first (if specified)
	if (!advertisingName.empty())
	{
		Logger::info(SSTR << "Setting adapter name to '" << advertisingName << "' (with short name: '" << advertisingShortName << "')");
		auto nameResult = adapter.setName(advertisingName, advertisingShortName);
		if (nameResult.hasError())
		{
			Logger::warn(SSTR << "Failed to set adapter name: " << nameResult.errorMessage());
		}
	}

	// Set bondable state
	auto bondableResult = adapter.setBondable(serverContext().getEnableBondable());
	if (bondableResult.hasError())
	{
		Logger::warn(SSTR << "Failed to set bondable state: " << bondableResult.errorMessage());
	}

	// Note: Connectable property removed - not supported in modern BlueZ for LE
	// BLE advertising handles connectable state automatically

	// Set discoverable state
	if (serverContext().getEnableDiscoverable())
	{
		auto discoverableResult = adapter.setDiscoverable(true);
		if (discoverableResult.hasError())
		{
			Logger::warn(SSTR << "Failed to set discoverable state: " << discoverableResult.errorMessage());
		}
	}

	// Enable advertising (this also ensures the adapter is powered and connectable)
	if (serverContext().getEnableAdvertising())
	{
		auto advertisingResult = adapter.setAdvertising(true);
		if (advertisingResult.hasError())
		{
			Logger::warn(SSTR << "Failed to enable advertising: " << advertisingResult.errorMessage());
		}
	}

	// Finally, ensure the adapter is powered on
	auto poweredResult = adapter.setPowered(true);
	if (poweredResult.hasError())
	{
		Logger::error(SSTR << "Failed to power on adapter: " << poweredResult.errorMessage());
		setRetry();
		return;
	}

	Logger::info("The Bluetooth adapter is fully configured using modern BlueZ D-Bus API");

	// We're all set, nothing to do!
	bAdapterConfigured = true;
	initializationStateProcessor();
}

// ---------------------------------------------------------------------------------------------------------------------------------
//     _       _             _
//    / \   __| | __ _ _ __ | |_ ___ _ __
//   / _ \ / _` |/ _` | '_ \| __/ _ \ '__|
//  / ___ \ (_| | (_| | |_) | ||  __/ |
// /_/   \_\__,_|\__,_| .__/ \__\___|_|
//                    |_|
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Find the BlueZ's GATT Manager interface for the *first* Bluetooth adapter provided by BlueZ. We'll need this to register our
// GATT server with BlueZ.
void findAdapterInterface()
{
	// Get a list of the BlueZ's D-Bus objects
	GList *pObjects = g_dbus_object_manager_get_objects(pBluezObjectManager);
	if (nullptr == pObjects)
	{
		Logger::error(SSTR << "Unable to get ObjectManager objects");
		setRetryFailure();
		return;
	}

	// Scan the list of objects we find one with a GATT manager interface
	//
	// Note that if there are multiple interfaces, we will only find the first
	for (guint i = 0; i < g_list_length(pObjects) && bluezGattManagerInterfaceName.empty(); ++i)
	{
		// Current object in question
		pBluezAdapterObject = static_cast<GDBusObject *>(g_list_nth_data(pObjects, i));
		if (nullptr == pBluezAdapterObject) { continue; }

		// See if it has a GATT manager interface
		pBluezGattManagerProxy = reinterpret_cast<GDBusProxy *>(g_dbus_object_get_interface(pBluezAdapterObject, "org.bluez.GattManager1"));
		if (nullptr == pBluezGattManagerProxy) { continue; }

		// Get the interface proxy for this adapter - this will come in handy later
		pBluezAdapterInterfaceProxy = reinterpret_cast<GDBusProxy *>(g_dbus_object_get_interface(pBluezAdapterObject, "org.bluez.Adapter1"));
		if (nullptr == pBluezAdapterInterfaceProxy)
		{
			Logger::warn(SSTR << "Failed to get adapter proxy for interface 'org.bluez.Adapter1'");
			continue;
		}

		// Get the interface proxy for this adapter's properties - this will come in handy later
		pBluezAdapterPropertiesInterfaceProxy = reinterpret_cast<GDBusProxy *>(g_dbus_object_get_interface(pBluezAdapterObject, "org.freedesktop.DBus.Properties"));
		if (nullptr == pBluezAdapterPropertiesInterfaceProxy)
		{
			Logger::warn(SSTR << "Failed to get adapter properties proxy for interface 'org.freedesktop.DBus.Properties'");
			continue;
		}

		// Finally, save off the interface name, we're done!
		bluezGattManagerInterfaceName = g_dbus_proxy_get_object_path(pBluezGattManagerProxy);
		break;
	}

	// Get a fresh copy of our objects so we can release the entire list
	pBluezAdapterObject = g_dbus_object_manager_get_object(pBluezObjectManager, g_dbus_object_get_object_path(pBluezAdapterObject));

	// We'll need access to the device object so we can set properties on it
	pBluezDeviceObject = g_dbus_object_manager_get_object(pBluezObjectManager, g_dbus_object_get_object_path(pBluezAdapterObject));

	// Cleanup the list
	for (guint i = 0; i < g_list_length(pObjects) && bluezGattManagerInterfaceName.empty(); ++i)
	{
		g_object_unref(g_list_nth_data(pObjects, i));
	}

	g_list_free(pObjects);

	// If we didn't find the adapter object, reset things and we'll try again later
	if (nullptr == pBluezAdapterObject || nullptr == pBluezDeviceObject)
	{
		Logger::warn(SSTR << "Unable to find BlueZ objects outside of object list");
		bluezGattManagerInterfaceName.clear();
	}

	// If we never ended up with an interface name, bail now
	if (bluezGattManagerInterfaceName.empty())
	{
		Logger::error(SSTR << "Unable to find the adapter");
		setRetryFailure();
		return;
	}

	// Keep going
	initializationStateProcessor();
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____  _            _____   ___  _     _           _   __  __
// | __ )| |_   _  ___|__  /  / _ \| |__ (_) ___  ___| |_|  \/  | __ _ _ __   __ _  __ _  ___ _ __
// |  _ \| | | | |/ _ \ / /  | | | | '_ \| |/ _ \/ __| __| |\/| |/ _` | '_ \ / _` |/ _` |/ _ \ '__|
// | |_) | | |_| |  __// /_  | |_| | |_) | |  __/ (__| |_| |  | | (_| | | | | (_| | (_| |  __/ |
// |____/|_|\__,_|\___/____|  \___/|_.__// |\___|\___|\__|_|  |_|\__,_|_| |_|\__,_|\__, |\___|_|
//                                     |__/                                        |___/
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Get the D-Bus Object Manager client to the BlueZ ObjectManager object
//
// An ObjectManager allows us to find out what objects (and from those, interfaces, etc.) are available from an owned name. We'll
// use this to interrogate BlueZ's objects to find an adapter we can use, among other things.
void getBluezObjectManager()
{
	g_dbus_object_manager_client_new
	(
		pBusConnection,                             // GDBusConnection
		G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE,    // GDBusObjectManagerClientFlags
		"org.bluez",                                // Owner name (or well-known name)
		"/",                                        // Object path
		nullptr,                                    // GDBusProxyTypeFunc get_proxy_type_func
		nullptr,                                    // gpointer get_proxy_type_user_data
		nullptr,                                    // GDestroyNotify get_proxy_type_destroy_notify
		nullptr,                                    // GCancellable *cancellable

		// GAsyncReadyCallback callback
		[] (GObject * /*pSourceObject*/, GAsyncResult *pAsyncResult, gpointer /*pUserData*/)
		{
			// Store BlueZ's ObjectManager
			GError *pError = nullptr;
			pBluezObjectManager = g_dbus_object_manager_client_new_finish(pAsyncResult, &pError);

			if (nullptr == pBluezObjectManager)
			{
				Logger::error(SSTR << "Failed to get an ObjectManager client: " << (nullptr == pError ? "Unknown" : pError->message));
				setRetryFailure();
				return;
			}

			// Keep going
			initializationStateProcessor();
		},

		nullptr                                     // gpointer user_data
	);
}

// ---------------------------------------------------------------------------------------------------------------------------------
//   ___                          _
//  / _ \__      ___ __   ___  __| |  _ __   __ _ _ __ ___   ___
// | | | \ \ /\ / / '_ \ / _ \/ _` | | '_ \ / _` | '_ ` _ \ / _ )
// | |_| |\ V  V /| | | |  __/ (_| | | | | | (_| | | | | | |  __/
//  \___/  \_/\_/ |_| |_|\___|\__,_| |_| |_|\__,_|_| |_| |_|\___|
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Acquire an "owned name" with D-Bus. This name represents our server as a whole, identifying us on D-Bus and allowing others
// (BlueZ) to communicate back to us. All of the D-Bus objects (which represent our BlueZ services, characteristics, etc.) will all
// reside under this owned name.
//
// Note about error management: We don't yet hwave a timeout callback running for retries; errors are considered fatal
void doOwnedNameAcquire()
{
	// Our name is not presently lost
	bOwnedNameAcquired = false;

	ownedNameId = g_bus_own_name_on_connection
	(
		pBusConnection,                    // GDBusConnection *connection
		serverContext().getOwnedName().c_str(), // const gchar *name
		G_BUS_NAME_OWNER_FLAGS_NONE,       // GBusNameOwnerFlags flags

		// GBusNameAcquiredCallback name_acquired_handler
		[](GDBusConnection *, const gchar *, gpointer)
		{
			// Handy way to get periodic activity
			periodicTimeoutId = attachTimeoutSecondsSource(kPeriodicTimerFrequencySeconds, onPeriodicTimer, pBusConnection);
			if (periodicTimeoutId <= 0)
			{
				Logger::fatal(SSTR << "Failed to add a periodic timer");
				setServerHealth(EFailedInit);
				shutdown();
			}

			// Bus name acquired
			bOwnedNameAcquired = true;

			// Keep going...
			initializationStateProcessor();
		},

		// GBusNameLostCallback name_lost_handler
		[](GDBusConnection *, const gchar *, gpointer)
		{
			// Bus name lost
			bOwnedNameAcquired = false;

			// If we don't have a periodicTimeout (which we use for error recovery) then we're sunk
			if (0 == periodicTimeoutId)
			{
				Logger::fatal(SSTR << "Unable to acquire an owned name ('" << serverContext().getOwnedName() << "') on the bus");
				setServerHealth(EFailedInit);
				shutdown();
			}
			else
			{
				Logger::warn(SSTR << "Owned name ('" << serverContext().getOwnedName() << "') lost");
				setRetryFailure();
				return;
			}

			// Keep going...
			initializationStateProcessor();
		},

		nullptr,                          // gpointer user_data
		nullptr                           // GDestroyNotify user_data_free_func
	);
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____
// | __ ) _   _ ___
// |  _ \| | | / __|
// | |_) | |_| \__ )
// |____/ \__,_|___/
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Acquire a connection to the SYSTEM bus so we can communicate with BlueZ.
//
// Note about error management: We don't yet hwave a timeout callback running for retries; errors are considered fatal
void doBusAcquire()
{
	// Acquire a connection to the SYSTEM bus
	g_bus_get
	(
		G_BUS_TYPE_SYSTEM,      // GBusType bus_type
		nullptr,                // GCancellable *cancellable

		// GAsyncReadyCallback callback
		[] (GObject */*pSourceObject*/, GAsyncResult *pAsyncResult, gpointer /*pUserData*/)
		{
			GError *pError = nullptr;
			pBusConnection = g_bus_get_finish(pAsyncResult, &pError);

			if (nullptr == pBusConnection)
			{
				Logger::fatal(SSTR << "Failed to get bus connection: " << (nullptr == pError ? "Unknown" : pError->message));
				setServerHealth(EFailedInit);
				shutdown();
			}

			// Continue
			initializationStateProcessor();
		},

		nullptr                 // gpointer user_data
	);
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____  _        _                                                                     _
// / ___|| |_ __ _| |_ ___   _ __ ___   __ _ _ __   __ _  __ _  ___ _ __ ___   ___ _ __ | |_
// \___ \| __/ _` | __/ _ \ | '_ ` _ \ / _` | '_ \ / _` |/ _` |/ _ \ '_ ` _ \ / _ \ '_ \| __|
//  ___) | || (_| | ||  __/ | | | | | | (_| | | | | (_| | (_| |  __/ | | | | |  __/ | | | |_
// |____/ \__\__,_|\__\___| |_| |_| |_|\__,_|_| |_|\__,_|\__, |\___|_| |_| |_|\___|_| |_|\__|
//                                                       |___/
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Poor-man's state machine, which effectively ensures everything is initialized in order by verifying actual initialization state
// rather than stepping through a set of numeric states. This way, if something fails in an out-of-order sort of way, we can still
// handle it and recover nicely.
void initializationStateProcessor()
{
	// If we're in our end-of-life or waiting for a retry, don't process states
	if (bzpGetServerRunState() > ERunning || 0 != retryTimeStart)
	{
		return;
	}

	//
	// Get a bus connection
	//
	if (nullptr == pBusConnection)
	{
		Logger::debug(SSTR << "Acquiring bus connection");
		doBusAcquire();
		return;
	}

	//
	// Acquire an owned name on the bus
	//
	if (!bOwnedNameAcquired)
	{
		Logger::debug(SSTR << "Acquiring owned name: '" << serverContext().getOwnedName() << "'");
		doOwnedNameAcquire();
		return;
	}

	//
	// Get BlueZ's ObjectManager
	//
	if (nullptr == pBluezObjectManager)
	{
		Logger::debug(SSTR << "Getting BlueZ ObjectManager");
		getBluezObjectManager();
		return;
	}

	//
	// Find the adapter interface
	//
	if (bluezGattManagerInterfaceName.empty())
	{
		Logger::debug(SSTR << "Finding BlueZ GattManager1 interface");
		findAdapterInterface();
		return;
	}

	//
	// Find the adapter interface
	//
	if (!bAdapterConfigured)
	{
		Logger::debug(SSTR << "Configuring BlueZ adapter '" << bluezGattManagerInterfaceName << "'");
		configureAdapter();
		return;
	}

	//
	// Register our object with D-bus
	//
	if (registeredObjectIds.empty())
	{
		Logger::debug(SSTR << "Registering with D-Bus");
		registerObjects();
		return;
	}

	// Register our appliation with the BlueZ GATT manager
	if (!bApplicationRegistered)
	{
		Logger::debug(SSTR << "Registering application with BlueZ GATT manager");

		doRegisterApplication();
		return;
	}

	// At this point, we should be fully initialized
	//
	// It shouldn't ever happen, but just in case, let's double-check that we're healthy and if not, shutdown immediately
	if (bzpGetServerHealth() != EOk)
	{
		shutdown();
		return;
	}

	// Successful initialization - switch to running state
	setServerRunState(ERunning);
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____                                                                  _
// |  _ \ _   _ _ __     ___  ___ _ ____   _____ _ __    _ __ _   _ _ __ | |
// | |_) | | | | '_ \   / __|/ _ \ '__\ \ / / _ \ '__|  | '__| | | | '_ \| |
// |  _ <| |_| | | | |  \__ \  __/ |   \ V /  __/ | _   | |  | |_| | | | |_|
// |_| \_\\__,_|_| |_|  |___/\___|_|    \_/ \___|_|( )  |_|   \__,_|_| |_(_)
//                                                 |/
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Entry point for the asynchronous server thread
//
// This method should not be called directly, instead, direct your attention over to `bzpStart()`
void runServerThread(Server *serverContextPtr, BluezAdapter *adapterContextPtr)
{
	bManualRunLoopMode = false;
	if (!initializeRunLoop(serverContextPtr, adapterContextPtr, true, true))
	{
		setServerHealth(EFailedInit);
		setServerRunState(EStopped);
		return;
	}

	Logger::trace(SSTR << "Starting GLib main loop");
	g_main_loop_run(pMainLoop.load(std::memory_order_acquire));
	finalizeRunLoop();
}

bool startServerLoopManually(Server *serverContextPtr, BluezAdapter *adapterContextPtr)
{
	bManualRunLoopMode = false;
	if (!initializeRunLoop(serverContextPtr, adapterContextPtr, false, false))
	{
		return false;
	}

	bManualRunLoopMode = true;
	Logger::trace("BzPeri manual run loop initialized; the first runServerLoopIteration() call binds the loop to the pumping thread");
	return true;
}

BZPRunLoopResult runServerLoopIterationEx(int mayBlock)
{
	if (!bManualRunLoopMode)
	{
		Logger::warn("runServerLoopIteration() is only valid after startServerLoopManually()");
		return BZP_RUN_LOOP_NOT_MANUAL_MODE;
	}

	if (pMainContext == nullptr)
	{
		Logger::warn("runServerLoopIteration() called without an active manual run loop");
		return BZP_RUN_LOOP_NOT_ACTIVE;
	}

	if (const BZPRunLoopResult ownerResult = ensureRunLoopOwnerThread("runServerLoopIteration()");
		ownerResult != BZP_RUN_LOOP_OK)
	{
		return ownerResult;
	}

	if (hasActiveRunLoopPollCycle())
	{
		Logger::warn("runServerLoopIteration() cannot run while a manual run-loop poll cycle is active; call dispatch or cancel first");
		return BZP_RUN_LOOP_POLL_CYCLE_ACTIVE;
	}

	if (!activateRunLoopOnCurrentThread())
	{
		setServerHealth(EFailedInit);
		setServerRunState(EStopped);
		finalizeRunLoop();
		return BZP_RUN_LOOP_ACTIVATION_FAILED;
	}

	if (finalizeManualRunLoopIfStopped())
	{
		return BZP_RUN_LOOP_OK;
	}

	const gboolean dispatched = g_main_context_iteration(pMainContext, mayBlock ? TRUE : FALSE);
	if (finalizeManualRunLoopIfStopped())
	{
		return BZP_RUN_LOOP_OK;
	}

	return dispatched ? BZP_RUN_LOOP_OK : BZP_RUN_LOOP_IDLE;
}

BZPRunLoopResult runServerLoopIterationForEx(int timeoutMS)
{
	if (timeoutMS < 0)
	{
		return runServerLoopIterationEx(1);
	}

	if (timeoutMS == 0)
	{
		return runServerLoopIterationEx(0);
	}

	if (!bManualRunLoopMode)
	{
		Logger::warn("runServerLoopIterationFor() is only valid after startServerLoopManually()");
		return BZP_RUN_LOOP_NOT_MANUAL_MODE;
	}

	if (pMainContext == nullptr)
	{
		Logger::warn("runServerLoopIterationFor() called without an active manual run loop");
		return BZP_RUN_LOOP_NOT_ACTIVE;
	}

	if (const BZPRunLoopResult ownerResult = ensureRunLoopOwnerThread("runServerLoopIterationFor()");
		ownerResult != BZP_RUN_LOOP_OK)
	{
		return ownerResult;
	}

	if (hasActiveRunLoopPollCycle())
	{
		Logger::warn("runServerLoopIterationFor() cannot run while a manual run-loop poll cycle is active; call dispatch or cancel first");
		return BZP_RUN_LOOP_POLL_CYCLE_ACTIVE;
	}

	if (!activateRunLoopOnCurrentThread())
	{
		setServerHealth(EFailedInit);
		setServerRunState(EStopped);
		finalizeRunLoop();
		return BZP_RUN_LOOP_ACTIVATION_FAILED;
	}

	if (finalizeManualRunLoopIfStopped())
	{
		return BZP_RUN_LOOP_OK;
	}

	RunLoopTimeoutWake timeoutWake;
	GSource *timeoutSource = g_timeout_source_new(timeoutMS);
	g_source_set_callback(
		timeoutSource,
		[](gpointer data) -> gboolean {
			auto *timeoutWake = static_cast<RunLoopTimeoutWake *>(data);
			timeoutWake->fired = true;
			timeoutWake->sourceId = 0;
			return G_SOURCE_REMOVE;
		},
		&timeoutWake,
		nullptr);
	timeoutWake.sourceId = g_source_attach(timeoutSource, pMainContext);
	g_source_unref(timeoutSource);

	const gboolean dispatched = g_main_context_iteration(pMainContext, TRUE);
	if (timeoutWake.sourceId != 0)
	{
		g_source_remove(timeoutWake.sourceId);
		timeoutWake.sourceId = 0;
	}

	if (finalizeManualRunLoopIfStopped())
	{
		return BZP_RUN_LOOP_OK;
	}

	if (timeoutWake.fired)
	{
		return BZP_RUN_LOOP_IDLE;
	}

	return dispatched ? BZP_RUN_LOOP_OK : BZP_RUN_LOOP_IDLE;
}

BZPRunLoopResult attachServerLoopToCurrentThreadEx()
{
	if (!bManualRunLoopMode)
	{
		Logger::warn("attachServerLoopToCurrentThread() is only valid after startServerLoopManually()");
		return BZP_RUN_LOOP_NOT_MANUAL_MODE;
	}

	if (pMainContext == nullptr)
	{
		Logger::warn("attachServerLoopToCurrentThread() called without an active manual run loop");
		return BZP_RUN_LOOP_NOT_ACTIVE;
	}

	if (const BZPRunLoopResult ownerResult = ensureRunLoopOwnerThread("attachServerLoopToCurrentThread()");
		ownerResult != BZP_RUN_LOOP_OK)
	{
		return ownerResult;
	}

	if (!activateRunLoopOnCurrentThread())
	{
		setServerHealth(EFailedInit);
		setServerRunState(EStopped);
		finalizeRunLoop();
		return BZP_RUN_LOOP_ACTIVATION_FAILED;
	}

	return BZP_RUN_LOOP_OK;
}

BZPRunLoopResult detachServerLoopFromCurrentThreadEx()
{
	return detachRunLoopFromCurrentThread();
}

bool isManualServerLoopMode()
{
	return bManualRunLoopMode && pMainContext != nullptr;
}

bool hasServerLoopOwner()
{
	return mainContextOwnerThread != std::thread::id();
}

bool isCurrentThreadServerLoopOwner()
{
	return mainContextOwnerThread != std::thread::id() && mainContextOwnerThread == std::this_thread::get_id();
}

BZPRunLoopResult prepareServerLoopPollEx(int *timeoutMS, int *requiredFDCount, int *dispatchReady)
{
	if (!bManualRunLoopMode)
	{
		Logger::warn("prepareServerLoopPoll() is only valid after startServerLoopManually()");
		return BZP_RUN_LOOP_NOT_MANUAL_MODE;
	}

	if (pMainContext == nullptr)
	{
		Logger::warn("prepareServerLoopPoll() called without an active manual run loop");
		return BZP_RUN_LOOP_NOT_ACTIVE;
	}

	if (hasActiveRunLoopPollCycle())
	{
		Logger::warn("prepareServerLoopPoll() cannot start a nested manual run-loop poll cycle");
		return BZP_RUN_LOOP_POLL_CYCLE_ACTIVE;
	}

	if (const BZPRunLoopResult ownerResult = ensureRunLoopOwnerThread("prepareServerLoopPoll()");
		ownerResult != BZP_RUN_LOOP_OK)
	{
		return ownerResult;
	}

	if (!activateRunLoopOnCurrentThread())
	{
		setServerHealth(EFailedInit);
		setServerRunState(EStopped);
		finalizeRunLoop();
		return BZP_RUN_LOOP_ACTIVATION_FAILED;
	}

	if (finalizeManualRunLoopIfStopped())
	{
		return BZP_RUN_LOOP_NOT_ACTIVE;
	}

	if (!g_main_context_acquire(pMainContext))
	{
		Logger::warn("prepareServerLoopPoll() could not acquire the manual run-loop context");
		return BZP_RUN_LOOP_ACTIVATION_FAILED;
	}

	resetRunLoopPollCycle();
	runLoopPollCycle.active = true;
	runLoopPollCycle.ownerThread = std::this_thread::get_id();
	runLoopPollCycle.preparedReady = g_main_context_prepare(pMainContext, &runLoopPollCycle.maxPriority) != FALSE;
	runLoopPollCycle.ready = false;
	runLoopPollCycle.timeoutMS = runLoopPollCycle.preparedReady ? 0 : -1;
	runLoopPollCycle.requiredFDCount = 0;

	if (!runLoopPollCycle.preparedReady)
	{
		runLoopPollCycle.requiredFDCount = g_main_context_query(
			pMainContext,
			runLoopPollCycle.maxPriority,
			&runLoopPollCycle.timeoutMS,
			nullptr,
			0);
	}

	if (timeoutMS != nullptr)
	{
		*timeoutMS = runLoopPollCycle.timeoutMS;
	}

	if (requiredFDCount != nullptr)
	{
		*requiredFDCount = runLoopPollCycle.requiredFDCount;
	}

	if (dispatchReady != nullptr)
	{
		*dispatchReady = runLoopPollCycle.preparedReady ? 1 : 0;
	}

	return BZP_RUN_LOOP_OK;
}

BZPRunLoopResult queryServerLoopPollEx(BZPPollFD *pollFDs, int pollFDCount, int *requiredFDCount)
{
	if (pollFDCount < 0)
	{
		Logger::warn("queryServerLoopPoll() requires a non-negative pollFDCount");
		return BZP_RUN_LOOP_INVALID_ARGUMENT;
	}

	if (pollFDCount > 0 && pollFDs == nullptr)
	{
		Logger::warn("queryServerLoopPoll() requires a non-null pollFDs buffer when pollFDCount is positive");
		return BZP_RUN_LOOP_INVALID_ARGUMENT;
	}

	if (const BZPRunLoopResult pollCycleResult = ensureRunLoopPollCycleThread("queryServerLoopPoll()");
		pollCycleResult != BZP_RUN_LOOP_OK)
	{
		return pollCycleResult;
	}

	std::vector<GPollFD> gpollFDs(static_cast<size_t>(pollFDCount));
	int timeoutMS = runLoopPollCycle.timeoutMS;
	const int neededFDCount = g_main_context_query(
		pMainContext,
		runLoopPollCycle.maxPriority,
		&timeoutMS,
		gpollFDs.data(),
		pollFDCount);

	runLoopPollCycle.timeoutMS = timeoutMS;
	runLoopPollCycle.requiredFDCount = neededFDCount;
	if (requiredFDCount != nullptr)
	{
		*requiredFDCount = neededFDCount;
	}

	if (neededFDCount > pollFDCount)
	{
		if (pollFDCount == 0 && pollFDs == nullptr)
		{
			return BZP_RUN_LOOP_OK;
		}

		return BZP_RUN_LOOP_BUFFER_TOO_SMALL;
	}

	if (neededFDCount == 0)
	{
		return BZP_RUN_LOOP_OK;
	}

	for (int index = 0; index < neededFDCount; ++index)
	{
		pollFDs[index].fd = gpollFDs[index].fd;
		pollFDs[index].events = gpollFDs[index].events;
		pollFDs[index].revents = gpollFDs[index].revents;
	}

	return BZP_RUN_LOOP_OK;
}

BZPRunLoopResult checkServerLoopPollEx(const BZPPollFD *pollFDs, int pollFDCount)
{
	if (pollFDCount < 0)
	{
		Logger::warn("checkServerLoopPoll() requires a non-negative pollFDCount");
		return BZP_RUN_LOOP_INVALID_ARGUMENT;
	}

	if (pollFDCount > 0 && pollFDs == nullptr)
	{
		Logger::warn("checkServerLoopPoll() requires a non-null pollFDs buffer when pollFDCount is positive");
		return BZP_RUN_LOOP_INVALID_ARGUMENT;
	}

	if (const BZPRunLoopResult pollCycleResult = ensureRunLoopPollCycleThread("checkServerLoopPoll()");
		pollCycleResult != BZP_RUN_LOOP_OK)
	{
		return pollCycleResult;
	}

	std::vector<GPollFD> gpollFDs(static_cast<size_t>(pollFDCount));
	for (int index = 0; index < pollFDCount; ++index)
	{
		gpollFDs[index].fd = pollFDs[index].fd;
		gpollFDs[index].events = pollFDs[index].events;
		gpollFDs[index].revents = pollFDs[index].revents;
	}

	runLoopPollCycle.ready = g_main_context_check(
		pMainContext,
		runLoopPollCycle.maxPriority,
		pollFDCount > 0 ? gpollFDs.data() : nullptr,
		pollFDCount) != FALSE;

	return runLoopPollCycle.ready ? BZP_RUN_LOOP_OK : BZP_RUN_LOOP_IDLE;
}

BZPRunLoopResult dispatchServerLoopPollEx()
{
	if (const BZPRunLoopResult pollCycleResult = ensureRunLoopPollCycleThread("dispatchServerLoopPoll()");
		pollCycleResult != BZP_RUN_LOOP_OK)
	{
		return pollCycleResult;
	}

	const bool ready = runLoopPollCycle.ready;
	if (ready)
	{
		g_main_context_dispatch(pMainContext);
	}

	releaseRunLoopPollCycle();

	if (finalizeManualRunLoopIfStopped())
	{
		return BZP_RUN_LOOP_OK;
	}

	return ready ? BZP_RUN_LOOP_OK : BZP_RUN_LOOP_IDLE;
}

BZPRunLoopResult cancelServerLoopPollEx()
{
	if (const BZPRunLoopResult pollCycleResult = ensureRunLoopPollCycleThread("cancelServerLoopPoll()");
		pollCycleResult != BZP_RUN_LOOP_OK)
	{
		return pollCycleResult;
	}

	releaseRunLoopPollCycle();
	(void)finalizeManualRunLoopIfStopped();
	return BZP_RUN_LOOP_OK;
}

BZPRunLoopResult invokeOnServerLoopEx(void (*callback)(void *), void *userData)
{
	if (callback == nullptr)
	{
		Logger::warn("invokeOnServerLoop() requires a non-null callback");
		return BZP_RUN_LOOP_INVALID_ARGUMENT;
	}

	if (pMainContext == nullptr)
	{
		Logger::warn("invokeOnServerLoop() called without an active BzPeri run loop");
		return BZP_RUN_LOOP_NOT_ACTIVE;
	}

	RunLoopInvocation *invocation = new (std::nothrow) RunLoopInvocation{callback, userData};
	if (invocation == nullptr)
	{
		Logger::error("Unable to allocate run-loop invocation state");
		return BZP_RUN_LOOP_ALLOCATION_FAILED;
	}

	if (mainContextOwnerThread != std::thread::id() && mainContextOwnerThread != std::this_thread::get_id())
	{
		g_main_context_invoke_full(
			pMainContext,
			G_PRIORITY_DEFAULT,
			[](gpointer data) -> gboolean {
				auto *invocation = static_cast<RunLoopInvocation *>(data);
				invocation->callback(invocation->userData);
				return G_SOURCE_REMOVE;
			},
			invocation,
			[](gpointer data) {
				delete static_cast<RunLoopInvocation *>(data);
			});
		return BZP_RUN_LOOP_OK;
	}

	GSource *source = g_idle_source_new();
	g_source_set_priority(source, G_PRIORITY_DEFAULT);
	g_source_set_callback(
		source,
		[](gpointer data) -> gboolean {
			auto *invocation = static_cast<RunLoopInvocation *>(data);
			invocation->callback(invocation->userData);
			return G_SOURCE_REMOVE;
		},
		invocation,
		[](gpointer data) {
			delete static_cast<RunLoopInvocation *>(data);
		});
	g_source_attach(source, pMainContext);
	g_source_unref(source);

	return BZP_RUN_LOOP_OK;
}

}; // namespace bzp
