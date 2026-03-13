// Compatibility storage and accessors for the legacy global server handle.

#include <bzp/Server.h>

namespace bzp {

#if defined(__GNUC__) && defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#if defined(__GNUC__) && !defined(__clang__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace {

std::shared_ptr<Server>& activeServerStorage() noexcept
{
	static std::shared_ptr<Server> activeServer;
	return activeServer;
}

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

} // namespace

// Legacy compatibility storage for callers that still reference TheServer directly.
std::shared_ptr<Server> TheServer = nullptr;

std::shared_ptr<Server> getActiveServer()
{
	syncActiveServerStorageWithLegacy();
	return activeServerStorage();
}

Server* getActiveServerPtr() noexcept
{
	syncActiveServerStorageWithLegacy();
	return activeServerStorage().get();
}

void setActiveServer(std::shared_ptr<Server> server)
{
	activeServerStorage() = std::move(server);
	TheServer = activeServerStorage();
}

#if defined(__GNUC__) && defined(__clang__)
	#pragma clang diagnostic pop
#endif
#if defined(__GNUC__) && !defined(__clang__)
	#pragma GCC diagnostic pop
#endif

} // namespace bzp
