// Copyright 2017-2019 Paul Nettle
//
// This file is part of Gobbledegook.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

// Simple test program for packet parser validation
// This ensures the packet parsing logic is robust against malformed input
//
// NOTE: This file is deprecated and requires HCI adapter functionality that has been removed.
// It is kept for historical purposes but is not included in the build.
// To compile this file, define BUILD_TESTING and ENABLE_HCI_TESTS.

#if defined(BUILD_TESTING) && defined(ENABLE_HCI_TESTS)
#include <iostream>
#include <vector>
#include <cassert>
#include <cstring>

#include "HciAdapter.h"
#include "Logger.h"

using namespace ggk;

class PacketParserTests
{
public:
    static bool runAllTests()
    {
        std::cout << "Running packet parser tests...\n";

        bool allPassed = true;
        allPassed &= testCommandCompleteEventParsing();
        allPassed &= testCommandStatusEventParsing();
        allPassed &= testDeviceConnectedEventParsing();
        allPassed &= testDeviceDisconnectedEventParsing();
        allPassed &= testMalformedPackets();
        allPassed &= testBoundaryConditions();

        std::cout << (allPassed ? "All tests PASSED\n" : "Some tests FAILED\n");
        return allPassed;
    }

private:
    static bool testCommandCompleteEventParsing()
    {
        std::cout << "Testing CommandCompleteEvent parsing...\n";

        // Create valid packet data
        std::vector<uint8_t> validPacket(sizeof(HciAdapter::CommandCompleteEvent));
        HciAdapter::HciHeader header = {0x0001, 0x0000, sizeof(uint16_t) + sizeof(uint8_t)};
        uint16_t commandCode = 0x0001;
        uint8_t status = 0x00;

        // Convert to network byte order
        header.toNetwork();
        commandCode = Utils::endianToHci(commandCode);

        // Pack into vector
        size_t offset = 0;
        std::memcpy(validPacket.data() + offset, &header, sizeof(header));
        offset += sizeof(header);
        std::memcpy(validPacket.data() + offset, &commandCode, sizeof(commandCode));
        offset += sizeof(commandCode);
        std::memcpy(validPacket.data() + offset, &status, sizeof(status));

        try {
            HciAdapter::CommandCompleteEvent event(validPacket);
            // If we get here without throwing, parsing succeeded
            std::cout << "  Valid packet parsing: PASS\n";
        } catch (...) {
            std::cout << "  Valid packet parsing: FAIL\n";
            return false;
        }

        // Test insufficient data
        std::vector<uint8_t> shortPacket(sizeof(HciAdapter::CommandCompleteEvent) - 1);
        try {
            HciAdapter::CommandCompleteEvent event(shortPacket);
            // Should handle gracefully (zero-initialize)
            std::cout << "  Short packet handling: PASS\n";
        } catch (...) {
            std::cout << "  Short packet handling: FAIL\n";
            return false;
        }

        return true;
    }

    static bool testCommandStatusEventParsing()
    {
        std::cout << "Testing CommandStatusEvent parsing...\n";

        // Create valid packet data
        std::vector<uint8_t> validPacket(sizeof(HciAdapter::CommandStatusEvent));
        HciAdapter::HciHeader header = {0x0002, 0x0000, sizeof(uint16_t) + sizeof(uint8_t)};
        uint16_t commandCode = 0x0001;
        uint8_t status = 0x00;

        // Convert to network byte order
        header.toNetwork();
        commandCode = Utils::endianToHci(commandCode);

        // Pack into vector
        size_t offset = 0;
        std::memcpy(validPacket.data() + offset, &header, sizeof(header));
        offset += sizeof(header);
        std::memcpy(validPacket.data() + offset, &commandCode, sizeof(commandCode));
        offset += sizeof(commandCode);
        std::memcpy(validPacket.data() + offset, &status, sizeof(status));

        try {
            HciAdapter::CommandStatusEvent event(validPacket);
            std::cout << "  Valid packet parsing: PASS\n";
        } catch (...) {
            std::cout << "  Valid packet parsing: FAIL\n";
            return false;
        }

        return true;
    }

