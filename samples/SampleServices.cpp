// Copyright 2017-2019 Paul Nettle
//
// This file is part of BzPeri.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  INSIDE THIS FILE
// >>
//
// This file contains comprehensive sample implementations of various Bluetooth LE GATT services.
// Each service demonstrates different patterns and techniques for building BLE applications with BzPeri.
//
// The services included are:
//
// * Device Information Service (0x180A) - Standard Bluetooth SIG service
//   Shows how to implement simple read-only characteristics with static string data
//
// * Battery Service (0x180F) - Standard Bluetooth SIG service with notifications
//   Demonstrates how to implement characteristics that can both be read and send notifications
//   when values change, using the BzPeri data getter/setter system
//
// * Current Time Service (0x1805) - Standard Bluetooth SIG service
//   Shows how to work with complex structured data types and real-time data generation
//
// * Custom Text Service - Custom service with read/write/notify capabilities
//   Demonstrates how to create custom services with custom UUIDs, handle write operations,
//   and implement bidirectional communication with descriptors
//
// * ASCII Time Service - Simple custom time service
//   Shows how to implement services that generate dynamic string data
//
// * CPU Information Service - System information service
//   Demonstrates how to integrate with system utilities and provide multiple related characteristics
//
// >>
// >>>  IMPLEMENTATION PATTERNS DEMONSTRATED
// >>
//
// 1. **Read-Only Characteristics**: Simple static data return (Device Info)
// 2. **Dynamic Read Characteristics**: Real-time data generation (Time services)
// 3. **Data-Driven Characteristics**: Using BzPeri's data getter/setter system (Battery)
// 4. **Read/Write Characteristics**: Bidirectional communication (Text service)
// 5. **Notification Support**: Sending change notifications to clients (Battery, Text)
// 6. **Custom vs Standard UUIDs**: When and how to use each type
// 7. **Descriptors**: Adding metadata and descriptions to characteristics
// 8. **Error Handling**: Proper error handling and logging
// 9. **Service Organization**: Grouping related characteristics into logical services
//
// >>
// >>>  UNDERSTANDING THE CODE STRUCTURE
// >>
//
// Each service follows this pattern:
//
// 1. Service Declaration: .gattServiceBegin(name, UUID)
// 2. Characteristic(s): .gattCharacteristicBegin(name, UUID, properties)
// 3. Event Handlers: .onReadValue(), .onWriteValue(), .onUpdatedValue()
// 4. Optional Descriptors: .gattDescriptorBegin()/.gattDescriptorEnd()
// 5. Service Completion: .gattServiceEnd()
//
// The lambda functions receive standard parameters:
// * self: Reference to the characteristic/descriptor being accessed
// * pConnection: D-Bus connection for sending responses
// * objectPath: D-Bus object path (usually not needed)
// * pParameters: Input parameters for write operations
// * pInvocation: Used to send response back to client
// * pUserData: Custom user data (usually nullptr)
//
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "SampleServices.h"

#include <ctime>
#include <vector>
#include <glib.h>

#include "ServiceRegistry.h"
#include "Server.h"
#include "ServerUtils.h"
#include "Utils.h"
#include "Logger.h"
#include "DBusObject.h"
#include "GattService.h"
#include "GattCharacteristic.h"
#include "GattDescriptor.h"

