// Copyright 2017-2019 Paul Nettle
// Copyright (c) 2025 BzPeri Contributors
//
// This file is part of BzPeri.
//
// Licensed under MIT License (see LICENSE file)

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  INSIDE THIS FILE
// >>
//
// The methods in this file represent the complete external C interface for a BzPeri server
// (compatible with the legacy Gobbledegook API).
//
// >>
// >>>  DISCUSSION
// >>
//
// BzPeri exposes a C-friendly facade so that non-C++ applications (or legacy code originally
// written for Gobbledegook) can embed the server without needing to dive into modern C++ code.
// The C interface keeps the integration surface simple and compact while the implementation is
// handled inside the library.
//
// Service definitions are now provided by modular configurators (see ServiceRegistry). This C API
// orchestrates server startup/shutdown, log routing, and data synchronization with those
// configurators.
//
// The interface is compatible with the C language, allowing non-C++ programs to interface with
// BzPeri. This also simplifies bindings for other languages such as Swift or Python.
//
// The interface below has the following categories:
//
//     Log registration - used to register methods that accept all BzPeri logs
//     Update queue management - used for notifying the server that data has been updated
//     Server state - used to track the server's current running state and health
//     Server control - running and stopping the server
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <string.h>
#include <cstddef>
#include <string>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <deque>
#include <mutex>
#include <exception>

#include "Init.h"
#include <bzp/Logger.h>
#include <bzp/Server.h>
#include "ServiceRegistry.h"

// Macro for safe C API functions that catch C++ exceptions
#define BZP_C_API_GUARD_BEGIN() try {
#define BZP_C_API_GUARD_END_RETURN_INT(default_value) \
	} catch (const std::exception& e) { \
		Logger::error(SSTR << "C API exception: " << e.what()); \
		return default_value; \
	} catch (...) { \
		Logger::error("C API unknown exception"); \
		return default_value; \
	}

#define BZP_C_API_GUARD_END_RETURN_VOID() \
	} catch (const std::exception& e) { \
		Logger::error(SSTR << "C API exception: " << e.what()); \
	} catch (...) { \
		Logger::error("C API unknown exception"); \
	}

namespace bzp
{
	// During initialization, we'll check for complation at this interval
	static const int kMaxAsyncInitCheckIntervalMS = 10;

	// Our server thread
	static std::thread serverThread;

	// The current server state (thread-safe atomic)
	static std::atomic<BZPServerRunState> serverRunState{EUninitialized};

	// The current server health (thread-safe atomic)
	static std::atomic<BZPServerHealth> serverHealth{EOk};

	// Condition variable for state transitions
	static std::condition_variable stateChangedCV;
	static std::mutex stateChangedMutex;

	// We store the old GLib print handler and error print handler so we can restore if
	static GPrintFunc printHandlerGLib;
	static GPrintFunc printerrHandlerGLib;
	static GLogFunc logHandlerGLib;

	// Our update queue
	typedef std::tuple<std::string, std::string> QueueEntry;
	std::deque<QueueEntry> updateQueue;
	std::mutex updateQueueMutex;

	// Internal method to set the run state of the server
	void setServerRunState(BZPServerRunState newState)
	{
		BZPServerRunState oldState = serverRunState.load(std::memory_order_acquire);
		Logger::status(SSTR << "** SERVER RUN STATE CHANGED: " << bzpGetServerRunStateString(oldState) << " -> " << bzpGetServerRunStateString(newState));

		// Store with release ordering and notify
		serverRunState.store(newState, std::memory_order_release);

		// Notify waiting threads about state change
		stateChangedCV.notify_all();
	}

	// Internal method to set the health of the server
	void setServerHealth(BZPServerHealth newHealth)
	{
		BZPServerHealth oldHealth = serverHealth.load(std::memory_order_acquire);
		Logger::status(SSTR << "** SERVER HEALTH CHANGED: " << bzpGetServerHealthString(oldHealth) << " -> " << bzpGetServerHealthString(newHealth));

		// Store with release ordering
		serverHealth.store(newHealth, std::memory_order_release);
	}
}; // namespace bzp

using namespace bzp;

