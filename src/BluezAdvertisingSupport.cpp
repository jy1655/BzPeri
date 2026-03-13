// Copyright (c) 2025 BzPeri Contributors
// Licensed under MIT License (see LICENSE file)
//
// Internal helpers for selecting BLE advertising payload content.

#include "BluezAdvertisingSupport.h"

#include <bzp/DBusObject.h>
#include <bzp/GattProperty.h>
#include <bzp/GattService.h>
#include <bzp/GattUuid.h>
#include <bzp/Server.h>

#include <gio/gio.h>

#include <algorithm>
#include <cctype>
#include <string_view>
#include <unordered_set>

namespace bzp::detail {

namespace {

constexpr std::string_view kBluetoothBaseUuidSuffix = "-0000-1000-8000-00805f9b34fb";

std::string toLower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return value;
}

std::string normalizeUuid128(const std::string& value)
{
	const std::string normalized = GattUuid(value).toString128();
	return normalized.empty() ? normalized : toLower(normalized);
}

std::string shortenUuidForLegacyAdvertising(const std::string& normalizedUuid)
{
	if (normalizedUuid.size() != 36 || normalizedUuid.size() <= kBluetoothBaseUuidSuffix.size())
	{
		return {};
	}

	if (normalizedUuid.compare(normalizedUuid.size() - kBluetoothBaseUuidSuffix.size(),
		kBluetoothBaseUuidSuffix.size(), kBluetoothBaseUuidSuffix) != 0)
	{
		return {};
	}

	if (normalizedUuid.rfind("0000", 0) == 0)
	{
		return normalizedUuid.substr(4, 4);
	}

	return normalizedUuid.substr(0, 8);
}

void collectGattServiceUUIDsFromObject(
	const DBusObject& object,
	std::vector<std::string>& uuids,
	std::unordered_set<std::string>& seen)
{
	for (const auto& interface : object.getInterfaces())
	{
		auto service = std::dynamic_pointer_cast<const GattService>(interface);
		if (service == nullptr)
		{
			continue;
		}

		const GattProperty* uuidProperty = service->findProperty("UUID");
		if (uuidProperty == nullptr || !uuidProperty->getValueRef())
		{
			continue;
		}

		GVariant* variant = uuidProperty->getValueRef().get();
		if (!g_variant_is_of_type(variant, G_VARIANT_TYPE_STRING))
		{
			continue;
		}

		const std::string normalized = normalizeUuid128(g_variant_get_string(variant, nullptr));
		if (!normalized.empty() && seen.insert(normalized).second)
		{
			uuids.push_back(normalized);
		}
	}

	for (const auto& child : object.getChildren())
	{
		collectGattServiceUUIDsFromObject(child, uuids, seen);
	}
}

} // namespace

bool canUseExtendedAdvertising(const BluezCapabilities& capabilities) noexcept
{
	return capabilities.maxAdvertisingDataLength > 31;
}

std::vector<std::string> collectGattServiceUUIDs(const Server& server)
{
	std::vector<std::string> uuids;
	std::unordered_set<std::string> seen;

	for (const auto& object : server.getObjects())
	{
		collectGattServiceUUIDsFromObject(object, uuids, seen);
	}

	return uuids;
}

std::vector<std::string> selectAdvertisementServiceUUIDs(
	const std::vector<std::string>& serviceUUIDs,
	const BluezCapabilities& capabilities)
{
	if (canUseExtendedAdvertising(capabilities))
	{
		return serviceUUIDs;
	}

	std::vector<std::string> selected;
	std::unordered_set<std::string> seen;

	for (const auto& uuid : serviceUUIDs)
	{
		const std::string shortened = shortenUuidForLegacyAdvertising(uuid);
		if (!shortened.empty() && seen.insert(shortened).second)
		{
			selected.push_back(shortened);
		}
	}

	return selected;
}

} // namespace bzp::detail
