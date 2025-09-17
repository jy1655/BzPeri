// Copyright (c) 2025 JaeYoung Hwang (BzPeri Project)
// Licensed under MIT License (see LICENSE file)
//
// Enhanced structured logging for BzPeri

#include "StructuredLogger.h"

namespace bzp {

// Global structured loggers for major components
StructuredLogger bluezLogger("BluezAdapter");
StructuredLogger gattLogger("GattServer");
StructuredLogger dbusLogger("DBusManager");

} // namespace bzp