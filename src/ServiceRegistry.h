#pragma once

#include "../include/BzPeriConfigurator.h"

namespace bzp {

class Server;

void applyRegisteredServiceConfigurators(Server& server);

} // namespace bzp
