// Compatibility storage and accessors for the legacy global server handle.

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

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
#if defined(__GNUC__) && defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#if defined(__GNUC__) && !defined(__clang__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

void syncActiveServerStorageWithLegacy() noexcept
{
	if (runtimeServerStorage())
	{
		if (TheServer.get() != runtimeServerStorage().get())
		{
			TheServer = runtimeServerStorage();
		}
		return;
	}

	auto &compatibilityServer = compatibilityServerStorage();
	if (TheServer.get() != compatibilityServer.get())
	{
		if (TheServer)
		{
			compatibilityServer = TheServer;
		}
		else
		{
			TheServer = compatibilityServer;
		}
	}
}
#endif

} // namespace

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
// Legacy compatibility storage for callers that still reference TheServer directly.
std::shared_ptr<Server> TheServer = nullptr;
#endif

std::shared_ptr<Server> getActiveServer()
{
	if (runtimeServerStorage())
	{
#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
		syncActiveServerStorageWithLegacy();
#endif
		return runtimeServerStorage();
	}

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
	syncActiveServerStorageWithLegacy();
#endif
	return compatibilityServerStorage();
}

Server* getActiveServerPtr() noexcept
{
	if (runtimeServerStorage())
	{
#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
		syncActiveServerStorageWithLegacy();
#endif
		return runtimeServerStorage().get();
	}

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
	syncActiveServerStorageWithLegacy();
#endif
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
#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
	if (!runtimeServerStorage())
	{
		TheServer = compatibilityServerStorage();
	}
#endif
}

void setActiveServerForRuntime(std::shared_ptr<Server> server) noexcept
{
	runtimeServerStorage() = std::move(server);
#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
	TheServer = runtimeServerStorage() ? runtimeServerStorage() : compatibilityServerStorage();
#endif
}

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
#if defined(__GNUC__) && defined(__clang__)
	#pragma clang diagnostic pop
#endif
#if defined(__GNUC__) && !defined(__clang__)
	#pragma GCC diagnostic pop
#endif
#endif

} // namespace bzp
