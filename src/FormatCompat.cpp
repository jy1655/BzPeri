// Copyright (c) 2025 JaeYoung Hwang (BzPeri Project)
// Licensed under MIT License (see LICENSE file)
//
// C++20 std::format compatibility layer

#include "FormatCompat.h"
#include <cstdio>

namespace bzp {

std::string safeHex(uint8_t value) noexcept
{
#if HAS_STD_FORMAT
    try
    {
        return std::format("0x{:02X}", value);
    }
    catch (...)
    {
        // Fallback
    }
#endif
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02X", value);
    return std::string(buffer);
}

std::string safeHex(uint16_t value) noexcept
{
#if HAS_STD_FORMAT
    try
    {
        return std::format("0x{:04X}", value);
    }
    catch (...)
    {
        // Fallback
    }
#endif
    char buffer[10];
    std::snprintf(buffer, sizeof(buffer), "0x%04X", value);
    return std::string(buffer);
}

std::string safeHex(uint32_t value) noexcept
{
#if HAS_STD_FORMAT
    try
    {
        return std::format("0x{:08X}", value);
    }
    catch (...)
    {
        // Fallback
    }
#endif
    char buffer[12];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", value);
    return std::string(buffer);
}

std::string safeBluetoothAddress(const uint8_t* address) noexcept
{
    if (!address)
    {
        return "00:00:00:00:00:00";
    }

#if HAS_STD_FORMAT
    try
    {
        return std::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
            address[0], address[1], address[2], address[3], address[4], address[5]);
    }
    catch (...)
    {
        // Fallback
    }
#endif
    char buffer[18];
    std::snprintf(buffer, sizeof(buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
        address[0], address[1], address[2], address[3], address[4], address[5]);
    return std::string(buffer);
}

}; // namespace bzp