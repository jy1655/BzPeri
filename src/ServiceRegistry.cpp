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
// Implementation of the service registry for BzPeri's modular service configuration system.
//
// This file implements the registry that manages service configurators - functions that define
// and register GATT services with the BzPeri server. The registry allows services to be defined
// in separate modules and registered before server startup.
//
// >>
// >>>  THREAD SAFETY
// >>
//
// All registry operations are thread-safe using a global mutex. This ensures that configurators
// can be registered from multiple threads during application initialization without data races.
//
// >>
// >>>  IMPLEMENTATION DETAILS
// >>
//
// - configurators(): Static vector holding all registered service configurators
// - configuratorMutex(): Static mutex protecting access to the configurators vector
// - applyRegisteredServiceConfigurators(): Safely iterates through all registered configurators
//   and applies them to the provided server instance
//
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include "ServiceRegistry.h"

#include <mutex>
#include <utility>
#include <vector>

#include <bzp/Server.h>

namespace bzp {

namespace {

std::vector<ServiceConfigurator>& configurators()
{
	static std::vector<ServiceConfigurator> gConfigurators;
	return gConfigurators;
}

std::mutex& configuratorMutex()
{
	static std::mutex gMutex;
	return gMutex;
}

} // namespace

void registerServiceConfigurator(ServiceConfigurator configurator)
{
	std::lock_guard<std::mutex> lock(configuratorMutex());
	configurators().push_back(std::move(configurator));
}

void clearServiceConfigurators()
{
	std::lock_guard<std::mutex> lock(configuratorMutex());
	configurators().clear();
}

std::size_t serviceConfiguratorCount()
{
	std::lock_guard<std::mutex> lock(configuratorMutex());
	return configurators().size();
}

void applyRegisteredServiceConfigurators(Server& server)
{
	std::vector<ServiceConfigurator> snapshot;
	{
		std::lock_guard<std::mutex> lock(configuratorMutex());
		snapshot = configurators();
	}

	for (auto& configurator : snapshot)
	{
		if (configurator)
		{
			configurator(server);
		}
	}
}

} // namespace bzp