    static bool testDeviceConnectedEventParsing()
    {
        std::cout << "Testing DeviceConnectedEvent parsing...\n";

        // Create valid packet data
        std::vector<uint8_t> validPacket(sizeof(HciAdapter::DeviceConnectedEvent));

        // Initialize with test data
        std::memset(validPacket.data(), 0, validPacket.size());

        try {
            HciAdapter::DeviceConnectedEvent event(validPacket);
            std::cout << "  Valid packet parsing: PASS\n";
        } catch (...) {
            std::cout << "  Valid packet parsing: FAIL\n";
            return false;
        }

        return true;
    }

    static bool testDeviceDisconnectedEventParsing()
    {
        std::cout << "Testing DeviceDisconnectedEvent parsing...\n";

        // Create valid packet data
        std::vector<uint8_t> validPacket(sizeof(HciAdapter::DeviceDisconnectedEvent));

        // Initialize with test data
        std::memset(validPacket.data(), 0, validPacket.size());

        try {
            HciAdapter::DeviceDisconnectedEvent event(validPacket);
            std::cout << "  Valid packet parsing: PASS\n";
        } catch (...) {
            std::cout << "  Valid packet parsing: FAIL\n";
            return false;
        }

        return true;
    }

    static bool testMalformedPackets()
    {
        std::cout << "Testing malformed packet handling...\n";

        // Test empty packet
        std::vector<uint8_t> emptyPacket;
        try {
            HciAdapter::CommandCompleteEvent event(emptyPacket);
            std::cout << "  Empty packet handling: PASS\n";
        } catch (...) {
            std::cout << "  Empty packet handling: FAIL\n";
            return false;
        }

        // Test single byte packet
        std::vector<uint8_t> singleByte = {0xFF};
        try {
            HciAdapter::CommandCompleteEvent event(singleByte);
            std::cout << "  Single byte packet handling: PASS\n";
        } catch (...) {
            std::cout << "  Single byte packet handling: FAIL\n";
            return false;
        }

        return true;
    }

    static bool testBoundaryConditions()
    {
        std::cout << "Testing boundary conditions...\n";

        // Test maximum size packet
        std::vector<uint8_t> maxPacket(4096, 0xFF);
        try {
            // This should not crash, even with garbage data
            HciAdapter::CommandCompleteEvent event(maxPacket);
            std::cout << "  Maximum size packet handling: PASS\n";
        } catch (...) {
            std::cout << "  Maximum size packet handling: FAIL\n";
            return false;
        }

        // Test exact minimum size
        std::vector<uint8_t> minPacket(sizeof(HciAdapter::CommandCompleteEvent), 0x00);
        try {
            HciAdapter::CommandCompleteEvent event(minPacket);
            std::cout << "  Minimum size packet handling: PASS\n";
        } catch (...) {
            std::cout << "  Minimum size packet handling: FAIL\n";
            return false;
        }

        return true;
    }
};

// Test runner that can be called from standalone or as a separate program
int main()
{
    // Register a simple console logger for testing
    Logger::registerDebugReceiver([](const char* msg) { std::cout << "[DEBUG] " << msg << std::endl; });
    Logger::registerInfoReceiver([](const char* msg) { std::cout << "[INFO] " << msg << std::endl; });
    Logger::registerErrorReceiver([](const char* msg) { std::cout << "[ERROR] " << msg << std::endl; });

    bool success = PacketParserTests::runAllTests();

    return success ? 0 : 1;
}

#else
// HCI tests are disabled - this file requires deprecated HCI adapter functionality
#include <iostream>
int main() {
    std::cout << "HCI packet parser tests are disabled. Define BUILD_TESTING and ENABLE_HCI_TESTS to enable." << std::endl;
    return 0;
}
#endif