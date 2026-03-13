#pragma once

#include <memory>

namespace bzp {

class Server;

void setActiveServerForRuntime(std::shared_ptr<Server> server) noexcept;
std::shared_ptr<Server> getRuntimeServer();
Server* getRuntimeServerPtr() noexcept;

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
std::shared_ptr<Server> getLegacyServerMirror() noexcept;
void setLegacyServerMirror(std::shared_ptr<Server> server) noexcept;
#else
inline std::shared_ptr<Server> getLegacyServerMirror() noexcept
{
	return nullptr;
}

inline void setLegacyServerMirror(std::shared_ptr<Server>) noexcept
{
}
#endif

} // namespace bzp
