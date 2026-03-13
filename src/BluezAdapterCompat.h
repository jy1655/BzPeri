#pragma once

#include <memory>

namespace bzp {

class BluezAdapter;

BluezAdapter* makeBluezAdapterForRuntime();
void destroyBluezAdapterForRuntime(BluezAdapter *adapter) noexcept;
using RuntimeBluezAdapterPtr = std::unique_ptr<BluezAdapter, void(*)(BluezAdapter*)>;
inline RuntimeBluezAdapterPtr makeRuntimeBluezAdapterPtr()
{
	return RuntimeBluezAdapterPtr(makeBluezAdapterForRuntime(), destroyBluezAdapterForRuntime);
}
void setActiveBluezAdapterForRuntime(BluezAdapter *adapter) noexcept;

} // namespace bzp
