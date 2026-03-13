// Legacy compatibility support for the deprecated BluezAdapter singleton API.

#include <bzp/BluezAdapter.h>

namespace bzp {

#if BZP_ENABLE_LEGACY_SINGLETON_COMPAT
BluezAdapter& BluezAdapter::getInstance()
{
	return getActiveBluezAdapter();
}
#endif

} // namespace bzp
