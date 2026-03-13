// Compatibility storage and accessors for the legacy BluezAdapter singleton API.

#include <bzp/BluezAdapter.h>
#include "BluezAdvertisement.h"

namespace bzp {

BluezAdapter& BluezAdapter::activeAdapterStorage() noexcept
{
	static BluezAdapter instance;
	return instance;
}

BluezAdapter& getActiveBluezAdapter() noexcept
{
	return BluezAdapter::activeAdapterStorage();
}

BluezAdapter* getActiveBluezAdapterPtr() noexcept
{
	return &BluezAdapter::activeAdapterStorage();
}

BluezAdapter& BluezAdapter::getInstance()
{
	return getActiveBluezAdapter();
}

} // namespace bzp
