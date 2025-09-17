#include "StructuredLogger.h"

namespace ggk {

// Global structured loggers for major components
StructuredLogger bluezLogger("BluezAdapter");
StructuredLogger gattLogger("GattServer");
StructuredLogger dbusLogger("DBusManager");

} // namespace ggk