namespace bzp::samples {

namespace {

// Helper function to ensure the namespace node exists in the D-Bus object tree
//
// This function creates the namespace node if it doesn't exist, or returns the root
// if no namespace is specified. This allows services to be organized under different
// namespaces (e.g., "samples", "demo", etc.) or placed directly at the root level.
//
// Parameters:
//   root: The root D-Bus object where services are registered
//   ns: The namespace string (empty for root level placement)
//
// Returns: Reference to the namespace node where services should be registered
DBusObject& ensureNamespace(DBusObject& root, const std::string& ns)
{
	if (ns.empty())
	{
		return root;
	}

	return root.addChild(DBusObjectPath(ns));
}

} // namespace

void registerSampleServices(const std::string& namespaceNode)
{
	// Register a service configurator that will be called when the BzPeri server starts.
	// This uses a lambda capture to remember the namespace parameter.
	registerServiceConfigurator([namespaceNode](Server& server)
	{
		// Configure the server with our sample services using the fluent DSL interface.
		// The lambda receives the root D-Bus object where we can add our services.
		server.configure([namespaceNode](DBusObject& root)
		{
			// Create or get the namespace node where our sample services will live.
			// This could be "samples", "demo", or empty string for root level.
			DBusObject& samplesRoot = ensureNamespace(root, namespaceNode);

			// Build the complete set of sample services using BzPeri's fluent interface.
			// Each service demonstrates different BLE GATT patterns and techniques.
			samplesRoot

				//
				// DEVICE INFORMATION SERVICE (0x180A)
				//
				// This is a standard Bluetooth SIG service that provides basic device information.
				// It demonstrates the simplest pattern: read-only characteristics with static data.
				//
				// UUID 0x180A is the official Bluetooth SIG UUID for Device Information Service.
				// Client apps can discover this service by its well-known UUID.
				//
				.gattServiceBegin("device", "180A")

					// Manufacturer Name String Characteristic (0x2A29)
					//
					// This characteristic returns a simple string identifying the device manufacturer.
					// It's read-only, so clients can read it but not write to it.
					//
					// UUID 0x2A29 is the official Bluetooth SIG UUID for Manufacturer Name String.
					//
					.gattCharacteristicBegin("mfgr_name", "2A29", {"read"})

						// Handle read requests for the manufacturer name.
						//
						// This lambda is called whenever a BLE client reads this characteristic.
						// We return a static string "Acme Inc." - in a real application, this
						// would typically come from device configuration or build settings.
						//
						// The `true` parameter indicates this is the final response (no errors).
						//
						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							self.methodReturnValue(pInvocation, "Acme Inc.", true);
						})

					.gattCharacteristicEnd()

					// Model Number String Characteristic (0x2A24)
					//
					// Another simple read-only characteristic that returns the device model.
					// This demonstrates the same pattern as manufacturer name but with different data.
					//
					.gattCharacteristicBegin("model_num", "2A24", {"read"})

						// Handle read requests for the model number.
						//
						// Returns a static model string. The model name "Marvin-PA" is a playful
						// reference to Marvin the Paranoid Android (PA = Paranoid Android).
						//
						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							self.methodReturnValue(pInvocation, "Marvin-PA", true);
						})

					.gattCharacteristicEnd()

				.gattServiceEnd()

				//
				// BATTERY SERVICE (0x180F)
				//
				// This service demonstrates the data-driven characteristic pattern using BzPeri's
				// data getter/setter system. It also shows how to implement notifications.
				//
				// UUID 0x180F is the official Bluetooth SIG UUID for Battery Service.
				// This is one of the most commonly implemented BLE services.
				//
				.gattServiceBegin("battery", "180F")

					// Battery Level Characteristic (0x2A19)
					//
					// This characteristic can be both read and can send notifications when the
					// battery level changes. It demonstrates several important patterns:
					// 1. Data-driven characteristics using getDataValue()
					// 2. Notification support
					// 3. Integration with the BzPeri data management system
					//
					.gattCharacteristicBegin("level", "2A19", {"read", "notify"})

						// Handle read requests for battery level.
						//
						// Instead of returning static data, this uses BzPeri's data getter system.
						// The data is retrieved using the key "battery/level" which corresponds
						// to the data getter function in the main application (see standalone.cpp).
						//
						// The second parameter (0) is the default value if the data key is not found.
						//
						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							uint8_t batteryLevel = self.getDataValue<uint8_t>("battery/level", 0);
							self.methodReturnValue(pInvocation, batteryLevel, true);
						})

						// Handle notification events when battery level changes.
						//
						// This lambda is called when bzpNofifyUpdatedCharacteristic() is called
						// from the main application. It demonstrates how to send change notifications
						// to connected BLE clients.
						//
						// The function should return true to indicate successful notification.
						//
						.onUpdatedValue([](const GattCharacteristic& self, GDBusConnection* pConnection, void*) {
							uint8_t batteryLevel = self.getDataValue<uint8_t>("battery/level", 0);
							self.sendChangeNotificationValue(pConnection, batteryLevel);
							return true;
						})

					.gattCharacteristicEnd()

				.gattServiceEnd()

				//
				// CURRENT TIME SERVICE (0x1805)
				//
				// This service demonstrates how to work with complex structured data and real-time
				// data generation. It shows how BLE characteristics can return binary data that
				// follows the official Bluetooth specification format.
				//
				// UUID 0x1805 is the official Bluetooth SIG UUID for Current Time Service.
				//
				.gattServiceBegin("time", "1805")

					// Current Time Characteristic (0x2A2B)
					//
					// This characteristic returns the current time in the standard Bluetooth
					// Current Time format. It demonstrates several important concepts:
					// 1. Real-time data generation (time is calculated on each read)
					// 2. Binary data formatting according to Bluetooth specifications
					// 3. Error handling for system call failures
					// 4. Complex data structure packing
					//
					.gattCharacteristicBegin("current_time", "2A2B", {"read"})

						// Handle read requests for current time.
						//
						// This function generates real-time data by calling system time functions
						// and packing the result into the standard Bluetooth Current Time format.
						//
						// The Current Time format is defined in the Bluetooth specification as:
						// - Bytes 0-1: Year (little-endian 16-bit)
						// - Byte 2: Month (1-12)
						// - Byte 3: Day (1-31)
						// - Byte 4: Hour (0-23)
						// - Byte 5: Minute (0-59)
						// - Byte 6: Second (0-59)
						// - Byte 7: Day of week (0=Sunday, 6=Saturday)
						// - Byte 8: Fractions of second (1/256 units)
						// - Byte 9: Adjust reason (bitfield for DST, time zone, etc.)
						//
						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							// Get current time from system
							time_t timeVal = time(nullptr);
							struct tm *pTimeStruct = gmtime(&timeVal);

							// Handle potential failure of gmtime()
							if (nullptr == pTimeStruct)
							{
								Logger::warn("Unable to get current time");
								// Return empty vector on error
								self.methodReturnValue(pInvocation, std::vector<uint8_t>{}, true);
								return;
							}

							// Convert tm structure to Bluetooth Current Time format
							// Note: tm_year is years since 1900, tm_mon is 0-based
							uint16_t year = static_cast<uint16_t>(1900 + pTimeStruct->tm_year);
							uint8_t month = static_cast<uint8_t>(pTimeStruct->tm_mon + 1);
							uint8_t day = static_cast<uint8_t>(pTimeStruct->tm_mday);
							uint8_t hour = static_cast<uint8_t>(pTimeStruct->tm_hour);
							uint8_t minute = static_cast<uint8_t>(pTimeStruct->tm_min);
							uint8_t second = static_cast<uint8_t>(pTimeStruct->tm_sec);
							uint8_t dayOfWeek = static_cast<uint8_t>(pTimeStruct->tm_wday);
							uint8_t fractions256 = 0;  // We don't calculate sub-second precision

							// Pack data into 10-byte Bluetooth Current Time format
							std::vector<uint8_t> payload(10);
							payload[0] = year & 0xFF;        // Year low byte
							payload[1] = year >> 8;          // Year high byte
							payload[2] = month;              // Month (1-12)
							payload[3] = day;                // Day (1-31)
							payload[4] = hour;               // Hour (0-23)
							payload[5] = minute;             // Minute (0-59)
							payload[6] = second;             // Second (0-59)
							payload[7] = dayOfWeek;          // Day of week (0-6)
							payload[8] = fractions256;       // Fractions of second (0-255)
							payload[9] = 0;                  // Adjust reason (no adjustments)

							// Return the formatted time data
							self.methodReturnValue(pInvocation, payload, true);
						})

					.gattCharacteristicEnd()

				.gattServiceEnd()

				//
				// CUSTOM TEXT SERVICE
				//
				// This service demonstrates advanced BLE patterns including:
				// 1. Read/Write/Notify characteristics with custom UUIDs
				// 2. Bidirectional data flow (client can read and write)
				// 3. Automatic notifications when data changes
				// 4. Descriptors for metadata
				// 5. String data handling and conversion
				//
				// This service uses custom 128-bit UUIDs instead of standard Bluetooth SIG UUIDs.
				// Custom UUIDs are used when implementing proprietary services not covered by
				// the Bluetooth specification.
				//
				.gattServiceBegin("text", "00000001-1E3C-FAD4-74E2-97A033F1BFAA")

					// Text String Characteristic
					//
					// This is a comprehensive example showing read, write, and notify operations
					// on a single characteristic. It demonstrates:
					// 1. Using BzPeri's data pointer system for string storage
					// 2. Handling write operations with proper data conversion
					// 3. Triggering notifications when data changes
					// 4. GVariant handling for D-Bus data transfer
					//
					.gattCharacteristicBegin("string", "00000002-1E3C-FAD4-74E2-97A033F1BFAA", {"read", "write", "notify"})

						// Handle read requests for the text string.
						//
						// This uses getDataPointer() to retrieve string data that can be
						// modified by both read operations and write operations from clients.
						// The empty string "" is returned as default if no data is set.
						//
						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							const char* pString = self.getDataPointer<const char*>("text/string", "");
							self.methodReturnValue(pInvocation, pString, true);
						})

						// Handle write requests to update the text string.
						//
						// This demonstrates several important patterns:
						// 1. Extracting data from GVariant (D-Bus data format)
						// 2. Converting binary data to C++ string
						// 3. Storing data using setDataPointer()
						// 4. Triggering notifications after successful write
						// 5. Proper GVariant memory management
						//
						.onWriteValue([](const GattCharacteristic& self, GDBusConnection* pConnection, const std::string&, GVariant* pParameters, GDBusMethodInvocation* pInvocation, void* pUserData) {
							// Extract the byte array from the D-Bus parameters
							// BLE write operations send data as byte arrays
							GVariant* pAyBuffer = g_variant_get_child_value(pParameters, 0);
							std::string incoming = Utils::stringFromGVariantByteArray(pAyBuffer);
							g_variant_unref(pAyBuffer);  // Important: free GVariant memory

							// Store the new string value in BzPeri's data management system
							// This makes it available to the data getter in the main application
							self.setDataPointer("text/string", incoming.c_str());

							// Trigger a notification to inform connected clients of the change
							// This calls the onUpdatedValue handler below
							self.callOnUpdatedValue(pConnection, pUserData);

							// Send success response back to the client
							self.methodReturnVariant(pInvocation, nullptr);
						})

						// Handle notification events when the text string changes.
						//
						// This is called both when the characteristic is written by a client
						// (via callOnUpdatedValue above) and when the application calls
						// bzpNofifyUpdatedCharacteristic() from external code.
						//
						.onUpdatedValue([](const GattCharacteristic& self, GDBusConnection* pConnection, void*) {
							const char* pValue = self.getDataPointer<const char*>("text/string", "");
							self.sendChangeNotificationValue(pConnection, pValue);
							return true;
						})

						//
						// CHARACTERISTIC DESCRIPTOR EXAMPLE
						//
						// Descriptors provide metadata about characteristics. The most common
						// descriptor is the User Description (0x2901) which provides a human-
						// readable description of what the characteristic does.
						//
						.gattDescriptorBegin("description", "2901", {"read"})

							// Handle read requests for the characteristic description.
							//
							// This returns a fun, human-readable description of what this
							// characteristic does. Client applications can read this to
							// understand the characteristic's purpose.
							//
							.onReadValue([](const GattDescriptor& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
								const char *pDescription = "A mutable text string used for testing. Read and write to me, it tickles!";
								self.methodReturnValue(pInvocation, pDescription, true);
							})

						.gattDescriptorEnd()

					.gattCharacteristicEnd()

				.gattServiceEnd()

				//
				// ASCII TIME SERVICE
				//
				// This service provides a simpler alternative to the structured Current Time Service.
				// It demonstrates how to implement services that return human-readable string data
				// instead of binary-packed data structures.
				//
				// This service uses a custom UUID and provides time as a readable ASCII string,
				// making it easier for debugging and human inspection.
				//
				.gattServiceBegin("ascii_time", "00000001-1E3D-FAD4-74E2-97A033F1BFEE")

					// ASCII Time String Characteristic
					//
					// This characteristic returns the current local time as a human-readable string.
					// It demonstrates:
					// 1. Using localtime() instead of gmtime() for local timezone
					// 2. Converting time to ASCII string format
					// 3. String trimming to remove unwanted whitespace
					// 4. Simple real-time data generation with string output
					//
					.gattCharacteristicBegin("string", "00000002-1E3D-FAD4-74E2-97A033F1BFEE", {"read"})

						// Handle read requests for ASCII time string.
						//
						// This function generates a human-readable time string using the standard
						// POSIX asctime() function. The result is in the format:
						// "Wed Jun 30 21:49:08 1993\n"
						//
						// Key differences from the structured Current Time service:
						// - Uses localtime() instead of gmtime() (local vs UTC time)
						// - Returns ASCII string instead of binary data
						// - Much simpler to implement and debug
						// - Easier for humans to read during development
						//
						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							// Get current time and convert to local timezone
							time_t timeVal = time(nullptr);
							struct tm *pTimeStruct = localtime(&timeVal);

							// Convert to ASCII string and remove trailing newline
							// asctime() returns a string with format "Wed Jun 30 21:49:08 1993\n"
							std::string timeString = Utils::trim(asctime(pTimeStruct));

							// Return the cleaned time string
							self.methodReturnValue(pInvocation, timeString, true);
						})

						// Descriptor explaining what this characteristic does
						.gattDescriptorBegin("description", "2901", {"read"})

							.onReadValue([](const GattDescriptor& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
								const char *pDescription = "Returns the local time (as reported by POSIX asctime()) each time it is read";
								self.methodReturnValue(pInvocation, pDescription, true);
							})

						.gattDescriptorEnd()

					.gattCharacteristicEnd()

				.gattServiceEnd()

				//
				// CPU INFORMATION SERVICE
				//
				// This service demonstrates how to integrate with system utilities and provide
				// multiple related characteristics within a single service. It shows how to
				// expose system information via BLE characteristics.
				//
				// This service provides information about the system's CPU configuration,
				// which can be useful for system monitoring or diagnostic applications.
				//
				.gattServiceBegin("cpu", "0000B001-1E3D-FAD4-74E2-97A033F1BFEE")

					// CPU Count Characteristic
					//
					// This characteristic returns the number of CPU cores in the system.
					// It demonstrates:
					// 1. Integration with BzPeri's ServerUtils for system information
					// 2. Returning numeric data types (int16_t)
					// 3. Using system utilities within BLE characteristics
					//
					.gattCharacteristicBegin("count", "0000B002-1E3D-FAD4-74E2-97A033F1BFEE", {"read"})

						// Handle read requests for CPU count.
						//
						// This calls ServerUtils::getCpuInfo() to get system CPU information.
						// The function takes a reference parameter for CPU count and returns
						// additional CPU information (used by the model characteristic below).
						//
						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							int16_t cpuCount = 0;
							// ServerUtils::getCpuInfo() fills cpuCount and returns CPU model info
							ServerUtils::getCpuInfo(cpuCount);
							self.methodReturnValue(pInvocation, cpuCount, true);
						})

						// Descriptor for CPU count characteristic
						.gattDescriptorBegin("description", "2901", {"read"})

							.onReadValue([](const GattDescriptor& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
								const char *pDescription = "This might represent the number of CPUs in the system";
								self.methodReturnValue(pInvocation, pDescription, true);
							})

						.gattDescriptorEnd()

					.gattCharacteristicEnd()

					// CPU Model Characteristic
					//
					// This characteristic returns CPU model information as a string.
					// It demonstrates how to return both numeric and string data from
					// the same system utility function call.
					//
					.gattCharacteristicBegin("model", "0000B003-1E3D-FAD4-74E2-97A033F1BFEE", {"read"})

						// Handle read requests for CPU model information.
						//
						// This calls the same ServerUtils::getCpuInfo() function but uses
						// the string return value (CPU model) instead of the reference
						// parameter (CPU count).
						//
						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							int16_t cpuCount = 0;  // We don't use this here, but the function requires it
							// Use the return value which contains CPU model information
							self.methodReturnValue(pInvocation, ServerUtils::getCpuInfo(cpuCount), true);
						})

						// Descriptor for CPU model characteristic
						.gattDescriptorBegin("description", "2901", {"read"})

							.onReadValue([](const GattDescriptor& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
								const char *pDescription = "Possibly the model of the CPU in the system";
								self.methodReturnValue(pInvocation, pDescription, true);
							})

						.gattDescriptorEnd()

					.gattCharacteristicEnd()

				.gattServiceEnd();

			// End of service configuration
			// All sample services have been registered and will be available when the server starts
		});
	});
}

} // namespace bzp::samples
