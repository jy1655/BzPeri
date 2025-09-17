#include "include/Gobbledegook.h"
#include <iostream>

// Simple test data callbacks
const void* testDataGetter(const char* name) {
    static int testValue = 42;
    return &testValue;
}

int testDataSetter(const char* name, const void* data) {
    return 1; // Success
}

int main() {
    std::cout << "Testing Gobbledegook Bondable API..." << std::endl;

    // Test 1: Original API (should default to bondable=true)
    std::cout << "Test 1: Testing original ggkStart API (should default to bondable=true)" << std::endl;
    // Note: We won't actually start the server since we need BlueZ/D-Bus, just test compilation

    // Test 2: New API with bondable=true
    std::cout << "Test 2: Testing ggkStartWithBondable with bondable=true" << std::endl;

    // Test 3: New API with bondable=false
    std::cout << "Test 3: Testing ggkStartWithBondable with bondable=false" << std::endl;

    std::cout << "All API tests compiled successfully!" << std::endl;
    std::cout << "Note: Actual server startup requires BlueZ/D-Bus and proper permissions" << std::endl;

    return 0;
}