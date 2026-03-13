// Copyright 2017-2019 Paul Nettle (original Gobbledegook)
// Copyright 2025 JaeYoung Hwang (BzPeri Project modernization)
//
// This file is part of BzPeri.
//
// Use of this source code is governed by the MIT license that can be found
// in the LICENSE file in the root of the source tree.

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  INSIDE THIS FILE
// >>
//
// This file represents the complete interface to BzPeri from a stand-alone application.
//
// >>
// >>>  DISCUSSION
// >>
//
// The interface to BzPeri is rether simple. It consists of the following categories of functionality:
//
//     * Logging
//
//       The server defers all logging to the application. The application registers a set of logging delegates (one for each
//       log level) so it can manage the logs however it wants (syslog, console, file, an external logging service, etc.)
//
//     * Managing updates to server data
//
//       The application is required to implement two delegates (`BZPServerDataGetter` and `BZPServerDataSetter`) for sharing data
//       with the server. See standalone.cpp for an example of how this is done.
//
//       In addition, the server provides a thread-safe queue for notifications of data updates to the server. Generally, the only
//       methods an application will need to call are `bzpNofifyUpdatedCharacteristic` and `bzpNofifyUpdatedDescriptor`. The other
//       methods are provided in case an application requies extended functionality.
//
//     * Server control
//
//       A small set of methods for starting and stopping the server.
//
//     * Server state
//
//       These routines allow the application to query the server's current state. The server runs through these states during its
//       lifecycle:
//
//           EUninitialized -> EInitializing -> ERunning -> EStopping -> EStopped
//
//     * Server health
//
//       The server maintains its own health information. The states are:
//
//           EOk         - the server is A-OK
//           EFailedInit - the server had a failure prior to the ERunning state
//           EFailedRun  - the server had a failure during the ERunning state
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

	// -----------------------------------------------------------------------------------------------------------------------------
	// LOGGING
	// -----------------------------------------------------------------------------------------------------------------------------

	// Type definition for callback delegates that receive log messages
	typedef void (*BZPLogReceiver)(const char *pMessage);

	// Type definition for callbacks that should execute on BzPeri's dedicated GLib run loop.
	typedef void (*BZPRunLoopCallback)(void *pUserData);

	// GLib-hidden poll-descriptor record for integrating the manual BzPeri run loop into a host poll/select loop.
	//
	// `events` and `revents` use the same bit layout as the native platform `poll(2)` API.
	typedef struct BZPPollFD
	{
		int fd;
		unsigned short events;
		unsigned short revents;
	} BZPPollFD;

	// Controls how BzPeri interacts with process-global GLib print/log handlers.
	enum BZPGLibLogCaptureMode
	{
		BZP_GLIB_LOG_CAPTURE_AUTOMATIC = 0,
		BZP_GLIB_LOG_CAPTURE_DISABLED = 1,
		BZP_GLIB_LOG_CAPTURE_HOST_MANAGED = 2,
		BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN = 3
	};

	enum BZPGLibLogCaptureTarget
	{
		BZP_GLIB_LOG_CAPTURE_TARGET_PRINT = 1 << 0,
		BZP_GLIB_LOG_CAPTURE_TARGET_PRINTERR = 1 << 1,
		BZP_GLIB_LOG_CAPTURE_TARGET_LOG = 1 << 2,
		BZP_GLIB_LOG_CAPTURE_TARGET_ALL =
			BZP_GLIB_LOG_CAPTURE_TARGET_PRINT
			| BZP_GLIB_LOG_CAPTURE_TARGET_PRINTERR
			| BZP_GLIB_LOG_CAPTURE_TARGET_LOG
	};

	enum BZPGLibLogCaptureDomain
	{
		BZP_GLIB_LOG_CAPTURE_DOMAIN_DEFAULT = 1 << 0,
		BZP_GLIB_LOG_CAPTURE_DOMAIN_GLIB = 1 << 1,
		BZP_GLIB_LOG_CAPTURE_DOMAIN_GIO = 1 << 2,
		BZP_GLIB_LOG_CAPTURE_DOMAIN_BLUEZ = 1 << 3,
		BZP_GLIB_LOG_CAPTURE_DOMAIN_OTHER = 1 << 4,
		BZP_GLIB_LOG_CAPTURE_DOMAIN_ALL =
			BZP_GLIB_LOG_CAPTURE_DOMAIN_DEFAULT
			| BZP_GLIB_LOG_CAPTURE_DOMAIN_GLIB
			| BZP_GLIB_LOG_CAPTURE_DOMAIN_GIO
			| BZP_GLIB_LOG_CAPTURE_DOMAIN_BLUEZ
			| BZP_GLIB_LOG_CAPTURE_DOMAIN_OTHER
	};

	enum BZPGLibLogCaptureResult
	{
		BZP_GLIB_LOG_CAPTURE_RESULT_OK = 0,
		BZP_GLIB_LOG_CAPTURE_RESULT_WRONG_MODE = 1,
		BZP_GLIB_LOG_CAPTURE_RESULT_NOT_INSTALLED = 2,
		BZP_GLIB_LOG_CAPTURE_RESULT_FAILED = 3
	};

	enum BZPGLibLogCaptureModeSetResult
	{
		BZP_GLIB_LOG_CAPTURE_MODE_SET_OK = 0,
		BZP_GLIB_LOG_CAPTURE_MODE_SET_INVALID_MODE = 1
	};

	enum BZPGLibLogCaptureTargetsSetResult
	{
		BZP_GLIB_LOG_CAPTURE_TARGETS_SET_OK = 0,
		BZP_GLIB_LOG_CAPTURE_TARGETS_SET_INVALID_TARGETS = 1
	};

	enum BZPGLibLogCaptureDomainsSetResult
	{
		BZP_GLIB_LOG_CAPTURE_DOMAINS_SET_OK = 0,
		BZP_GLIB_LOG_CAPTURE_DOMAINS_SET_INVALID_DOMAINS = 1
	};

	enum BZPQueryResult
	{
		BZP_QUERY_OK = 1,
		BZP_QUERY_INVALID_ARGUMENT = -1,
		BZP_QUERY_FAILED = -2
	};

	// Each of these methods registers a log receiver method. Receivers are set when registered. To unregister a log receiver,
	// simply register with `nullptr`.
	void bzpLogRegisterDebug(BZPLogReceiver receiver);
	void bzpLogRegisterInfo(BZPLogReceiver receiver);
	void bzpLogRegisterStatus(BZPLogReceiver receiver);
	void bzpLogRegisterWarn(BZPLogReceiver receiver);
	void bzpLogRegisterError(BZPLogReceiver receiver);
	void bzpLogRegisterFatal(BZPLogReceiver receiver);
	void bzpLogRegisterAlways(BZPLogReceiver receiver);
	void bzpLogRegisterTrace(BZPLogReceiver receiver);

	// Control whether BzPeri captures process-wide GLib print/log handlers during startup.
	//
	// When enabled (default), GLib print/log output is routed through the registered BzPeri log receivers while the server
	// is active. When disabled, BzPeri leaves the process-global GLib handlers untouched, which is often preferable for
	// embedded use inside larger host applications.
	//
	// This legacy boolean API maps to `BZP_GLIB_LOG_CAPTURE_AUTOMATIC` when enabled and
	// `BZP_GLIB_LOG_CAPTURE_DISABLED` when disabled. Use `bzpSetGLibLogCaptureMode()` for host-managed integration.
	void bzpSetGLibLogCaptureEnabled(int enabled);
	int bzpGetGLibLogCaptureEnabled();
	enum BZPQueryResult bzpGetGLibLogCaptureEnabledEx(int *pEnabled);

	// Configure how BzPeri captures GLib process-global print/log handlers.
	//
	// `AUTOMATIC` (default): startup/shutdown install and restore the handlers automatically.
	// `DISABLED`: BzPeri never installs the handlers.
	// `HOST_MANAGED`: startup/shutdown do not touch the handlers; the host may explicitly call
	// `bzpInstallGLibLogCapture()` / `bzpRestoreGLibLogCapture()`.
	// `STARTUP_AND_SHUTDOWN`: capture GLib handlers during initialization and again during shutdown, but release them once the
	// server reaches `ERunning` so the process-global override does not remain active for the full runtime.
	void bzpSetGLibLogCaptureMode(enum BZPGLibLogCaptureMode mode);
	enum BZPGLibLogCaptureModeSetResult bzpSetGLibLogCaptureModeEx(enum BZPGLibLogCaptureMode mode);
	enum BZPGLibLogCaptureMode bzpGetGLibLogCaptureMode();
	void bzpSetGLibLogCaptureTargets(unsigned int targets);
	enum BZPGLibLogCaptureTargetsSetResult bzpSetGLibLogCaptureTargetsEx(unsigned int targets);
	unsigned int bzpGetGLibLogCaptureTargets();
	unsigned int bzpGetConfiguredGLibLogCaptureTargets();
	void bzpSetGLibLogCaptureDomains(unsigned int domains);
	enum BZPGLibLogCaptureDomainsSetResult bzpSetGLibLogCaptureDomainsEx(unsigned int domains);
	unsigned int bzpGetGLibLogCaptureDomains();

	// Returns the build-time default GLib capture mode configured into this BzPeri build.
	enum BZPGLibLogCaptureMode bzpGetConfiguredGLibLogCaptureMode();

	// Explicitly install GLib process-global handler capture in `HOST_MANAGED` mode.
	//
	// Returns non-zero on success, otherwise 0.
	int bzpInstallGLibLogCapture();
	// Detailed result-code variant of bzpInstallGLibLogCapture().
	enum BZPGLibLogCaptureResult bzpInstallGLibLogCaptureEx();

	// Explicitly restore the previously-installed GLib process-global handlers in `HOST_MANAGED` mode.
	//
	// Returns non-zero on success, otherwise 0.
	int bzpRestoreGLibLogCapture();
	// Detailed result-code variant of bzpRestoreGLibLogCapture().
	enum BZPGLibLogCaptureResult bzpRestoreGLibLogCaptureEx();

	// Returns non-zero when BzPeri currently has GLib process-global handlers installed.
	int bzpIsGLibLogCaptureInstalled();
	enum BZPQueryResult bzpIsGLibLogCaptureInstalledEx(int *pInstalled);

	// -----------------------------------------------------------------------------------------------------------------------------
	// SERVER DATA
	// -----------------------------------------------------------------------------------------------------------------------------

	// Type definition for a delegate that the server will use when it needs to receive data from the host application
	//
	// IMPORTANT:
	//
	// This will be called from the server's thread. Be careful to ensure your implementation is thread safe.
	//
	// Similarly, the pointer to data returned to the server should point to non-volatile memory so that the server can use it
	// safely for an indefinite period of time.
	typedef const void *(*BZPServerDataGetter)(const char *pName);

	// Type definition for a delegate that the server will use when it needs to notify the host application that data has changed
	//
	// IMPORTANT:
	//
	// This will be called from the server's thread. Be careful to ensure your implementation is thread safe.
	//
	// The data setter uses void* types to allow receipt of unknown data types from the server. Ensure that you do not store these
	// pointers. Copy the data before returning from your getter delegate.
	//
	// This method returns a non-zero value on success or 0 on failure.
	//
	// Possible failures:
	//
	//   * pName is null
	//   * pData is null
	//   * pName is not a supported value to store
	//   * Any other failure, as deemed by the delegate handler
	typedef int (*BZPServerDataSetter)(const char *pName, const void *pData);

	// -----------------------------------------------------------------------------------------------------------------------------
	// SERVER DATA UPDATE MANAGEMENT
	// -----------------------------------------------------------------------------------------------------------------------------

	// Detailed result codes for update-queue operations.
	enum BZPUpdateQueueResult
	{
		BZP_UPDATE_QUEUE_OK = 1,
		BZP_UPDATE_QUEUE_EMPTY = 0,
		BZP_UPDATE_QUEUE_BUFFER_TOO_SMALL = -1,
		BZP_UPDATE_QUEUE_INVALID_ARGUMENT = -2
	};

	// Detailed result codes for enqueueing update notifications.
	enum BZPUpdateEnqueueResult
	{
		BZP_UPDATE_ENQUEUE_OK = 1,
		BZP_UPDATE_ENQUEUE_INVALID_ARGUMENT = -1,
		BZP_UPDATE_ENQUEUE_NOT_RUNNING = -2
	};

	// Detailed result codes for startup operations.
	enum BZPStartResult
	{
		BZP_START_OK = 1,
		BZP_START_INVALID_ARGUMENT = -1,
		BZP_START_INVALID_TIMEOUT = -2,
		BZP_START_SERVICE_NAME_TOO_LONG = -3,
		BZP_START_MANUAL_LOOP_INIT_FAILED = -4,
		BZP_START_THREAD_START_FAILED = -5,
		BZP_START_INIT_TIMEOUT = -6,
		BZP_START_INIT_FAILED = -7,
		BZP_START_EXCEPTION = -8
	};

	// Adds an update to the front of the queue for a characteristic at the given object path
	//
	// Returns non-zero value on success or 0 on failure.
	[[deprecated("Use bzpNotifyUpdatedCharacteristic() instead")]]
	int bzpNofifyUpdatedCharacteristic(const char *pObjectPath);

	// Adds an update to the front of the queue for a descriptor at the given object path
	//
	// Returns non-zero value on success or 0 on failure.
	[[deprecated("Use bzpNotifyUpdatedDescriptor() instead")]]
	int bzpNofifyUpdatedDescriptor(const char *pObjectPath);

	// Correctly-spelled versions (preferred)
	// Adds an update to the front of the queue for a characteristic at the given object path
	//
	// Returns non-zero value on success or 0 on failure.
	int bzpNotifyUpdatedCharacteristic(const char *pObjectPath);

	// Adds an update to the front of the queue for a descriptor at the given object path
	//
	// Returns non-zero value on success or 0 on failure.
	int bzpNotifyUpdatedDescriptor(const char *pObjectPath);

	// Adds a named update to the front of the queue. Generally, this routine should not be used directly. Instead, use the
	// `bzpNofifyUpdatedCharacteristic()` instead.
	//
	// Returns non-zero value on success or 0 on failure.
	int bzpPushUpdateQueue(const char *pObjectPath, const char *pInterfaceName);

	// Detailed enqueue helpers that distinguish invalid arguments from "server is not running".
	enum BZPUpdateEnqueueResult bzpNotifyUpdatedCharacteristicEx(const char *pObjectPath);
	enum BZPUpdateEnqueueResult bzpNotifyUpdatedDescriptorEx(const char *pObjectPath);
	enum BZPUpdateEnqueueResult bzpPushUpdateQueueEx(const char *pObjectPath, const char *pInterfaceName);

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
	int bzpPopUpdateQueue(char *pElement, int elementLen, int keep);

	// Detailed variant of bzpPopUpdateQueue().
	//
	// Returns:
	//   BZP_UPDATE_QUEUE_OK on success
	//   BZP_UPDATE_QUEUE_EMPTY if the queue is empty
	//   BZP_UPDATE_QUEUE_BUFFER_TOO_SMALL if the buffer cannot hold the entry
	//   BZP_UPDATE_QUEUE_INVALID_ARGUMENT if pElement is null or elementLen <= 0
	enum BZPUpdateQueueResult bzpPopUpdateQueueEx(char *pElement, int elementLen, int keep);

	// Returns 1 if the queue is empty, otherwise 0
	int bzpUpdateQueueIsEmpty();
	enum BZPQueryResult bzpUpdateQueueIsEmptyEx(int *pIsEmpty);

	// Returns the number of entries waiting in the queue
	int bzpUpdateQueueSize();
	enum BZPQueryResult bzpUpdateQueueSizeEx(int *pSize);

	// Removes all entries from the queue
	void bzpUpdateQueueClear();
	enum BZPQueryResult bzpUpdateQueueClearEx(int *pClearedCount);

	// -----------------------------------------------------------------------------------------------------------------------------
	// SERVER CONTROL
	// -----------------------------------------------------------------------------------------------------------------------------

	// Set the server state to 'EInitializing' and then immediately create a server thread and initiate the server's async
	// processing on the server thread.
	//
	// At that point the current thread will block for up to maxAsyncInitTimeoutMS milliseconds or until initialization completes.
	//
	// If `maxAsyncInitTimeoutMS == 0`, the method returns immediately after the server thread has been created and the server
	// enters `EInitializing`. In that mode, use `bzpWaitForState(ERunning, timeoutMS)` or `bzpGetServerRunState()` to observe
	// the asynchronous initialization result.
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
	// serviceName: The name of our server (collectino of services)
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
	//     Retrieve this value using the active Server instance (for example via `getActiveServer()` in C++).
	//
	// advertisingName: The name for this controller, as advertised over LE
	//
	//     IMPORTANT: Setting the advertisingName will change the system-wide name of the device. If that's not what you want, set
	//     BOTH advertisingName and advertisingShortName to as empty string ("") to prevent setting the advertising
	//     name.
	//
	//     Retrieve this value using the `getAdvertisingName()` method
	//
	// advertisingShortName: The short name for this controller, as advertised over LE
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
	// enableBondable: Enable or disable device bonding/pairing capability (default: 1/true)
	//
	//     When non-zero (default), the adapter will accept pairing requests from client devices and allow them to bond.
	//     When zero, pairing requests will be rejected, which may cause immediate disconnection for devices that
	//     require security/authentication.
	//
	//     Modern BLE applications typically require bonding for security, so this should generally be left as non-zero
	//     unless you specifically need an open, non-authenticated connection.
	//
	int bzpStart(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS);

	// Detailed-result variant of bzpStart().
	enum BZPStartResult bzpStartEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS);

	// Extended version of bzpStart with bondable configuration
	//
	// This is the preferred API for new applications. The basic bzpStart() function above calls this with
	// enableBondable=1 for backward compatibility.
	//
	// `maxAsyncInitTimeoutMS` uses the same startup semantics described above. A value of `0` means "do not wait for
	// initialization completion".
	//
	int bzpStartWithBondable(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS, int enableBondable);

	// Detailed-result variant of bzpStartWithBondable().
	enum BZPStartResult bzpStartWithBondableEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS, int enableBondable);

	// Start the server without waiting for asynchronous initialization to complete.
	//
	// This is equivalent to calling `bzpStart(..., 0)` and returns after the server thread has been created and the server has
	// entered `EInitializing`.
	//
	// Use `bzpWaitForState(ERunning, timeoutMS)` or `bzpGetServerRunState()` to observe the eventual initialization result.
	int bzpStartNoWait(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter);

	// Detailed-result variant of bzpStartNoWait().
	enum BZPStartResult bzpStartNoWaitEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter);

	// Bondable-aware no-wait startup variant.
	//
	// This is equivalent to calling `bzpStartWithBondable(..., 0, enableBondable)`.
	int bzpStartWithBondableNoWait(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter, int enableBondable);

	// Detailed-result variant of bzpStartWithBondableNoWait().
	enum BZPStartResult bzpStartWithBondableNoWaitEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter, int enableBondable);

	// Start the server in manual-iteration mode without creating the internal server thread.
	//
	// This returns after the dedicated GLib context has been created and initialization has been scheduled. The caller must then
	// repeatedly invoke `bzpRunLoopIteration()` until the server reaches `ERunning` or `EStopped`.
	int bzpStartManual(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter);

	// Detailed-result variant of bzpStartManual().
	enum BZPStartResult bzpStartManualEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter);

	// Bondable-aware manual-iteration startup variant.
	//
	// This is the preferred way to integrate BzPeri into a host that already owns an event loop and cannot dedicate a thread to
	// `g_main_loop_run()`.
	int bzpStartWithBondableManual(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter, int enableBondable);

	// Detailed-result variant of bzpStartWithBondableManual().
	enum BZPStartResult bzpStartWithBondableManualEx(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter, int enableBondable);

	// Blocks for up to maxAsyncInitTimeoutMS milliseconds until the server shuts down.
	//
	// If shutdown is successful, this method will return a non-zero value. Otherwise, it will return 0.
	//
	// If the server fails to stop for some reason, the thread will be killed.
	//
	// Typically, a call to this method would follow `bzpTriggerShutdown()`.
	//
	// This is the indefinite-wait form. For integration code that must avoid an unbounded block, prefer
	// `bzpWaitForShutdown()`.
	int bzpWait();

	// Tells the server to begin the shutdown process
	//
	// The shutdown process will interrupt any currently running asynchronous operation and prevent new operations from starting.
	// Once the server has stabilized, its event processing loop is terminated and the server is cleaned up.
	//
	// `bzpGetServerRunState` will return EStopped when shutdown is complete. To block until the shutdown is complete, see
	// `bzpWait()`.
	//
	// Alternatively, you can use `bzpShutdownAndWait()` to request the shutdown and block until the shutdown is complete.
	void bzpTriggerShutdown();

	// Convenience method to trigger a shutdown (via `bzpTriggerShutdown()`) and also waits for shutdown to complete (via
	// `bzpWait()`)
	int bzpShutdownAndWait();

	// -----------------------------------------------------------------------------------------------------------------------------
	// SERVER STATE
	// -----------------------------------------------------------------------------------------------------------------------------

	// Current state of the server
	//
	// States should progress through states in this order:
	//
	//     EUninitialized -> EInitializing -> ERunning -> EStopping -> EStopped
	//
	// Note that in some cases, a server may skip one or more states, as is the case of a failed initialization where the server
	// will progress from EInitializing directly to EStopped.
	//
	// Use `bzpGetServerRunState` to retrive the state and `bzpGetServerRunStateString` to convert a `BZPServerRunState` into a
	// human-readable string.
	enum BZPServerRunState
	{
		EUninitialized,
		EInitializing,
		ERunning,
		EStopping,
		EStopped
	};

	enum BZPWaitResult
	{
		BZP_WAIT_OK = 0,
		BZP_WAIT_TIMEOUT = 1,
		BZP_WAIT_INVALID_STATE = 2,
		BZP_WAIT_INVALID_TIMEOUT = 3,
		BZP_WAIT_DEADLOCK = 4,
		BZP_WAIT_JOIN_FAILED = 5,
		BZP_WAIT_FAILED = 6,
		BZP_WAIT_NOT_RUNNING = 7
	};

	enum BZPShutdownTriggerResult
	{
		BZP_SHUTDOWN_TRIGGER_OK = 1,
		BZP_SHUTDOWN_TRIGGER_NOT_RUNNING = 0,
		BZP_SHUTDOWN_TRIGGER_ALREADY_STOPPING = -1,
		BZP_SHUTDOWN_TRIGGER_FAILED = -2
	};

	enum BZPShutdownTriggerResult bzpTriggerShutdownEx();

	enum BZPRunLoopResult
	{
		BZP_RUN_LOOP_OK = 1,
		BZP_RUN_LOOP_IDLE = 0,
		BZP_RUN_LOOP_INVALID_ARGUMENT = -1,
		BZP_RUN_LOOP_NOT_MANUAL_MODE = -2,
		BZP_RUN_LOOP_NOT_ACTIVE = -3,
		BZP_RUN_LOOP_WRONG_THREAD = -4,
		BZP_RUN_LOOP_POLL_CYCLE_ACTIVE = -5,
		BZP_RUN_LOOP_NO_POLL_CYCLE = -6,
		BZP_RUN_LOOP_BUFFER_TOO_SMALL = -7,
		BZP_RUN_LOOP_ACTIVATION_FAILED = -8,
		BZP_RUN_LOOP_ALLOCATION_FAILED = -9,
		BZP_RUN_LOOP_INVALID_STATE = -10,
		BZP_RUN_LOOP_INVALID_TIMEOUT = -11,
		BZP_RUN_LOOP_NOT_ATTACHED = -12
	};

	enum BZPWaitResult bzpShutdownAndWaitEx();
	enum BZPWaitResult bzpWaitEx();

	// Retrieve the current running state of the server
	//
	// See `BZPServerRunState` (enumeration) for more information.
	enum BZPServerRunState bzpGetServerRunState();

	// Wait until the server reaches the exact target state.
	//
	// `timeoutMS` is interpreted as follows:
	//   * `timeoutMS < 0`: wait indefinitely
	//   * `timeoutMS == 0`: check immediately and return without blocking
	//   * `timeoutMS > 0`: wait up to the specified number of milliseconds
	//
	// This is particularly useful after `bzpStart*()` is invoked with `maxAsyncInitTimeoutMS == 0`.
	//
	// Returns non-zero if the requested state was reached, otherwise 0.
	int bzpWaitForState(enum BZPServerRunState state, int timeoutMS);
	enum BZPWaitResult bzpWaitForStateEx(enum BZPServerRunState state, int timeoutMS);

	// Wait until shutdown is complete and the internal server thread has been joined.
	//
	// This is a bounded alternative to `bzpWait()` that avoids an unbounded block in event-driven hosts.
	//
	// `timeoutMS` uses the same semantics as `bzpWaitForState()`.
	//
	// Returns non-zero if shutdown completed within the requested timeout, otherwise 0.
	int bzpWaitForShutdown(int timeoutMS);
	enum BZPWaitResult bzpWaitForShutdownEx(int timeoutMS);

	// Run one iteration of the dedicated GLib context when the server was started with `bzpStartManual()` or
	// `bzpStartWithBondableManual()`.
	//
	// `mayBlock == 0` performs a non-blocking poll and returns immediately.
	// `mayBlock != 0` blocks until one source is dispatched or shutdown cleanup completes.
	//
	// Returns non-zero if work was dispatched or shutdown cleanup completed, otherwise 0.
	int bzpRunLoopIteration(int mayBlock);
	enum BZPRunLoopResult bzpRunLoopIterationEx(int mayBlock);

	// Run one iteration of the dedicated GLib context with a bounded timeout.
	//
	// `timeoutMS < 0` waits indefinitely.
	// `timeoutMS == 0` performs a non-blocking poll.
	// `timeoutMS > 0` waits up to the requested timeout and returns 0 if no work was dispatched before it expired.
	int bzpRunLoopIterationFor(int timeoutMS);
	enum BZPRunLoopResult bzpRunLoopIterationForEx(int timeoutMS);

	// Explicitly attach the manual run loop to the current thread.
	//
	// This is optional. If not called, the first successful `bzpRunLoopIteration*()` call implicitly attaches the current thread.
	// Returns non-zero on success, otherwise 0.
	int bzpRunLoopAttach();
	enum BZPRunLoopResult bzpRunLoopAttachEx();

	// Detach the manual run loop from the current thread without shutting it down.
	//
	// After detaching, another thread may attach by calling `bzpRunLoopAttach()` or `bzpRunLoopIteration*()`.
	// Returns non-zero on success, otherwise 0.
	int bzpRunLoopDetach();
	enum BZPRunLoopResult bzpRunLoopDetachEx();

	// Returns non-zero when the server is currently using the manual run-loop execution model.
	int bzpRunLoopIsManualMode();
	enum BZPQueryResult bzpRunLoopIsManualModeEx(int *pIsManualMode);

	// Returns non-zero when the manual run loop currently has an owning thread attached.
	int bzpRunLoopHasOwner();
	enum BZPQueryResult bzpRunLoopHasOwnerEx(int *pHasOwner);

	// Returns non-zero when the calling thread is the current manual run-loop owner.
	int bzpRunLoopIsCurrentThreadOwner();
	enum BZPQueryResult bzpRunLoopIsCurrentThreadOwnerEx(int *pIsOwner);

	// Schedule a callback to run on BzPeri's dedicated GLib run loop.
	//
	// In the default threaded mode, the callback executes on the internal server thread.
	// In manual mode, the callback executes when the host next drives `bzpRunLoopIteration()`.
	//
	// Returns non-zero if the callback was queued successfully, otherwise 0.
	int bzpRunLoopInvoke(BZPRunLoopCallback callback, void *pUserData);
	enum BZPRunLoopResult bzpRunLoopInvokeEx(BZPRunLoopCallback callback, void *pUserData);

	// Begin a GLib-hidden poll cycle for the manual run loop.
	//
	// This acquires the dedicated run-loop context and prepares it for `query -> check -> dispatch/cancel`.
	// It is only valid after `bzpStartManual()` / `bzpStartWithBondableManual()`.
	//
	// Outputs are optional:
	//   * `pTimeoutMS`: the current timeout in milliseconds (`-1` means "wait indefinitely")
	//   * `pRequiredFDCount`: the number of poll descriptors required by `bzpRunLoopPollQuery()`
	//   * `pDispatchReady`: non-zero when work is ready immediately and `bzpRunLoopPollDispatch()` can run without polling
	//
	// Returns non-zero if the poll cycle was prepared successfully, otherwise 0.
	int bzpRunLoopPollPrepare(int *pTimeoutMS, int *pRequiredFDCount, int *pDispatchReady);
	enum BZPRunLoopResult bzpRunLoopPollPrepareEx(int *pTimeoutMS, int *pRequiredFDCount, int *pDispatchReady);

	// Query the poll descriptors for the currently active manual run-loop poll cycle.
	//
	// A discovery call with `pPollFDs == nullptr` and `pollFDCount == 0` is valid and stores the required descriptor count in
	// `pRequiredFDCount` (if non-null).
	//
	// Otherwise, if `pollFDCount` is smaller than the required count, the function returns 0 and stores the required size in
	// `pRequiredFDCount` (if non-null).
	//
	// Returns non-zero on success, otherwise 0.
	int bzpRunLoopPollQuery(BZPPollFD *pPollFDs, int pollFDCount, int *pRequiredFDCount);
	enum BZPRunLoopResult bzpRunLoopPollQueryEx(BZPPollFD *pPollFDs, int pollFDCount, int *pRequiredFDCount);

	// Check whether the currently active manual run-loop poll cycle is ready to dispatch after the host poll step.
	//
	// The caller should copy native poll results into `revents` before calling this.
	//
	// Returns non-zero when dispatch is ready, otherwise 0.
	int bzpRunLoopPollCheck(const BZPPollFD *pPollFDs, int pollFDCount);
	enum BZPRunLoopResult bzpRunLoopPollCheckEx(const BZPPollFD *pPollFDs, int pollFDCount);

	// Dispatch the currently active manual run-loop poll cycle.
	//
	// This releases the internal poll-cycle ownership. If shutdown cleanup completes during dispatch, that also counts as work.
	//
	// Returns non-zero if work was dispatched or shutdown cleanup completed, otherwise 0.
	int bzpRunLoopPollDispatch();
	enum BZPRunLoopResult bzpRunLoopPollDispatchEx();

	// Cancel the currently active manual run-loop poll cycle without dispatching any work.
	//
	// Returns non-zero if an active poll cycle was canceled, otherwise 0.
	int bzpRunLoopPollCancel();
	enum BZPRunLoopResult bzpRunLoopPollCancelEx();

	// Drive the manual run loop until the requested state is reached or the timeout expires.
	//
	// This is intended for hosts using `bzpStartManual()` / `bzpStartWithBondableManual()` that want bounded lifecycle helpers
	// without handing control back to `bzpWaitForState()`, which does not pump the manual run loop.
	//
	// `timeoutMS` uses the same semantics as `bzpWaitForState()`.
	int bzpRunLoopDriveUntilState(enum BZPServerRunState state, int timeoutMS);
	enum BZPRunLoopResult bzpRunLoopDriveUntilStateEx(enum BZPServerRunState state, int timeoutMS);

	// Convenience form of `bzpRunLoopDriveUntilState(EStopped, timeoutMS)`.
	int bzpRunLoopDriveUntilShutdown(int timeoutMS);
	enum BZPRunLoopResult bzpRunLoopDriveUntilShutdownEx(int timeoutMS);

	// Convert a `BZPServerRunState` into a human-readable string
	const char *bzpGetServerRunStateString(enum BZPServerRunState state);

	// Convenience method to check ServerRunState for a running server
	int bzpIsServerRunning();
	enum BZPQueryResult bzpIsServerRunningEx(int *pIsRunning);

	// -----------------------------------------------------------------------------------------------------------------------------
	// SERVER HEALTH
	// -----------------------------------------------------------------------------------------------------------------------------

	// The current health of the server
	//
	// A running server's health will always be EOk, therefore it is only necessary to check the health status after the server
	// has shut down to determine if it was shut down due to an unhealthy condition.
	//
	// Use `bzpGetServerHealth` to retrieve the health and `bzpGetServerHealthString` to convert a `BZPServerHealth` into a
	// human-readable string.
	enum BZPServerHealth
	{
		EOk,
		EFailedInit,
		EFailedRun
	};

	// Retrieve the current health of the server
	//
	// See `BZPServerHealth` (enumeration) for more information.
	enum BZPServerHealth bzpGetServerHealth();

	// Convert a `BZPServerHealth` into a human-readable string
	const char *bzpGetServerHealthString(enum BZPServerHealth state);

#ifdef __cplusplus
}
#endif //__cplusplus
