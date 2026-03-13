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
#include <algorithm>
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

#include "config.h"
#include "BluezAdapterCompat.h"
#include "ServerCompat.h"
#include "Init.h"
#include <bzp/BluezAdapter.h>
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

#define BZP_C_API_GUARD_END_RETURN(default_value) \
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
	namespace {
		constexpr int kConfiguredGLibLogCaptureMode = BZP_DEFAULT_GLIB_LOG_CAPTURE_MODE_VALUE;

		RuntimeBluezAdapterPtr& runtimeBluezAdapterStorage()
		{
			static RuntimeBluezAdapterPtr runtimeAdapter(nullptr, destroyBluezAdapterForRuntime);
			return runtimeAdapter;
		}

		BluezAdapter* ensureRuntimeBluezAdapter()
		{
			auto &runtimeAdapter = runtimeBluezAdapterStorage();
			if (!runtimeAdapter)
			{
				runtimeAdapter = makeRuntimeBluezAdapterPtr();
			}

			setActiveBluezAdapterForRuntime(runtimeAdapter.get());
			return runtimeAdapter.get();
		}

		void releaseRuntimeBluezAdapter() noexcept
		{
			setActiveBluezAdapterForRuntime(nullptr);
			runtimeBluezAdapterStorage().reset();
		}

		void releaseRuntimeServer() noexcept
		{
			setActiveServerForRuntime(nullptr);
		}
	}

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
	static std::atomic<int> glibLogCaptureMode{kConfiguredGLibLogCaptureMode};
	static std::atomic<unsigned int> automaticGLibCaptureInstalls{0};

	// RAII guard that saves and restores GLib global handlers
	struct GLibHandlerGuard {
		GPrintFunc savedPrint = nullptr;
		GPrintFunc savedPrinterr = nullptr;
		GLogFunc savedLog = nullptr;
		bool active = false;
		unsigned int installDepth = 0;
		std::mutex mutex;

		bool install() {
			std::lock_guard<std::mutex> lock(mutex);
			if (installDepth++ > 0)
			{
				return true;
			}
			savedPrint = g_set_print_handler([](const gchar* s) { Logger::info(s); });
			savedPrinterr = g_set_printerr_handler([](const gchar* s) { Logger::error(s); });
			savedLog = g_log_set_default_handler(
				[](const gchar* d, GLogLevelFlags f, const gchar* m, gpointer) {
					std::string str = std::string(d ? d : "") + ": " + (m ? m : "");
					if ((f & (G_LOG_FLAG_RECURSION|G_LOG_FLAG_FATAL)) != 0) Logger::fatal(str);
					else if ((f & (G_LOG_LEVEL_CRITICAL|G_LOG_LEVEL_ERROR)) != 0) Logger::error(str);
					else if ((f & G_LOG_LEVEL_WARNING) != 0) Logger::warn(str);
					else if ((f & G_LOG_LEVEL_DEBUG) != 0) Logger::debug(str);
					else Logger::info(str);
				}, nullptr);
			active = true;
			return true;
		}

		bool restore() {
			std::lock_guard<std::mutex> lock(mutex);
			if (installDepth == 0)
			{
				return false;
			}
			if (--installDepth > 0)
			{
				return true;
			}
			if (active) {
				g_set_print_handler(savedPrint);
				g_set_printerr_handler(savedPrinterr);
				g_log_set_default_handler(savedLog, nullptr);
				active = false;
				savedPrint = nullptr;
				savedPrinterr = nullptr;
				savedLog = nullptr;
			}
			return true;
		}

		bool isInstalled()
		{
			std::lock_guard<std::mutex> lock(mutex);
			return active;
		}

		~GLibHandlerGuard() { (void)restore(); }
	};

	// GLib handler guard (replaces the three separate static handler variables)
	static GLibHandlerGuard glibHandlerGuard;

	bool shouldAutoCaptureGLibLogs()
	{
		const int mode = glibLogCaptureMode.load(std::memory_order_acquire);
		return mode == BZP_GLIB_LOG_CAPTURE_AUTOMATIC || mode == BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN;
	}

	bool shouldUseHostManagedGLibCapture()
	{
		return glibLogCaptureMode.load(std::memory_order_acquire) == BZP_GLIB_LOG_CAPTURE_HOST_MANAGED;
	}

	bool shouldReleaseAutomaticGLibLogsAtRunning()
	{
		return glibLogCaptureMode.load(std::memory_order_acquire) == BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN;
	}

	bool installAutomaticGLibHandlers()
	{
		if (!shouldAutoCaptureGLibLogs())
		{
			return false;
		}

		if (!glibHandlerGuard.install())
		{
			return false;
		}

		automaticGLibCaptureInstalls.fetch_add(1, std::memory_order_acq_rel);
		return true;
	}

	void restoreAutomaticGLibHandlers()
	{
		unsigned int expected = automaticGLibCaptureInstalls.load(std::memory_order_acquire);
		while (expected > 0)
		{
			if (automaticGLibCaptureInstalls.compare_exchange_weak(expected, expected - 1, std::memory_order_acq_rel))
			{
				(void)glibHandlerGuard.restore();
				return;
			}
		}
	}

	struct ScopedAction
	{
		std::function<void()> action;
		bool active = true;

		~ScopedAction()
		{
			if (active && action)
			{
				action();
			}
		}

		void release() noexcept
		{
			active = false;
		}
	};

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

		if (newState == ERunning && oldState != ERunning && shouldReleaseAutomaticGLibLogsAtRunning())
		{
			restoreAutomaticGLibHandlers();
		}
	}

	// Internal method to set the health of the server
	void setServerHealth(BZPServerHealth newHealth)
	{
		BZPServerHealth oldHealth = serverHealth.load(std::memory_order_acquire);
		Logger::status(SSTR << "** SERVER HEALTH CHANGED: " << bzpGetServerHealthString(oldHealth) << " -> " << bzpGetServerHealthString(newHealth));

		// Store with release ordering
		serverHealth.store(newHealth, std::memory_order_release);
	}

	void restoreGLibHandlers()
	{
		restoreAutomaticGLibHandlers();
	}

	bool ensureAutomaticGLibCaptureForShutdown()
	{
		if (glibLogCaptureMode.load(std::memory_order_acquire) != BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN)
		{
			return false;
		}

		if (glibHandlerGuard.isInstalled())
		{
			return true;
		}

		return installAutomaticGLibHandlers();
	}

	bool isValidRunState(BZPServerRunState state) noexcept
	{
		switch (state)
		{
			case EUninitialized:
			case EInitializing:
			case ERunning:
			case EStopping:
			case EStopped:
				return true;
			default:
				return false;
		}
	}

	bool isServerThreadCurrent() noexcept
	{
		return serverThread.joinable() && serverThread.get_id() == std::this_thread::get_id();
	}

	bool waitForRunState(BZPServerRunState targetState, int timeoutMS)
	{
		if (serverRunState.load(std::memory_order_acquire) == targetState)
		{
			return true;
		}

		std::unique_lock<std::mutex> lock(stateChangedMutex);
		auto reachedTarget = [targetState]() {
			return serverRunState.load(std::memory_order_acquire) == targetState;
		};

		if (timeoutMS < 0)
		{
			stateChangedCV.wait(lock, reachedTarget);
			return true;
		}

		return stateChangedCV.wait_for(lock, std::chrono::milliseconds(timeoutMS), reachedTarget);
	}

	int joinServerThread(const char *context)
	{
		try
		{
			if (serverThread.joinable())
			{
				serverThread.join();
			}
			return 1;
		}
		catch (std::system_error &ex)
		{
			switch (ex.code().value())
			{
				case static_cast<int>(std::errc::invalid_argument):
					Logger::warn(SSTR << "Server thread was not joinable during " << context << "(): " << ex.what());
					break;
				case static_cast<int>(std::errc::no_such_process):
					Logger::warn(SSTR << "Server thread was not valid during " << context << "(): " << ex.what());
					break;
				case static_cast<int>(std::errc::resource_deadlock_would_occur):
					Logger::warn(SSTR << "Deadlock avoided in call to " << context << "() (did the server thread try to stop itself?): " << ex.what());
					break;
				default:
					Logger::warn(SSTR << "Unknown system_error code (" << ex.code() << ") during " << context << "(): " << ex.what());
					break;
			}
		}

		return 0;
	}
}; // namespace bzp

