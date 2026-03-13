// Copyright (c) 2025 BzPeri Contributors
// Licensed under MIT License (see LICENSE file)
//
// Internal helpers for selecting BLE advertising payload content.

#pragma once

#include <bzp/BluezTypes.h>
#include <string>
#include <vector>

namespace bzp {

struct Server;

namespace detail {

[[nodiscard]] bool canUseExtendedAdvertising(const BluezCapabilities& capabilities) noexcept;
[[nodiscard]] std::vector<std::string> collectGattServiceUUIDs(const Server& server);
[[nodiscard]] std::vector<std::string> selectAdvertisementServiceUUIDs(
	const std::vector<std::string>& serviceUUIDs,
	const BluezCapabilities& capabilities);

} // namespace detail

} // namespace bzp
