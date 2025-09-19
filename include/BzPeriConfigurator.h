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
// This header provides the public API for BzPeri's service configuration system.
//
// The configurator system allows modular service registration, where each service (or group of services)
// can be defined in separate modules and registered with the server before startup. This promotes clean
// separation of concerns and makes services easily testable and maintainable.
//
// >>
// >>>  USAGE
// >>
//
// To register services with BzPeri:
//
// 1. Define a service configurator function that takes a Server& parameter
// 2. Call registerServiceConfigurator() to register your configurator
// 3. When the server starts, all registered configurators will be called
//
// Example:
//     void configureMyService(Server& server) {
//         server.configure([](DBusObject& root) {
//             root.gattServiceBegin("my_service", "12345678-1234-1234-1234-123456789ABC")
//                 // ... service definition
//                 .gattServiceEnd();
//         });
//     }
//
//     registerServiceConfigurator(configureMyService);
//
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#ifdef __cplusplus
#include <cstddef>
#include <functional>

namespace bzp {

class Server;

using ServiceConfigurator = std::function<void(Server&)>;

// Register a new configurator that can append services/descriptors to the server prior to startup
void registerServiceConfigurator(ServiceConfigurator configurator);

// Remove all registered configurators
void clearServiceConfigurators();

// Retrieve the number of configurators currently registered
std::size_t serviceConfiguratorCount();

} // namespace bzp

#endif // __cplusplus