// ---------------------------------------------------------------------------------------------------------------------------------
//  _                                  _     _             _   _
// | |    ___   __ _    _ __ ___  __ _(_)___| |_ _ __ __ _| |_(_) ___  _ ___
// | |   / _ \ / _` |  | '__/ _ \/ _` | / __| __| '__/ _` | __| |/ _ \| '_  |
// | |__| (_) | (_| |  | | |  __/ (_| | \__ \ |_| | | (_| | |_| | (_) | | | |
// |_____\___/ \__, |  |_|  \___|\__, |_|___/\__|_|  \__,_|\__|_|\___/|_| |_|
//             |___/             |___/
//
// ---------------------------------------------------------------------------------------------------------------------------------

void bzpLogRegisterDebug(BZPLogReceiver receiver) {
	BZP_C_API_GUARD_BEGIN()
	Logger::registerDebugReceiver(receiver);
	BZP_C_API_GUARD_END_RETURN_VOID()
}
void bzpLogRegisterInfo(BZPLogReceiver receiver) {
	BZP_C_API_GUARD_BEGIN()
	Logger::registerInfoReceiver(receiver);
	BZP_C_API_GUARD_END_RETURN_VOID()
}
void bzpLogRegisterStatus(BZPLogReceiver receiver) {
	BZP_C_API_GUARD_BEGIN()
	Logger::registerStatusReceiver(receiver);
	BZP_C_API_GUARD_END_RETURN_VOID()
}
void bzpLogRegisterWarn(BZPLogReceiver receiver) {
	BZP_C_API_GUARD_BEGIN()
	Logger::registerWarnReceiver(receiver);
	BZP_C_API_GUARD_END_RETURN_VOID()
}
void bzpLogRegisterError(BZPLogReceiver receiver) {
	BZP_C_API_GUARD_BEGIN()
	Logger::registerErrorReceiver(receiver);
	BZP_C_API_GUARD_END_RETURN_VOID()
}
void bzpLogRegisterFatal(BZPLogReceiver receiver) {
	BZP_C_API_GUARD_BEGIN()
	Logger::registerFatalReceiver(receiver);
	BZP_C_API_GUARD_END_RETURN_VOID()
}
void bzpLogRegisterTrace(BZPLogReceiver receiver) {
	BZP_C_API_GUARD_BEGIN()
	Logger::registerTraceReceiver(receiver);
	BZP_C_API_GUARD_END_RETURN_VOID()
}
void bzpLogRegisterAlways(BZPLogReceiver receiver) {
	BZP_C_API_GUARD_BEGIN()
	Logger::registerAlwaysReceiver(receiver);
	BZP_C_API_GUARD_END_RETURN_VOID()
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  _   _           _       _                                                                                                     _
// | | | |_ __   __| | __ _| |_ ___     __ _ _   _  ___ _   _  ___    _ __ ___   __ _ _ __   __ _  __ _  ___ _ __ ___   ___ _ __ | |_
// | | | | '_ \ / _` |/ _` | __/ _ \   / _` | | | |/ _ \ | | |/ _ \  | '_ ` _ \ / _` | '_ \ / _` |/ _` |/ _ \ '_ ` _ \ / _ \ '_ \| __|
// | |_| | |_) | (_| | (_| | ||  __/  | (_| | |_| |  __/ |_| |  __/  | | | | | | (_| | | | | (_| | (_| |  __/ | | | | |  __/ | | | |_
//  \___/| .__/ \__,_|\__,_|\__\___|   \__, |\__,_|\___|\__,_|\___|  |_| |_| |_|\__,_|_| |_|\__,_|\__, |\___|_| |_| |_|\___|_| |_|\__|
//       |_|                              |_|                                                     |___/
//
// Push/pop update notifications onto a queue. As these methods are where threads collide (i.e., this is how they communicate),
// these methods are thread-safe.
// ---------------------------------------------------------------------------------------------------------------------------------

// Adds an update to the front of the queue for a characteristic at the given object path
//
// Returns non-zero value on success or 0 on failure.
int bzpNofifyUpdatedCharacteristic(const char *pObjectPath)
{
	BZP_C_API_GUARD_BEGIN()
	if (!pObjectPath) return 0;
	return bzpPushUpdateQueue(pObjectPath, "org.bluez.GattCharacteristic1") != 0;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

// Adds an update to the front of the queue for a descriptor at the given object path
//
// Returns non-zero value on success or 0 on failure.
int bzpNofifyUpdatedDescriptor(const char *pObjectPath)
{
	BZP_C_API_GUARD_BEGIN()
	if (!pObjectPath) return 0;
	return bzpPushUpdateQueue(pObjectPath, "org.bluez.GattDescriptor1") != 0;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

// Adds a named update to the front of the queue. Generally, this routine should not be used directly. Instead, use the
// `bzpNofifyUpdatedCharacteristic()` instead.
//
// Returns non-zero value on success or 0 on failure.
int bzpPushUpdateQueue(const char *pObjectPath, const char *pInterfaceName)
{
	BZP_C_API_GUARD_BEGIN()
	if (!pObjectPath || !pInterfaceName) return 0;

	QueueEntry t(pObjectPath, pInterfaceName);

	std::lock_guard<std::mutex> guard(updateQueueMutex);
	updateQueue.push_front(t);
	return 1;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

// Get the next update from the back of the queue and returns the element in `element` as a string in the format:
//
//     "com/object/path|com.interface.name"
//
// If the queue is empty, this method returns `0` and does nothing.
//
// `elementLen` is the size of the `element` buffer in bytes. If the resulting string (including the null terminator) will not
// fit within `elementLen` bytes, the method returns `-1` and does nothing.
//
// If `keep` is set to non-zero, the entry is not removed and will be retrieved again on the next call. Otherwise, the element
// is removed.
//
// Returns 1 on success, 0 if the queue is empty, -1 on error (such as the length too small to store the element)
int bzpPopUpdateQueue(char *pElementBuffer, int elementLen, int keep)
{
	BZP_C_API_GUARD_BEGIN()
	if (!pElementBuffer || elementLen <= 0) return -1;

	std::string result;

	{
		std::lock_guard<std::mutex> guard(updateQueueMutex);

		// Check for an empty queue
		if (updateQueue.empty()) { return 0; }

		// Get the last element
		QueueEntry t = updateQueue.back();

		// Get the result string
		result = std::get<0>(t) + "|" + std::get<1>(t);

		// Ensure there's enough room for it
		if (result.length() + 1 > static_cast<size_t>(elementLen)) { return -1; }

		if (keep == 0)
		{
			updateQueue.pop_back();
		}
	}

	// Copy the element string safely
	strncpy(pElementBuffer, result.c_str(), elementLen - 1);
	pElementBuffer[elementLen - 1] = '\0';

	return 1;
	BZP_C_API_GUARD_END_RETURN_INT(-1)
}

// Returns 1 if the queue is empty, otherwise 0
int bzpUpdateQueueIsEmpty()
{
	BZP_C_API_GUARD_BEGIN()
	std::lock_guard<std::mutex> guard(updateQueueMutex);
	return updateQueue.empty() ? 1 : 0;
	BZP_C_API_GUARD_END_RETURN_INT(1)
}

// Returns the number of entries waiting in the queue
int bzpUpdateQueueSize()
{
	BZP_C_API_GUARD_BEGIN()
	std::lock_guard<std::mutex> guard(updateQueueMutex);
	return static_cast<int>(updateQueue.size());
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

// Removes all entries from the queue
void bzpUpdateQueueClear()
{
	BZP_C_API_GUARD_BEGIN()
	std::lock_guard<std::mutex> guard(updateQueueMutex);
	updateQueue.clear();
	BZP_C_API_GUARD_END_RETURN_VOID()
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____                     _        _
// |  _ \ _   _ _ __     ___| |_ __ _| |_ ___
// | |_) | | | | '_ \   / __| __/ _` | __/ _ )
// |  _ <| |_| | | | |  \__ \ || (_| | ||  __/
// |_| \_\\__,_|_| |_|  |___/\__\__,_|\__\___|
//
// Methods for maintaining and reporting the state of the server
// ---------------------------------------------------------------------------------------------------------------------------------

// Retrieve the current running state of the server
//
// See `BZPServerRunState` (enumeration) for more information.
BZPServerRunState bzpGetServerRunState()
{
	return serverRunState;
}

// Convert a `BZPServerRunState` into a human-readable string
const char *bzpGetServerRunStateString(BZPServerRunState state)
{
	switch(state)
	{
		case EUninitialized: return "Uninitialized";
		case EInitializing: return "Initializing";
		case ERunning: return "Running";
		case EStopping: return "Stopping";
		case EStopped: return "Stopped";
		default: return "Unknown";
	}
}

// Convenience method to check ServerRunState for a running server
int bzpIsServerRunning()
{
	return serverRunState <= ERunning ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____                              _                _ _   _
// / ___|  ___ _ ____   _____ _ __   | |__   ___  __ _| | |_| |___
// \___ \ / _ \ '__\ \ / / _ \ '__|  | '_ \ / _ \/ _` | | __| '_  |
//  ___) |  __/ |   \ V /  __/ |     | | | |  __/ (_| | | |_| | | |
// |____/ \___|_|    \_/ \___|_|     |_| |_|\___|\__,_|_|\__|_| |_|
//
// Methods for maintaining and reporting the health of the server
// ---------------------------------------------------------------------------------------------------------------------------------

// Retrieve the current health of the server
//
// See `BZPServerHealth` (enumeration) for more information.
BZPServerHealth bzpGetServerHealth()
{
	return serverHealth;
}

// Convert a `BZPServerHealth` into a human-readable string
const char *bzpGetServerHealthString(BZPServerHealth state)
{
	switch(state)
	{
		case EOk: return "Ok";
		case EFailedInit: return "Failed initialization";
		case EFailedRun: return "Failed run";
		default: return "Unknown";
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____  _                 _   _
// / ___|| |_ ___  _ __    | |_| |__   ___    ___  ___ _ ____   _____ _ __
// \___ \| __/ _ \| '_ \   | __| '_ \ / _ \  / __|/ _ \ '__\ \ / / _ \ '__|
//  ___) | || (_) | |_) |  | |_| | | |  __/  \__ \  __/ |   \ V /  __/ |
// |____/ \__\___/| .__/    \__|_| |_|\___|  |___/\___|_|    \_/ \___|_|
//                |_|
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Tells the server to begin the shutdown process
//
// The shutdown process will interrupt any currently running asynchronous operation and prevent new operations from starting.
// Once the server has stabilized, its event processing loop is terminated and the server is cleaned up.
//
// `bzpGetServerRunState` will return EStopped when shutdown is complete. To block until the shutdown is complete, see
// `bzpWait()`.
//
// Alternatively, you can use `bzpShutdownAndWait()` to request the shutdown and block until the shutdown is complete.
void bzpTriggerShutdown()
{
	shutdown();
}

// Convenience method to trigger a shutdown (via `bzpTriggerShutdown()`) and also waits for shutdown to complete (via
// `bzpWait()`)
int bzpShutdownAndWait()
{
	if (bzpIsServerRunning() != 0)
	{
		// Tell the server to shut down
		bzpTriggerShutdown();
	}

	// Block until it has shut down completely
	return bzpWait();
}

// ---------------------------------------------------------------------------------------------------------------------------------
// __        __    _ _
// \ \      / /_ _(_) |_     ___  _ __     ___  ___ _ ____   _____ _ __
//  \ \ /\ / / _` | | __|   / _ \| '_ \   / __|/ _ \ '__\ \ / / _ \ '__|
//   \ V  V / (_| | | |_   | (_) | | | |  \__ \  __/ |   \ V /  __/ |
//    \_/\_/ \__,_|_|\__|   \___/|_| |_|  |___/\___|_|    \_/ \___|_|
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Blocks for up to maxAsyncInitTimeoutMS milliseconds until the server shuts down.
//
// If shutdown is successful, this method will return a non-zero value. Otherwise, it will return 0.
//
// If the server fails to stop for some reason, the thread will be killed.
//
// Typically, a call to this method would follow `bzpTriggerShutdown()`.
int bzpWait()
{
	int result = 0;
	try
	{
		if (bzpGetServerRunState() <= ERunning)
		{
			Logger::info("Waiting for BzPeri server to stop");
		}

		if (serverThread.joinable())
		{
			serverThread.join();
		}

		result = 1;
	}
	catch(std::system_error &ex)
	{
		switch (ex.code().value())
		{
			case static_cast<int>(std::errc::invalid_argument):
				Logger::warn(SSTR << "Server thread was not joinable during bzpWait(): " << ex.what());
				break;
			case static_cast<int>(std::errc::no_such_process):
				Logger::warn(SSTR << "Server thread was not valid during bzpWait(): " << ex.what());
				break;
			case static_cast<int>(std::errc::resource_deadlock_would_occur):
				Logger::warn(SSTR << "Deadlock avoided in call to bzpWait() (did the server thread try to stop itself?): " << ex.what());
				break;
			default:
				Logger::warn(SSTR << "Unknown system_error code (" << ex.code() << ") during bzpWait(): " << ex.what());
				break;
		}
	}

	// Restore the GLib output functions
	g_set_print_handler(printHandlerGLib);
	g_set_printerr_handler(printerrHandlerGLib);
	g_log_set_default_handler(logHandlerGLib, nullptr);

	return result;
}

// ---------------------------------------------------------------------------------------------------------------------------------
//  ____  _             _      _   _
// / ___|| |_ __ _ _ __| |_   | |_| |__   ___    ___  ___ _ ____   _____ _ __
// \___ \| __/ _` | '__| __|  | __| '_ \ / _ \  / __|/ _ \ '__\ \ / / _ \ '__|
//  ___) | || (_| | |  | |_   | |_| | | |  __/  \__ \  __/ |   \ V /  __/ |
// |____/ \__\__,_|_|   \__|   \__|_| |_|\___|  |___/\___|_|    \_/ \___|_|
//
// ---------------------------------------------------------------------------------------------------------------------------------

// Set the server state to 'EInitializing' and then immediately create a server thread and initiate the server's async
// processing on the server thread.
//
// At that point the current thread will block for maxAsyncInitTimeoutMS milliseconds or until initialization completes.
//
// If initialization was successful, the method will return a non-zero value with the server running on its own thread in
// 'runServerThread'.
//
// If initialization was unsuccessful, this method will continue to block until the server has stopped. This method will then
// return 0.
//
// IMPORTANT:
//
// The data setter uses void* types to allow receipt of unknown data types from the server. Ensure that you do not store these
// pointers. Copy the data before returning from your getter delegate.
//
// Similarly, the pointer to data returned to the data getter should point to non-volatile memory so that the server can use it
// safely for an indefinite period of time.
//
// pServiceName: The name of our server (collectino of services)
//
//     !!!IMPORTANT!!!
//
//     This name must match tha name configured in the D-Bus permissions. See the Readme.md file for more information.
//
//     This is used to build the path for our Bluetooth services. It also provides the base for the D-Bus owned name (see
//     getOwnedName.)
//
//     This value will be stored as lower-case only.
//
//     Retrieve this value using the `getName()` method
//
// pAdvertisingName: The name for this controller, as advertised over LE
//
//     IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
//     BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
//     name.
//
//     Retrieve this value using the `getAdvertisingName()` method
//
// pAdvertisingShortName: The short name for this controller, as advertised over LE
//
//     According to the spec, the short name is used in case the full name doesn't fit within Extended Inquiry Response (EIR) or
//     Advertising Data (AD).
//
//     IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
//     BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
//     name.
//
//     Retrieve this value using the `getAdvertisingShortName()` method
//
int bzpStartWithBondable(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS, int enableBondable)
{
	try
	{
		// Input validation
		if (!pServiceName || strlen(pServiceName) == 0)
		{
			Logger::error("bzpStart: pServiceName cannot be null or empty");
			return 0;
		}
		if (!pAdvertisingName)
		{
			Logger::error("bzpStart: pAdvertisingName cannot be null");
			return 0;
		}
		if (!pAdvertisingShortName)
		{
			Logger::error("bzpStart: pAdvertisingShortName cannot be null");
			return 0;
		}
		if (!getter)
		{
			Logger::error("bzpStart: getter delegate cannot be null");
			return 0;
		}
		if (!setter)
		{
			Logger::error("bzpStart: setter delegate cannot be null");
			return 0;
		}
		if (maxAsyncInitTimeoutMS < 100 || maxAsyncInitTimeoutMS > 60000)
		{
			Logger::error(SSTR << "bzpStart: maxAsyncInitTimeoutMS (" << maxAsyncInitTimeoutMS << ") must be between 100 and 60000 milliseconds");
			return 0;
		}

		// Validate service name length (reasonable limits)
		if (strlen(pServiceName) > 255)
		{
			Logger::error(SSTR << "bzpStart: pServiceName too long (" << strlen(pServiceName) << " > 255)");
			return 0;
		}

		//
		// Start by capturing the GLib output
		//

		// Redirect GLib output to this log method
		printHandlerGLib = g_set_print_handler([](const gchar *string)
		{
			Logger::info(string);
		});
		printerrHandlerGLib = g_set_printerr_handler([](const gchar *string)
		{
			Logger::error(string);
		});
		logHandlerGLib = g_log_set_default_handler([](const gchar *log_domain, GLogLevelFlags log_levels, const gchar *message, gpointer /*user_data*/)
		{
			std::string str = std::string(log_domain) + ": " + message;
			if ((log_levels & (G_LOG_FLAG_RECURSION|G_LOG_FLAG_FATAL)) != 0)
			{
				Logger::fatal(str);
			}
			else if ((log_levels & (G_LOG_LEVEL_CRITICAL|G_LOG_LEVEL_ERROR)) != 0)
			{
				Logger::error(str);
			}
			else if ((log_levels & G_LOG_LEVEL_WARNING) != 0)
			{
				Logger::warn(str);
			}
			else if ((log_levels & G_LOG_LEVEL_DEBUG) != 0)
			{
				Logger::debug(str);
			}
			else
			{
				Logger::info(str);
			}
		}, nullptr);

		Logger::info(SSTR << "Starting BzPeri server '" << pAdvertisingName << "'");

		// Allocate our server
		TheServer = std::make_shared<Server>(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, enableBondable != 0);

		const std::size_t configuratorCount = serviceConfiguratorCount();
		if (configuratorCount == 0)
		{
			Logger::info("No service configurators registered; starting with an empty GATT database");
		}
		else
		{
			applyRegisteredServiceConfigurators(*TheServer);
			Logger::trace(SSTR << "Applied " << configuratorCount << " service configurator(s)");
		}

		// Start our server thread
		try
		{
			serverThread = std::thread(runServerThread);
		}
		catch(std::system_error &ex)
		{
			Logger::error(SSTR << "Server thread was unable to start (code " << ex.code() << ") during bzpStart(): " << ex.what());

			setServerRunState(EStopped);
			return 0;
		}

		// Wait for the server to complete initialization using condition variable
		// Create local lock to avoid lock lifetime issues
		std::unique_lock<std::mutex> lock(stateChangedMutex);
		bool initCompleted = stateChangedCV.wait_for(lock, std::chrono::milliseconds(maxAsyncInitTimeoutMS),
			[]() { return serverRunState.load(std::memory_order_acquire) > EInitializing; });
		lock.unlock();

		// If something went wrong, shut down
		if (!initCompleted)
		{
			Logger::error("BzPeri server initialization timed out");

			setServerHealth(EFailedInit);

			shutdown();
		}

		// If something went wrong, shut down if we've not already done so
		if (bzpGetServerRunState() != ERunning)
		{
			if (!bzpWait())
			{
				Logger::warn(SSTR << "Unable to stop the server after an error in bzpStart()");
			}

			return 0;
		}

		// Everything looks good
		Logger::trace("BzPeri server has started");
		return 1;
	}
	catch(...)
	{
		Logger::error(SSTR << "Unknown exception during bzpStartWithBondable()");
		return 0;
	}
}

// Backward compatibility wrapper - calls bzpStartWithBondable with enableBondable=1 (true)
int bzpStart(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS)
{
	return bzpStartWithBondable(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, maxAsyncInitTimeoutMS, 1);
}