using namespace bzp;

namespace {

BZPUpdateEnqueueResult enqueueUpdate(const char *pObjectPath, const char *pInterfaceName, bool requireRunning)
{
	if (!pObjectPath || !pInterfaceName)
	{
		return BZP_UPDATE_ENQUEUE_INVALID_ARGUMENT;
	}

	if (requireRunning)
	{
		const BZPServerRunState state = bzpGetServerRunState();
		if (state == EUninitialized || state > ERunning)
		{
			return BZP_UPDATE_ENQUEUE_NOT_RUNNING;
		}
	}

	static constexpr size_t kMaxUpdateQueueSize = 1024;
	QueueEntry entry(pObjectPath, pInterfaceName);

	std::lock_guard<std::mutex> guard(updateQueueMutex);
	if (updateQueue.size() >= kMaxUpdateQueueSize)
	{
		Logger::warn("Update queue full — dropping oldest entry");
		updateQueue.pop_back();
	}
	updateQueue.push_front(std::move(entry));
	return BZP_UPDATE_ENQUEUE_OK;
}

} // namespace

void bzpSetGLibLogCaptureEnabled(int enabled)
{
	glibLogCaptureMode.store(enabled != 0 ? BZP_GLIB_LOG_CAPTURE_AUTOMATIC : BZP_GLIB_LOG_CAPTURE_DISABLED, std::memory_order_release);
	if (enabled == 0)
	{
		restoreAutomaticGLibHandlers();
	}
}

