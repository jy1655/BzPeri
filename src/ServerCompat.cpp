// Compatibility storage and accessors for the legacy global server handle.

#include <bzp/Server.h>

namespace bzp {

namespace {

std::shared_ptr<Server>& activeServerStorage() noexcept
{
	static std::shared_ptr<Server> activeServer;
	return activeServer;
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
	auto &activeServer = activeServerStorage();
	if (TheServer.get() != activeServer.get())
	{
		if (TheServer)
		{
			activeServer = TheServer;
		}
		else
		{
			TheServer = activeServer;
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
#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
	syncActiveServerStorageWithLegacy();
#endif
	return activeServerStorage();
}

Server* getActiveServerPtr() noexcept
{
#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
	syncActiveServerStorageWithLegacy();
#endif
	return activeServerStorage().get();
}

void setActiveServer(std::shared_ptr<Server> server)
{
	activeServerStorage() = std::move(server);
#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
	TheServer = activeServerStorage();
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
