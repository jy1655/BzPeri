// Copyright (c) 2025 BzPeri Contributors
// Licensed under MIT License (see LICENSE file)
//
// This file is part of BzPeri.

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  BZPERI CONFIGURATOR SUPPORT
// >>
//
// This header provides everything needed to use BzPeri's service configurator API.
// Include this single header to get access to all configurator functionality.
//
// >>
// >>>  QUICK START
// >>
//
// 1. Include this header in your configurator source file:
//    #include <bzp/ConfiguratorSupport.h>
//
// 2. Define your service configurator:
//    void configureMyServices(bzp::Server& server) {
//        server.configure([](bzp::DBusObject& root) {
//            root.gattServiceBegin("my_service", "12345678-1234-1234-1234-123456789ABC")
//                .gattCharacteristicBegin("my_char", "87654321-4321-4321-4321-ABCDEF123456", {"read", "write"})
//                    .onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) { ... })
//                    .onWriteValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) { ... })
//                .gattCharacteristicEnd()
//            .gattServiceEnd();
//        });
//    }
//
// 3. Register your configurator:
//    bzp::registerServiceConfigurator(configureMyServices);
//
// >>
// >>>  WHAT'S INCLUDED
// >>
//
// This header includes all the types and macros needed for configurator development:
//
// - bzp::Server - Server configuration interface
// - bzp::DBusObject - Root object for service tree
// - bzp::GattService - Service definition interface
// - bzp::GattCharacteristic - Characteristic definition interface
// - bzp::GattDescriptor - Descriptor definition interface
// - bzp::GattUuid - UUID handling for services/characteristics/descriptors
// - Lambda-based callbacks for GATT event handling
//
// >>
// >>>  USAGE PATTERNS
// >>
//
// Standard BLE Services (using Bluetooth SIG UUIDs):
//   .gattServiceBegin("battery", "180F")  // Battery Service
//   .gattServiceBegin("device_info", "180A")  // Device Information
//   .gattServiceBegin("current_time", "1805")  // Current Time Service
//
// Custom Services (using 128-bit UUIDs):
//   .gattServiceBegin("my_service", "12345678-1234-1234-1234-123456789ABC")
//
// Characteristic Properties:
//   {"read"} - Read-only characteristic
//   {"write"} - Write-only characteristic
//   {"read", "write"} - Read/write characteristic
//   {"read", "notify"} - Read + notification support
//   {"read", "write", "notify"} - Full-featured characteristic
//
// Event Handlers:
//   .onReadValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) { ... })
//   .onWriteValue([](const GattCharacteristic& self, GDBusConnection*, const std::string&, GVariant*, GDBusMethodInvocation* pInvocation, void*) { ... })
//   .onUpdatedValue([](const GattCharacteristic& self, GDBusConnection*, void*) { ... })
//
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

// Core configurator types
#include <bzp/Server.h>
#include <bzp/DBusObject.h>
#include <bzp/GattService.h>
#include <bzp/GattCharacteristic.h>
#include <bzp/GattDescriptor.h>
#include <bzp/GattUuid.h>

// No special callback macros needed - use direct lambda functions

// Main configurator API (from BzPeriConfigurator.h)
#include "../BzPeriConfigurator.h"

/**
 * @brief BzPeri Configurator Support
 *
 * This namespace contains all the types and functions needed to configure
 * BzPeri GATT services using the modern configurator API.
 *
 * The configurator API uses a fluent interface pattern where method calls
 * are chained together to build the service tree. Each method returns a
 * reference to the appropriate type for the next step in the chain.
 *
 * Example configuration flow:
 * 1. Server::configure() provides DBusObject& root
 * 2. DBusObject::gattServiceBegin() returns GattService&
 * 3. GattService::gattCharacteristicBegin() returns GattCharacteristic&
 * 4. GattCharacteristic::onReadValue/onWriteValue() returns GattCharacteristic&
 * 5. GattCharacteristic::gattCharacteristicEnd() returns GattService&
 * 6. GattService::gattServiceEnd() returns DBusObject&
 *
 * This pattern ensures type safety and provides IntelliSense/auto-completion
 * support in modern IDEs.
 */
namespace bzp {

// All types are declared in their respective headers above.
// This namespace declaration ensures they're all accessible under bzp::.

} // namespace bzp