int bzpGetGLibLogCaptureEnabled()
{
	const int mode = glibLogCaptureMode.load(std::memory_order_acquire);
	return (mode == BZP_GLIB_LOG_CAPTURE_AUTOMATIC || mode == BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN) ? 1 : 0;
}

void bzpSetGLibLogCaptureMode(BZPGLibLogCaptureMode mode)
{
	if (mode != BZP_GLIB_LOG_CAPTURE_AUTOMATIC
		&& mode != BZP_GLIB_LOG_CAPTURE_DISABLED
		&& mode != BZP_GLIB_LOG_CAPTURE_HOST_MANAGED
		&& mode != BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN)
	{
		Logger::warn(SSTR << "Ignoring invalid GLib log capture mode (" << static_cast<int>(mode) << ")");
		return;
	}

	glibLogCaptureMode.store(mode, std::memory_order_release);
	if (mode == BZP_GLIB_LOG_CAPTURE_DISABLED
		|| mode == BZP_GLIB_LOG_CAPTURE_HOST_MANAGED
		|| (mode == BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN && bzpGetServerRunState() == ERunning))
	{
		restoreAutomaticGLibHandlers();
	}
}

BZPGLibLogCaptureMode bzpGetGLibLogCaptureMode()
{
	return static_cast<BZPGLibLogCaptureMode>(glibLogCaptureMode.load(std::memory_order_acquire));
}

BZPGLibLogCaptureMode bzpGetConfiguredGLibLogCaptureMode()
{
	return static_cast<BZPGLibLogCaptureMode>(kConfiguredGLibLogCaptureMode);
}

BZPGLibLogCaptureResult bzpInstallGLibLogCaptureEx()
{
	if (!shouldUseHostManagedGLibCapture())
	{
		Logger::warn("bzpInstallGLibLogCapture() requires BZP_GLIB_LOG_CAPTURE_HOST_MANAGED mode");
		return BZP_GLIB_LOG_CAPTURE_RESULT_WRONG_MODE;
	}

	return glibHandlerGuard.install()
		? BZP_GLIB_LOG_CAPTURE_RESULT_OK
		: BZP_GLIB_LOG_CAPTURE_RESULT_FAILED;
}

int bzpInstallGLibLogCapture()
{
	return bzpInstallGLibLogCaptureEx() == BZP_GLIB_LOG_CAPTURE_RESULT_OK ? 1 : 0;
}

BZPGLibLogCaptureResult bzpRestoreGLibLogCaptureEx()
{
	if (!shouldUseHostManagedGLibCapture())
	{
		Logger::warn("bzpRestoreGLibLogCapture() requires BZP_GLIB_LOG_CAPTURE_HOST_MANAGED mode");
		return BZP_GLIB_LOG_CAPTURE_RESULT_WRONG_MODE;
	}

	return glibHandlerGuard.restore()
		? BZP_GLIB_LOG_CAPTURE_RESULT_OK
		: BZP_GLIB_LOG_CAPTURE_RESULT_NOT_INSTALLED;
}

int bzpRestoreGLibLogCapture()
{
	return bzpRestoreGLibLogCaptureEx() == BZP_GLIB_LOG_CAPTURE_RESULT_OK ? 1 : 0;
}

int bzpIsGLibLogCaptureInstalled()
{
	return glibHandlerGuard.isInstalled() ? 1 : 0;
}

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
	return enqueueUpdate(pObjectPath, "org.bluez.GattCharacteristic1", false) == BZP_UPDATE_ENQUEUE_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

