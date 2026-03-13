// Legacy compatibility storage for the deprecated global server handle.

#include <bzp/Server.h>
#include "ServerCompat.h"

namespace bzp {

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
#if defined(__GNUC__) && defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#if defined(__GNUC__) && !defined(__clang__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

std::shared_ptr<Server> TheServer = nullptr;

std::shared_ptr<Server> getLegacyServerMirror() noexcept
{
	return TheServer;
}

void setLegacyServerMirror(std::shared_ptr<Server> server) noexcept
{
	TheServer = std::move(server);
}

#if defined(__GNUC__) && defined(__clang__)
	#pragma clang diagnostic pop
#endif
#if defined(__GNUC__) && !defined(__clang__)
	#pragma GCC diagnostic pop
#endif
#endif

} // namespace bzp
