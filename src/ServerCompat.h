#pragma once

#include <memory>

namespace bzp {

class Server;

void setActiveServerForRuntime(std::shared_ptr<Server> server) noexcept;
std::shared_ptr<Server> getRuntimeServer();
Server* getRuntimeServerPtr() noexcept;

} // namespace bzp
