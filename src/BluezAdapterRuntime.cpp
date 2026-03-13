// Runtime-owned BlueZ adapter storage and accessors.

#include <bzp/BluezAdapter.h>
#include "BluezAdvertisement.h"
#include "BluezAdapterCompat.h"

namespace bzp {

namespace {

BluezAdapter*& activeAdapterPtrStorage() noexcept
{
	static BluezAdapter *activeAdapter = nullptr;
	return activeAdapter;
}

} // namespace

BluezAdapter& BluezAdapter::activeAdapterStorage() noexcept
{
	static BluezAdapter instance;
	return instance;
}

BluezAdapter* makeBluezAdapterForRuntime()
{
	return new BluezAdapter();
}

void destroyBluezAdapterForRuntime(BluezAdapter *adapter) noexcept
{
	delete adapter;
}

void setActiveBluezAdapterForRuntime(BluezAdapter *adapter) noexcept
{
	activeAdapterPtrStorage() = adapter;
}

BluezAdapter& getActiveBluezAdapter() noexcept
{
	if (auto *activeAdapter = activeAdapterPtrStorage(); activeAdapter != nullptr)
	{
		return *activeAdapter;
	}

	return BluezAdapter::activeAdapterStorage();
}

BluezAdapter* getActiveBluezAdapterPtr() noexcept
{
	if (auto *activeAdapter = activeAdapterPtrStorage(); activeAdapter != nullptr)
	{
		return activeAdapter;
	}

	return &BluezAdapter::activeAdapterStorage();
}

BluezAdapter* getRuntimeBluezAdapterPtr() noexcept
{
	return activeAdapterPtrStorage();
}

} // namespace bzp
