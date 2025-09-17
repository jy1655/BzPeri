// Test C++20 features used in the modernized code
#include <format>
#include <string>
#include <string_view>
#include <span>
#include <vector>
#include <cstdint>

// Test the modernized hex functions
std::string hex(uint8_t value)
{
    return std::format("0x{:02X}", value);
}

std::string hex(uint16_t value)
{
    return std::format("0x{:04X}", value);
}

std::string hex(uint32_t value)
{
    return std::format("0x{:08X}", value);
}

// Test the safe bluetooth address function
std::string bluetoothAddressString(std::span<const uint8_t, 6> address)
{
    return std::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
        address[0], address[1], address[2], address[3], address[4], address[5]);
}

// Test constexpr functions
constexpr uint8_t endianToHost(uint8_t value) noexcept
{
    return value;
}

constexpr uint16_t endianToHost(uint16_t value) noexcept
{
    // Mock implementation for testing
    return value;
}

int main()
{
    // Test hex formatting
    auto hex8 = hex(static_cast<uint8_t>(0xFF));
    auto hex16 = hex(static_cast<uint16_t>(0xABCD));
    auto hex32 = hex(static_cast<uint32_t>(0x12345678));

    // Test bluetooth address formatting with span
    std::vector<uint8_t> mac = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    std::span<const uint8_t, 6> macSpan{mac.data(), 6};
    auto btAddr = bluetoothAddressString(macSpan);

    // Test constexpr functions
    constexpr auto test1 = endianToHost(static_cast<uint8_t>(42));
    constexpr auto test2 = endianToHost(static_cast<uint16_t>(1234));

    return 0;
}