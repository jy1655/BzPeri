#include "ServiceRegistry.h"

#include <mutex>
#include <utility>
#include <vector>

#include "Server.h"

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