// Adds an update to the front of the queue for a descriptor at the given object path
//
// Returns non-zero value on success or 0 on failure.
int bzpNofifyUpdatedDescriptor(const char *pObjectPath)
{
	BZP_C_API_GUARD_BEGIN()
	if (!pObjectPath) return 0;
	return enqueueUpdate(pObjectPath, "org.bluez.GattDescriptor1", false) == BZP_UPDATE_ENQUEUE_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

// Correctly-spelled versions of the notify functions
int bzpNotifyUpdatedCharacteristic(const char *pObjectPath)
{
	BZP_C_API_GUARD_BEGIN()
	if (!pObjectPath) return 0;
	return enqueueUpdate(pObjectPath, "org.bluez.GattCharacteristic1", false) == BZP_UPDATE_ENQUEUE_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

int bzpNotifyUpdatedDescriptor(const char *pObjectPath)
{
	BZP_C_API_GUARD_BEGIN()
	if (!pObjectPath) return 0;
	return enqueueUpdate(pObjectPath, "org.bluez.GattDescriptor1", false) == BZP_UPDATE_ENQUEUE_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPUpdateEnqueueResult bzpNotifyUpdatedCharacteristicEx(const char *pObjectPath)
{
	BZP_C_API_GUARD_BEGIN()
	return enqueueUpdate(pObjectPath, "org.bluez.GattCharacteristic1", true);
	BZP_C_API_GUARD_END_RETURN_INT(BZP_UPDATE_ENQUEUE_INVALID_ARGUMENT)
}

BZPUpdateEnqueueResult bzpNotifyUpdatedDescriptorEx(const char *pObjectPath)
{
	BZP_C_API_GUARD_BEGIN()
	return enqueueUpdate(pObjectPath, "org.bluez.GattDescriptor1", true);
	BZP_C_API_GUARD_END_RETURN_INT(BZP_UPDATE_ENQUEUE_INVALID_ARGUMENT)
}

// Adds a named update to the front of the queue. Generally, this routine should not be used directly. Instead, use the
// `bzpNofifyUpdatedCharacteristic()` instead.
//
// Returns non-zero value on success or 0 on failure.
int bzpPushUpdateQueue(const char *pObjectPath, const char *pInterfaceName)
{
	BZP_C_API_GUARD_BEGIN()
	return enqueueUpdate(pObjectPath, pInterfaceName, false) == BZP_UPDATE_ENQUEUE_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPUpdateEnqueueResult bzpPushUpdateQueueEx(const char *pObjectPath, const char *pInterfaceName)
{
	BZP_C_API_GUARD_BEGIN()
	return enqueueUpdate(pObjectPath, pInterfaceName, true);
	BZP_C_API_GUARD_END_RETURN_INT(BZP_UPDATE_ENQUEUE_INVALID_ARGUMENT)
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
	auto result = bzpPopUpdateQueueEx(pElementBuffer, elementLen, keep);
	switch (result)
	{
		case BZP_UPDATE_QUEUE_OK:
			return 1;
		case BZP_UPDATE_QUEUE_EMPTY:
			return 0;
		case BZP_UPDATE_QUEUE_BUFFER_TOO_SMALL:
		case BZP_UPDATE_QUEUE_INVALID_ARGUMENT:
		default:
			return -1;
	}
	BZP_C_API_GUARD_END_RETURN_INT(-1)
}

enum BZPUpdateQueueResult bzpPopUpdateQueueEx(char *pElementBuffer, int elementLen, int keep)
{
	BZP_C_API_GUARD_BEGIN()
	if (!pElementBuffer || elementLen <= 0) return BZP_UPDATE_QUEUE_INVALID_ARGUMENT;

	std::string result;

	{
		std::lock_guard<std::mutex> guard(updateQueueMutex);

		// Check for an empty queue
		if (updateQueue.empty()) { return BZP_UPDATE_QUEUE_EMPTY; }

		// Get the last element
		QueueEntry t = updateQueue.back();

		// Get the result string
		result = std::get<0>(t) + "|" + std::get<1>(t);

		// Ensure there's enough room for it
		if (result.length() + 1 > static_cast<size_t>(elementLen)) { return BZP_UPDATE_QUEUE_BUFFER_TOO_SMALL; }

		if (keep == 0)
		{
			updateQueue.pop_back();
		}
	}

	// Copy the element string safely
	strncpy(pElementBuffer, result.c_str(), elementLen - 1);
	pElementBuffer[elementLen - 1] = '\0';

	return BZP_UPDATE_QUEUE_OK;
	BZP_C_API_GUARD_END_RETURN_INT(BZP_UPDATE_QUEUE_INVALID_ARGUMENT)
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
	(void)bzpTriggerShutdownEx();
}

BZPShutdownTriggerResult bzpTriggerShutdownEx()
{
	BZP_C_API_GUARD_BEGIN()
	return shutdownEx();
	BZP_C_API_GUARD_END_RETURN(BZP_SHUTDOWN_TRIGGER_FAILED)
}

// Convenience method to trigger a shutdown (via `bzpTriggerShutdown()`) and also waits for shutdown to complete (via
// `bzpWait()`)
int bzpShutdownAndWait()
{
	return bzpShutdownAndWaitEx() == BZP_WAIT_OK ? 1 : 0;
}

BZPWaitResult bzpShutdownAndWaitEx()
{
	if (bzpIsServerRunning() != 0)
	{
		// Tell the server to shut down
		bzpTriggerShutdown();
	}

	// Block until it has shut down completely
	return bzpWaitForShutdownEx(-1);
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
	if (bzpGetServerRunState() <= ERunning)
	{
		Logger::info("Waiting for BzPeri server to stop");
	}

	return bzpWaitForShutdownEx(-1) == BZP_WAIT_OK ? 1 : 0;
}

BZPWaitResult bzpWaitForStateEx(BZPServerRunState state, int timeoutMS)
{
	BZP_C_API_GUARD_BEGIN()
	if (!isValidRunState(state))
	{
		Logger::warn(SSTR << "bzpWaitForState: invalid target state (" << static_cast<int>(state) << ")");
		return BZP_WAIT_INVALID_STATE;
	}

	if (timeoutMS < -1)
	{
		Logger::warn(SSTR << "bzpWaitForState: invalid timeout (" << timeoutMS << ")");
		return BZP_WAIT_INVALID_TIMEOUT;
	}

	if (isServerThreadCurrent() && bzpGetServerRunState() != state)
	{
		Logger::warn("bzpWaitForState() called from the server thread before the requested state was reached");
		return BZP_WAIT_DEADLOCK;
	}

	return waitForRunState(state, timeoutMS) ? BZP_WAIT_OK : BZP_WAIT_TIMEOUT;
	BZP_C_API_GUARD_END_RETURN_INT(BZP_WAIT_FAILED)
}

int bzpWaitForState(BZPServerRunState state, int timeoutMS)
{
	return bzpWaitForStateEx(state, timeoutMS) == BZP_WAIT_OK ? 1 : 0;
}

BZPWaitResult bzpWaitForShutdownEx(int timeoutMS)
{
	BZP_C_API_GUARD_BEGIN()
	if (timeoutMS < -1)
	{
		Logger::warn(SSTR << "bzpWaitForShutdown: invalid timeout (" << timeoutMS << ")");
		return BZP_WAIT_INVALID_TIMEOUT;
	}

	if (isServerThreadCurrent() && bzpGetServerRunState() != EStopped)
	{
		Logger::warn("bzpWaitForShutdown() called from the server thread before shutdown completed");
		return BZP_WAIT_DEADLOCK;
	}

	if (!waitForRunState(EStopped, timeoutMS))
	{
		return BZP_WAIT_TIMEOUT;
	}

	const int joined = joinServerThread("bzpWaitForShutdown");
	restoreAutomaticGLibHandlers();
	if (!joined)
	{
		return BZP_WAIT_JOIN_FAILED;
	}

	releaseRuntimeBluezAdapter();
	releaseRuntimeServer();
	return BZP_WAIT_OK;
	BZP_C_API_GUARD_END_RETURN_INT(BZP_WAIT_FAILED)
}

int bzpWaitForShutdown(int timeoutMS)
{
	return bzpWaitForShutdownEx(timeoutMS) == BZP_WAIT_OK ? 1 : 0;
}

namespace {

enum class StartupMode
{
	Threaded,
	ManualIteration
};

BZPStartResult startServerWithMode(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS, int enableBondable, StartupMode startupMode)
{
	try
	{
		if (bzpGetServerRunState() == EStopped && !serverThread.joinable())
		{
			releaseRuntimeBluezAdapter();
			releaseRuntimeServer();
		}

		if (!pServiceName || strlen(pServiceName) == 0)
		{
			Logger::error("bzpStart: pServiceName cannot be null or empty");
			return BZP_START_INVALID_ARGUMENT;
		}
		if (!pAdvertisingName)
		{
			Logger::error("bzpStart: pAdvertisingName cannot be null");
			return BZP_START_INVALID_ARGUMENT;
		}
		if (!pAdvertisingShortName)
		{
			Logger::error("bzpStart: pAdvertisingShortName cannot be null");
			return BZP_START_INVALID_ARGUMENT;
		}
		if (!getter)
		{
			Logger::error("bzpStart: getter delegate cannot be null");
			return BZP_START_INVALID_ARGUMENT;
		}
		if (!setter)
		{
			Logger::error("bzpStart: setter delegate cannot be null");
			return BZP_START_INVALID_ARGUMENT;
		}
		if (startupMode == StartupMode::Threaded && maxAsyncInitTimeoutMS != 0 && (maxAsyncInitTimeoutMS < 100 || maxAsyncInitTimeoutMS > 60000))
		{
			Logger::error(SSTR << "bzpStart: maxAsyncInitTimeoutMS (" << maxAsyncInitTimeoutMS
				<< ") must be 0 or between 100 and 60000 milliseconds");
			return BZP_START_INVALID_TIMEOUT;
		}
		if (startupMode == StartupMode::ManualIteration && maxAsyncInitTimeoutMS != 0)
		{
			Logger::error("bzpStartManual: maxAsyncInitTimeoutMS must be 0 in manual-iteration mode");
			return BZP_START_INVALID_TIMEOUT;
		}
		if (strlen(pServiceName) > 255)
		{
			Logger::error(SSTR << "bzpStart: pServiceName too long (" << strlen(pServiceName) << " > 255)");
			return BZP_START_SERVICE_NAME_TOO_LONG;
		}

		const bool autoInstalledGLibHandlers = installAutomaticGLibHandlers();
		ScopedAction restoreGLibHandlersOnFailure{[] {
			restoreAutomaticGLibHandlers();
		}};

		Logger::info(SSTR << "Starting BzPeri server '" << pAdvertisingName << "'");

		auto server = std::make_shared<Server>(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, enableBondable != 0);
		setActiveServerForRuntime(server);

		const std::size_t configuratorCount = serviceConfiguratorCount();
		if (configuratorCount == 0)
		{
			Logger::info("No service configurators registered; starting with an empty GATT database");
		}
		else
		{
			applyRegisteredServiceConfigurators(*server);
			Logger::trace(SSTR << "Applied " << configuratorCount << " service configurator(s)");
		}

		setServerHealth(EOk);
		setServerRunState(EInitializing);
		auto *adapter = ensureRuntimeBluezAdapter();

		if (startupMode == StartupMode::ManualIteration)
		{
			if (!startServerLoopManually(server.get(), adapter))
			{
				Logger::error("Unable to initialize the manual BzPeri run loop");
				setServerHealth(EFailedInit);
				setServerRunState(EStopped);
				releaseRuntimeBluezAdapter();
				releaseRuntimeServer();
				return BZP_START_MANUAL_LOOP_INIT_FAILED;
			}

			Logger::trace("BzPeri manual run loop started; host must drive it with bzpRunLoopIteration()");
			if (autoInstalledGLibHandlers)
			{
				restoreGLibHandlersOnFailure.release();
			}
			return BZP_START_OK;
		}

		try
		{
			serverThread = std::thread([server, adapter] {
				try
				{
					runServerThread(server.get(), adapter);
				}
				catch (const std::exception& ex)
				{
					Logger::error(SSTR << "Unhandled exception in server thread: " << ex.what());
					setServerHealth(EFailedInit);
					setServerRunState(EStopped);
				}
				catch (...)
				{
					Logger::error("Unhandled non-standard exception in server thread");
					setServerHealth(EFailedInit);
					setServerRunState(EStopped);
				}

				restoreAutomaticGLibHandlers();
			});
		}
		catch(std::system_error &ex)
		{
			Logger::error(SSTR << "Server thread was unable to start (code " << ex.code() << ") during bzpStart(): " << ex.what());

			setServerHealth(EFailedInit);
			setServerRunState(EStopped);
			releaseRuntimeBluezAdapter();
			releaseRuntimeServer();
			return BZP_START_THREAD_START_FAILED;
		}

		if (maxAsyncInitTimeoutMS == 0)
		{
			Logger::trace("BzPeri server thread started; initialization continues asynchronously");
			if (autoInstalledGLibHandlers)
			{
				restoreGLibHandlersOnFailure.release();
			}
			return BZP_START_OK;
		}

		std::unique_lock<std::mutex> lock(stateChangedMutex);
		bool initCompleted = stateChangedCV.wait_for(lock, std::chrono::milliseconds(maxAsyncInitTimeoutMS),
			[]() { return serverRunState.load(std::memory_order_acquire) > EInitializing; });
		lock.unlock();

		if (!initCompleted)
		{
			Logger::error("BzPeri server initialization timed out");
			setServerHealth(EFailedInit);
			shutdown();
		}

		if (bzpGetServerRunState() != ERunning)
		{
			if (!bzpWait())
			{
				Logger::warn(SSTR << "Unable to stop the server after an error in bzpStart()");
			}

			return initCompleted ? BZP_START_INIT_FAILED : BZP_START_INIT_TIMEOUT;
		}

		Logger::trace("BzPeri server has started");
		if (autoInstalledGLibHandlers)
		{
			restoreGLibHandlersOnFailure.release();
		}
		return BZP_START_OK;
	}
	catch(...)
	{
		Logger::error(SSTR << "Unknown exception during server startup");
		releaseRuntimeBluezAdapter();
		releaseRuntimeServer();
		return BZP_START_EXCEPTION;
	}
}

} // namespace

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
	return bzpStartWithBondableEx(
		pServiceName,
		pAdvertisingName,
		pAdvertisingShortName,
		getter,
		setter,
		maxAsyncInitTimeoutMS,
		enableBondable) == BZP_START_OK;
}

// Backward compatibility wrapper - calls bzpStartWithBondable with enableBondable=1 (true)
int bzpStart(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS)
{
	return bzpStartEx(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, maxAsyncInitTimeoutMS) == BZP_START_OK;
}

int bzpStartNoWait(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter)
{
	return bzpStartNoWaitEx(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter) == BZP_START_OK;
}

int bzpStartWithBondableNoWait(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, int enableBondable)
{
	return bzpStartWithBondableNoWaitEx(
		pServiceName,
		pAdvertisingName,
		pAdvertisingShortName,
		getter,
		setter,
		enableBondable) == BZP_START_OK;
}

int bzpStartManual(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter)
{
	return bzpStartManualEx(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter) == BZP_START_OK;
}

int bzpStartWithBondableManual(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, int enableBondable)
{
	return bzpStartWithBondableManualEx(
		pServiceName,
		pAdvertisingName,
		pAdvertisingShortName,
		getter,
		setter,
		enableBondable) == BZP_START_OK;
}

BZPStartResult bzpStartEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS)
{
	return bzpStartWithBondableEx(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, maxAsyncInitTimeoutMS, 1);
}

BZPStartResult bzpStartWithBondableEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS, int enableBondable)
{
	return startServerWithMode(
		pServiceName,
		pAdvertisingName,
		pAdvertisingShortName,
		getter,
		setter,
		maxAsyncInitTimeoutMS,
		enableBondable,
		StartupMode::Threaded);
}

BZPStartResult bzpStartNoWaitEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter)
{
	return bzpStartEx(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, 0);
}

