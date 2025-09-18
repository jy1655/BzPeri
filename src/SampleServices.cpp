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
	registerServiceConfigurator([namespaceNode](Server& server)
	{
		server.configure([namespaceNode](DBusObject& root)
		{
			DBusObject& samplesRoot = ensureNamespace(root, namespaceNode);

			samplesRoot
				// Device Information Service (0x180A)
				.gattServiceBegin("device", "180A")

					.gattCharacteristicBegin("mfgr_name", "2A29", {"read"})

						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							self.methodReturnValue(pInvocation, "Acme Inc.", true);
						})

					.gattCharacteristicEnd()

					.gattCharacteristicBegin("model_num", "2A24", {"read"})

						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							self.methodReturnValue(pInvocation, "Marvin-PA", true);
						})

					.gattCharacteristicEnd()

				.gattServiceEnd()

				// Battery Service (0x180F)
				.gattServiceBegin("battery", "180F")

					.gattCharacteristicBegin("level", "2A19", {"read", "notify"})

						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							uint8_t batteryLevel = self.getDataValue<uint8_t>("battery/level", 0);
							self.methodReturnValue(pInvocation, batteryLevel, true);
						})

						.onUpdatedValue([](const GattCharacteristic& self, GDBusConnection* pConnection, void*) {
							uint8_t batteryLevel = self.getDataValue<uint8_t>("battery/level", 0);
							self.sendChangeNotificationValue(pConnection, batteryLevel);
							return true;
						})

					.gattCharacteristicEnd()

				.gattServiceEnd()

				// Current Time Service (0x1805)
				.gattServiceBegin("time", "1805")

					.gattCharacteristicBegin("current_time", "2A2B", {"read"})

						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							time_t timeVal = time(nullptr);
							struct tm *pTimeStruct = gmtime(&timeVal);

							if (nullptr == pTimeStruct)
							{
								Logger::warn("Unable to get current time");
								self.methodReturnValue(pInvocation, std::vector<uint8_t>{}, true);
								return;
							}

							uint16_t year = static_cast<uint16_t>(1900 + pTimeStruct->tm_year);
							uint8_t month = static_cast<uint8_t>(pTimeStruct->tm_mon + 1);
							uint8_t day = static_cast<uint8_t>(pTimeStruct->tm_mday);
							uint8_t hour = static_cast<uint8_t>(pTimeStruct->tm_hour);
							uint8_t minute = static_cast<uint8_t>(pTimeStruct->tm_min);
							uint8_t second = static_cast<uint8_t>(pTimeStruct->tm_sec);
							uint8_t dayOfWeek = static_cast<uint8_t>(pTimeStruct->tm_wday);
							uint8_t fractions256 = 0;

							std::vector<uint8_t> payload(10);
							payload[0] = year & 0xFF;
							payload[1] = year >> 8;
							payload[2] = month;
							payload[3] = day;
							payload[4] = hour;
							payload[5] = minute;
							payload[6] = second;
							payload[7] = dayOfWeek;
							payload[8] = fractions256;
							payload[9] = 0;

							self.methodReturnValue(pInvocation, payload, true);
						})

					.gattCharacteristicEnd()

				.gattServiceEnd()

				// Custom Text Service
				.gattServiceBegin("text", "00000001-1E3C-FAD4-74E2-97A033F1BFAA")

					.gattCharacteristicBegin("string", "00000002-1E3C-FAD4-74E2-97A033F1BFAA", {"read", "write", "notify"})

						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							const char* pString = self.getDataPointer<const char*>("text/string", "");
							self.methodReturnValue(pInvocation, pString, true);
						})

						.onWriteValue([](const GattCharacteristic& self, GDBusConnection* pConnection, const std::string&, GVariant* pParameters, GDBusMethodInvocation* pInvocation, void* pUserData) {
							GVariant* pAyBuffer = g_variant_get_child_value(pParameters, 0);
							std::string incoming = Utils::stringFromGVariantByteArray(pAyBuffer);
							g_variant_unref(pAyBuffer);

							self.setDataPointer("text/string", incoming.c_str());
							self.callOnUpdatedValue(pConnection, pUserData);
							self.methodReturnVariant(pInvocation, nullptr);
						})

						.onUpdatedValue([](const GattCharacteristic& self, GDBusConnection* pConnection, void*) {
							const char* pValue = self.getDataPointer<const char*>("text/string", "");
							self.sendChangeNotificationValue(pConnection, pValue);
							return true;
						})

						.gattDescriptorBegin("description", "2901", {"read"})

							.onReadValue([](const GattDescriptor& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
								const char *pDescription = "A mutable text string used for testing. Read and write to me, it tickles!";
								self.methodReturnValue(pInvocation, pDescription, true);
							})

						.gattDescriptorEnd()

					.gattCharacteristicEnd()

				.gattServiceEnd()

				// ASCII time service
				.gattServiceBegin("ascii_time", "00000001-1E3D-FAD4-74E2-97A033F1BFEE")

					.gattCharacteristicBegin("string", "00000002-1E3D-FAD4-74E2-97A033F1BFEE", {"read"})

						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							time_t timeVal = time(nullptr);
							struct tm *pTimeStruct = localtime(&timeVal);
							std::string timeString = Utils::trim(asctime(pTimeStruct));
							self.methodReturnValue(pInvocation, timeString, true);
						})

						.gattDescriptorBegin("description", "2901", {"read"})

							.onReadValue([](const GattDescriptor& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
								const char *pDescription = "Returns the local time (as reported by POSIX asctime()) each time it is read";
								self.methodReturnValue(pInvocation, pDescription, true);
							})

						.gattDescriptorEnd()

					.gattCharacteristicEnd()

				.gattServiceEnd()

				// CPU information service
				.gattServiceBegin("cpu", "0000B001-1E3D-FAD4-74E2-97A033F1BFEE")

					.gattCharacteristicBegin("count", "0000B002-1E3D-FAD4-74E2-97A033F1BFEE", {"read"})

						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							int16_t cpuCount = 0;
							ServerUtils::getCpuInfo(cpuCount);
							self.methodReturnValue(pInvocation, cpuCount, true);
						})

						.gattDescriptorBegin("description", "2901", {"read"})

							.onReadValue([](const GattDescriptor& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
								const char *pDescription = "This might represent the number of CPUs in the system";
								self.methodReturnValue(pInvocation, pDescription, true);
							})

						.gattDescriptorEnd()

					.gattCharacteristicEnd()

					.gattCharacteristicBegin("model", "0000B003-1E3D-FAD4-74E2-97A033F1BFEE", {"read"})

						.onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
							int16_t cpuCount = 0;
							self.methodReturnValue(pInvocation, ServerUtils::getCpuInfo(cpuCount), true);
						})

						.gattDescriptorBegin("description", "2901", {"read"})

							.onReadValue([](const GattDescriptor& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) {
								const char *pDescription = "Possibly the model of the CPU in the system";
								self.methodReturnValue(pInvocation, pDescription, true);
							})

						.gattDescriptorEnd()

					.gattCharacteristicEnd()

				.gattServiceEnd();
		});
	});
}

} // namespace bzp::samples
