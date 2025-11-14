// Copyright (c) 2025 JaeYoung Hwang (BzPeri Project)
// Licensed under MIT License (see LICENSE file)
//
// C++20 std::format compatibility layer

#pragma once

#include <string>
#include <cstdint>
#include <cstdio>

// Try to use std::format, fall back to snprintf if not available
#ifndef HAS_STD_FORMAT
    #if __cpp_lib_format >= 201907L
        #include <format>
        #define HAS_STD_FORMAT 1
    #else
        #define HAS_STD_FORMAT 0
    #endif
#endif

#if HAS_STD_FORMAT
    #include <format>
#endif

namespace bzp {

// Safe format functions with fallback
template<typename... Args>
[[nodiscard]] std::string safeFormat(const std::string& format_str, Args&&... args)
{
#if HAS_STD_FORMAT
    try
    {
        // Use vformat for runtime format strings (std::format requires constexpr)
        return std::vformat(format_str, std::make_format_args(args...));
    }
    catch (const std::exception&)
    {
        // Fallback to basic concatenation on format error
        return format_str + " [format_error]";
    }
#else
    // Fallback implementation using snprintf
    constexpr size_t buffer_size = 256;
    char buffer[buffer_size];

    int result = std::snprintf(buffer, buffer_size, format_str.c_str(), args...);
    if (result > 0 && result < static_cast<int>(buffer_size))
    {
        return std::string(buffer);
    }
    else
    {
        return format_str + " [format_fallback]";
    }
#endif
}

// Specialized safe hex formatting
[[nodiscard]] std::string safeHex(uint8_t value) noexcept;
[[nodiscard]] std::string safeHex(uint16_t value) noexcept;
[[nodiscard]] std::string safeHex(uint32_t value) noexcept;

// Safe Bluetooth address formatting
[[nodiscard]] std::string safeBluetoothAddress(const uint8_t* address) noexcept;

}; // namespace bzp