BZPStartResult bzpStartWithBondableNoWaitEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, int enableBondable)
{
	return bzpStartWithBondableEx(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, 0, enableBondable);
}

BZPStartResult bzpStartManualEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter)
{
	return bzpStartWithBondableManualEx(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, 1);
}

BZPStartResult bzpStartWithBondableManualEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
	BZPServerDataGetter getter, BZPServerDataSetter setter, int enableBondable)
{
	return startServerWithMode(
		pServiceName,
		pAdvertisingName,
		pAdvertisingShortName,
		getter,
		setter,
		0,
		enableBondable,
		StartupMode::ManualIteration);
}

int bzpRunLoopIteration(int mayBlock)
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopIterationEx(mayBlock) == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopIterationEx(int mayBlock)
{
	BZP_C_API_GUARD_BEGIN()
	return runServerLoopIterationEx(mayBlock);
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopIterationFor(int timeoutMS)
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopIterationForEx(timeoutMS) == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopIterationForEx(int timeoutMS)
{
	BZP_C_API_GUARD_BEGIN()
	return runServerLoopIterationForEx(timeoutMS);
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopAttach()
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopAttachEx() == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopAttachEx()
{
	BZP_C_API_GUARD_BEGIN()
	return attachServerLoopToCurrentThreadEx();
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopDetach()
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopDetachEx() == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopDetachEx()
{
	BZP_C_API_GUARD_BEGIN()
	return detachServerLoopFromCurrentThreadEx();
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopIsManualMode()
{
	BZP_C_API_GUARD_BEGIN()
	return isManualServerLoopMode() ? 1 : 0;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

int bzpRunLoopHasOwner()
{
	BZP_C_API_GUARD_BEGIN()
	return hasServerLoopOwner() ? 1 : 0;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

int bzpRunLoopIsCurrentThreadOwner()
{
	BZP_C_API_GUARD_BEGIN()
	return isCurrentThreadServerLoopOwner() ? 1 : 0;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

int bzpRunLoopInvoke(BZPRunLoopCallback callback, void *pUserData)
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopInvokeEx(callback, pUserData) == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopInvokeEx(BZPRunLoopCallback callback, void *pUserData)
{
	BZP_C_API_GUARD_BEGIN()
	return invokeOnServerLoopEx(callback, pUserData);
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopPollPrepare(int *pTimeoutMS, int *pRequiredFDCount, int *pDispatchReady)
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopPollPrepareEx(pTimeoutMS, pRequiredFDCount, pDispatchReady) == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopPollPrepareEx(int *pTimeoutMS, int *pRequiredFDCount, int *pDispatchReady)
{
	BZP_C_API_GUARD_BEGIN()
	return prepareServerLoopPollEx(pTimeoutMS, pRequiredFDCount, pDispatchReady);
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopPollQuery(BZPPollFD *pPollFDs, int pollFDCount, int *pRequiredFDCount)
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopPollQueryEx(pPollFDs, pollFDCount, pRequiredFDCount) == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopPollQueryEx(BZPPollFD *pPollFDs, int pollFDCount, int *pRequiredFDCount)
{
	BZP_C_API_GUARD_BEGIN()
	return queryServerLoopPollEx(pPollFDs, pollFDCount, pRequiredFDCount);
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopPollCheck(const BZPPollFD *pPollFDs, int pollFDCount)
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopPollCheckEx(pPollFDs, pollFDCount) == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopPollCheckEx(const BZPPollFD *pPollFDs, int pollFDCount)
{
	BZP_C_API_GUARD_BEGIN()
	return checkServerLoopPollEx(pPollFDs, pollFDCount);
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopPollDispatch()
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopPollDispatchEx() == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopPollDispatchEx()
{
	BZP_C_API_GUARD_BEGIN()
	return dispatchServerLoopPollEx();
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopPollCancel()
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopPollCancelEx() == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopPollCancelEx()
{
	BZP_C_API_GUARD_BEGIN()
	return cancelServerLoopPollEx();
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopDriveUntilState(BZPServerRunState state, int timeoutMS)
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopDriveUntilStateEx(state, timeoutMS) == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopDriveUntilStateEx(BZPServerRunState state, int timeoutMS)
{
	BZP_C_API_GUARD_BEGIN()
	if (!isValidRunState(state))
	{
		Logger::warn(SSTR << "bzpRunLoopDriveUntilState: invalid target state (" << static_cast<int>(state) << ")");
		return BZP_RUN_LOOP_INVALID_STATE;
	}

	if (timeoutMS < -1)
	{
		Logger::warn(SSTR << "bzpRunLoopDriveUntilState: invalid timeout (" << timeoutMS << ")");
		return BZP_RUN_LOOP_INVALID_TIMEOUT;
	}

	if (!isManualServerLoopMode())
	{
		Logger::warn("bzpRunLoopDriveUntilState() is only valid after bzpStartManual() or bzpStartWithBondableManual()");
		return BZP_RUN_LOOP_NOT_MANUAL_MODE;
	}

	if (bzpGetServerRunState() == state)
	{
		return BZP_RUN_LOOP_OK;
	}

	const auto deadline = timeoutMS < 0
		? std::chrono::steady_clock::time_point::max()
		: std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMS);

	while (bzpGetServerRunState() != state)
	{
		int sliceMS = 0;
		if (timeoutMS < 0)
		{
			sliceMS = 50;
		}
		else
		{
			const auto now = std::chrono::steady_clock::now();
			if (now >= deadline)
			{
				break;
			}

			sliceMS = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
			sliceMS = std::min(sliceMS, 50);
		}

		const BZPRunLoopResult iterationResult = runServerLoopIterationForEx(sliceMS);
		if (iterationResult < 0)
		{
			return iterationResult;
		}

		if (timeoutMS == 0)
		{
			break;
		}
	}

	return bzpGetServerRunState() == state ? BZP_RUN_LOOP_OK : BZP_RUN_LOOP_IDLE;
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}

int bzpRunLoopDriveUntilShutdown(int timeoutMS)
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopDriveUntilShutdownEx(timeoutMS) == BZP_RUN_LOOP_OK;
	BZP_C_API_GUARD_END_RETURN_INT(0)
}

BZPRunLoopResult bzpRunLoopDriveUntilShutdownEx(int timeoutMS)
{
	BZP_C_API_GUARD_BEGIN()
	return bzpRunLoopDriveUntilStateEx(EStopped, timeoutMS);
	BZP_C_API_GUARD_END_RETURN(BZP_RUN_LOOP_ACTIVATION_FAILED)
}
