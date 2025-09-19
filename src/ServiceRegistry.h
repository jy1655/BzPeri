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
// Internal header for the service registry implementation.
//
// This file provides the internal interface for applying registered service configurators to a BzPeri
// server instance. It is used internally by the server startup process to invoke all registered
// service configurators before the server begins accepting connections.
//
// >>
// >>>  IMPLEMENTATION NOTES
// >>
//
// This is an internal header - application code should use the public BzPeriConfigurator.h interface
// instead. The ServiceRegistry manages the collection of registered configurators and provides the
// mechanism to apply them all to a server during initialization.
//
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include "../include/BzPeriConfigurator.h"

namespace bzp {

class Server;

void applyRegisteredServiceConfigurators(Server& server);

} // namespace bzp
