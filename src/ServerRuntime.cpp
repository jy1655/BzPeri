// Runtime-owned server storage and accessors.

#include <bzp/Server.h>
#include "ServerCompat.h"

namespace bzp {

namespace {

std::shared_ptr<Server>& compatibilityServerStorage() noexcept
{
	static std::shared_ptr<Server> compatibilityServer;
	return compatibilityServer;
}

std::shared_ptr<Server>& runtimeServerStorage() noexcept
{
	static std::shared_ptr<Server> runtimeServer;
	return runtimeServer;
}

void syncCompatibilityStorageWithLegacy() noexcept
{
	auto legacyServer = getLegacyServerMirror();
	auto &compatibilityServer = compatibilityServerStorage();
	if (legacyServer.get() != compatibilityServer.get())
	{
		if (legacyServer)
		{
			compatibilityServer = std::move(legacyServer);
		}
		else
		{
			setLegacyServerMirror(compatibilityServer);
		}
	}
}

void syncLegacyWithRuntime() noexcept
{
	if (runtimeServerStorage())
	{
		setLegacyServerMirror(runtimeServerStorage());
	}
}

} // namespace

std::shared_ptr<Server> getActiveServer()
{
	if (runtimeServerStorage())
	{
		syncLegacyWithRuntime();
		return runtimeServerStorage();
	}

	syncCompatibilityStorageWithLegacy();
	return compatibilityServerStorage();
}

Server* getActiveServerPtr() noexcept
{
	if (runtimeServerStorage())
	{
		syncLegacyWithRuntime();
		return runtimeServerStorage().get();
	}

	syncCompatibilityStorageWithLegacy();
	return compatibilityServerStorage().get();
}

std::shared_ptr<Server> getRuntimeServer()
{
	return runtimeServerStorage();
}

Server* getRuntimeServerPtr() noexcept
{
	return runtimeServerStorage().get();
}

void setActiveServer(std::shared_ptr<Server> server)
{
	compatibilityServerStorage() = std::move(server);
	if (!runtimeServerStorage())
	{
		setLegacyServerMirror(compatibilityServerStorage());
	}
}

void setActiveServerForRuntime(std::shared_ptr<Server> server) noexcept
{
	runtimeServerStorage() = std::move(server);
	setLegacyServerMirror(runtimeServerStorage() ? runtimeServerStorage() : compatibilityServerStorage());
}

} // namespace bzp
