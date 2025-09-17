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

	// Adds an update to the front of the queue for a characteristic at the given object path
	//
	// Returns non-zero value on success or 0 on failure.
	int bzpNofifyUpdatedCharacteristic(const char *pObjectPath);

	// Adds an update to the front of the queue for a descriptor at the given object path
	//
	// Returns non-zero value on success or 0 on failure.
	int bzpNofifyUpdatedDescriptor(const char *pObjectPath);

	// Adds a named update to the front of the queue. Generally, this routine should not be used directly. Instead, use the
	// `bzpNofifyUpdatedCharacteristic()` instead.
	//
	// Returns non-zero value on success or 0 on failure.
	int bzpPushUpdateQueue(const char *pObjectPath, const char *pInterfaceName);

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

	// Returns 1 if the queue is empty, otherwise 0
	int bzpUpdateQueueIsEmpty();

	// Returns the number of entries waiting in the queue
	int bzpUpdateQueueSize();

	// Removes all entries from the queue
	void bzpUpdateQueueClear();

	// -----------------------------------------------------------------------------------------------------------------------------
	// SERVER CONTROL
	// -----------------------------------------------------------------------------------------------------------------------------

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
	//     Retrieve this value using the `TheServer->getName()` method
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

	// Extended version of bzpStart with bondable configuration
	//
	// This is the preferred API for new applications. The basic bzpStart() function above calls this with
	// enableBondable=1 for backward compatibility.
	//
	int bzpStartWithBondable(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		BZPServerDataGetter getter, BZPServerDataSetter setter, int maxAsyncInitTimeoutMS, int enableBondable);

	// Blocks for up to maxAsyncInitTimeoutMS milliseconds until the server shuts down.
	//
	// If shutdown is successful, this method will return a non-zero value. Otherwise, it will return 0.
	//
	// If the server fails to stop for some reason, the thread will be killed.
	//
	// Typically, a call to this method would follow `bzpTriggerShutdown()`.
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

	// Retrieve the current running state of the server
	//
	// See `BZPServerRunState` (enumeration) for more information.
	enum BZPServerRunState bzpGetServerRunState();

	// Convert a `BZPServerRunState` into a human-readable string
	const char *bzpGetServerRunStateString(enum BZPServerRunState state);

	// Convenience method to check ServerRunState for a running server
	int bzpIsServerRunning();

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
