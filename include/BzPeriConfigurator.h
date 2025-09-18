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
