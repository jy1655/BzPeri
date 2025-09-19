// Copyright (c) 2025 BzPeri Contributors
// Licensed under MIT License (see LICENSE file)
//
// This file is part of BzPeri.

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  INSIDE THIS FILE
// >>
//
// This header provides the interface for registering BzPeri's built-in example GATT services.
//
// These sample services demonstrate how to implement various types of Bluetooth LE characteristics:
// * Read-only characteristics (device info, time)
// * Read/Write characteristics (custom text)
// * Notify characteristics (battery level)
// * Complex data types (time structures, CPU info)
// * Custom UUIDs vs standard Bluetooth SIG UUIDs
//
// >>
// >>>  USAGE
// >>
//
// To use these sample services in your application:
//
// 1. Include this header in your main application
// 2. Call registerSampleServices() with your desired namespace
// 3. The services will be automatically registered when the server starts
//
// Example:
//     #include "SampleServices.h"
//
//     // Register sample services under the "examples" namespace
//     bzp::samples::registerSampleServices("examples");
//
//     // Start your BzPeri server - the sample services will be available
//
// >>
// >>>  NAMESPACE ORGANIZATION
// >>
//
// The namespace parameter controls where the services appear in the D-Bus object tree:
// * Empty string "": Services appear directly under the service root
// * "samples": Services appear under /com/bzperi/samples/
// * "demo": Services appear under /com/bzperi/demo/
//
// This allows multiple applications to use different namespaces to avoid conflicts.
//
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <string>

namespace bzp::samples {

// Registers the built-in example GATT services underneath the provided namespace node.
//
// This function registers a comprehensive set of sample services that demonstrate various
// Bluetooth LE GATT service patterns and implementation techniques.
//
// Services registered:
// * Device Information Service (0x180A) - Standard service with manufacturer/model info
// * Battery Service (0x180F) - Standard service with notification support
// * Current Time Service (0x1805) - Standard service returning structured time data
// * Custom Text Service - Demonstrates read/write/notify with custom UUIDs
// * ASCII Time Service - Simple string-based time service
// * CPU Information Service - System information with multiple characteristics
//
// Parameters:
//   namespaceNode: The namespace under which to register services (e.g., "samples", "demo")
//                  Empty string registers services at the root level
//
// Usage Notes:
// * The caller is responsible for clearing any existing configurators if they want to avoid
//   duplicate registrations - call bzp::clearServiceConfigurators() before this function
// * This function only registers the service configurators - the actual services are created
//   when the BzPeri server starts
// * Each service includes detailed implementation examples for different GATT patterns
//
// Thread Safety:
// * This function should only be called during application initialization, before starting
//   the BzPeri server
// * Not thread-safe - do not call from multiple threads simultaneously
void registerSampleServices(const std::string& namespaceNode);

} // namespace bzp::samples
