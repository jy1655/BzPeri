// Copyright 2017-2019 Paul Nettle
//
// This file is part of BzPeri.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// COMPATIBILITY HEADER FOR LEGACY CODE
//
// This header provides backward compatibility for existing code that includes Gobbledegook.h
// New applications should use BzPeri.h directly.
//
// To migrate:
// 1. Change #include "Gobbledegook.h" to #include "BzPeri.h"
// 2. Update function names from ggk* to bzp*
// 3. Update type names from GGK* to BZP*
//
// This compatibility header will be removed in a future version.
//
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include "BzPeri.h"

#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

	// Legacy type aliases for backward compatibility
	typedef BZPLogReceiver GGKLogReceiver;
	typedef BZPServerDataGetter GGKServerDataGetter;
	typedef BZPServerDataSetter GGKServerDataSetter;
	typedef enum BZPServerRunState GGKServerRunState;
	typedef enum BZPServerHealth GGKServerHealth;

	// Legacy function wrappers for backward compatibility
	// All ggk* functions are deprecated. Use bzp* equivalents. Gobbledegook.h will be removed in v2.0
	[[deprecated("Use bzpLogRegisterDebug(). Gobbledegook.h will be removed in v2.0")]]
	static inline void ggkLogRegisterDebug(GGKLogReceiver receiver) { bzpLogRegisterDebug(receiver); }
	[[deprecated("Use bzpLogRegisterInfo(). Gobbledegook.h will be removed in v2.0")]]
	static inline void ggkLogRegisterInfo(GGKLogReceiver receiver) { bzpLogRegisterInfo(receiver); }
	[[deprecated("Use bzpLogRegisterStatus(). Gobbledegook.h will be removed in v2.0")]]
	static inline void ggkLogRegisterStatus(GGKLogReceiver receiver) { bzpLogRegisterStatus(receiver); }
	[[deprecated("Use bzpLogRegisterWarn(). Gobbledegook.h will be removed in v2.0")]]
	static inline void ggkLogRegisterWarn(GGKLogReceiver receiver) { bzpLogRegisterWarn(receiver); }
	[[deprecated("Use bzpLogRegisterError(). Gobbledegook.h will be removed in v2.0")]]
	static inline void ggkLogRegisterError(GGKLogReceiver receiver) { bzpLogRegisterError(receiver); }
	[[deprecated("Use bzpLogRegisterFatal(). Gobbledegook.h will be removed in v2.0")]]
	static inline void ggkLogRegisterFatal(GGKLogReceiver receiver) { bzpLogRegisterFatal(receiver); }
	[[deprecated("Use bzpLogRegisterAlways(). Gobbledegook.h will be removed in v2.0")]]
	static inline void ggkLogRegisterAlways(GGKLogReceiver receiver) { bzpLogRegisterAlways(receiver); }
	[[deprecated("Use bzpLogRegisterTrace(). Gobbledegook.h will be removed in v2.0")]]
	static inline void ggkLogRegisterTrace(GGKLogReceiver receiver) { bzpLogRegisterTrace(receiver); }

	[[deprecated("Use bzpNotifyUpdatedCharacteristic(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkNofifyUpdatedCharacteristic(const char *pObjectPath) { return bzpNofifyUpdatedCharacteristic(pObjectPath); }
	[[deprecated("Use bzpNotifyUpdatedDescriptor(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkNofifyUpdatedDescriptor(const char *pObjectPath) { return bzpNofifyUpdatedDescriptor(pObjectPath); }
	[[deprecated("Use bzpPushUpdateQueue(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkPushUpdateQueue(const char *pObjectPath, const char *pInterfaceName) { return bzpPushUpdateQueue(pObjectPath, pInterfaceName); }
	[[deprecated("Use bzpPopUpdateQueue(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkPopUpdateQueue(char *pElement, int elementLen, int keep) { return bzpPopUpdateQueue(pElement, elementLen, keep); }
	[[deprecated("Use bzpUpdateQueueIsEmpty(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkUpdateQueueIsEmpty() { return bzpUpdateQueueIsEmpty(); }
	[[deprecated("Use bzpUpdateQueueSize(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkUpdateQueueSize() { return bzpUpdateQueueSize(); }
	[[deprecated("Use bzpUpdateQueueClear(). Gobbledegook.h will be removed in v2.0")]]
	static inline void ggkUpdateQueueClear() { bzpUpdateQueueClear(); }

	[[deprecated("Use bzpStart(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkStart(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		GGKServerDataGetter getter, GGKServerDataSetter setter, int maxAsyncInitTimeoutMS) {
		return bzpStart(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, maxAsyncInitTimeoutMS);
	}

	[[deprecated("Use bzpStartWithBondable(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkStartWithBondable(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		GGKServerDataGetter getter, GGKServerDataSetter setter, int maxAsyncInitTimeoutMS, int enableBondable) {
		return bzpStartWithBondable(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, maxAsyncInitTimeoutMS, enableBondable);
	}

	[[deprecated("Use bzpWait(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkWait() { return bzpWait(); }
	[[deprecated("Use bzpTriggerShutdown(). Gobbledegook.h will be removed in v2.0")]]
	static inline void ggkTriggerShutdown() { bzpTriggerShutdown(); }
	[[deprecated("Use bzpShutdownAndWait(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkShutdownAndWait() { return bzpShutdownAndWait(); }

	[[deprecated("Use bzpGetServerRunState(). Gobbledegook.h will be removed in v2.0")]]
	static inline enum GGKServerRunState ggkGetServerRunState() { return (GGKServerRunState)bzpGetServerRunState(); }
	[[deprecated("Use bzpGetServerRunStateString(). Gobbledegook.h will be removed in v2.0")]]
	static inline const char *ggkGetServerRunStateString(enum GGKServerRunState state) { return bzpGetServerRunStateString((BZPServerRunState)state); }
	[[deprecated("Use bzpIsServerRunning(). Gobbledegook.h will be removed in v2.0")]]
	static inline int ggkIsServerRunning() { return bzpIsServerRunning(); }

	[[deprecated("Use bzpGetServerHealth(). Gobbledegook.h will be removed in v2.0")]]
	static inline enum GGKServerHealth ggkGetServerHealth() { return (GGKServerHealth)bzpGetServerHealth(); }
	[[deprecated("Use bzpGetServerHealthString(). Gobbledegook.h will be removed in v2.0")]]
	static inline const char *ggkGetServerHealthString(enum GGKServerHealth state) { return bzpGetServerHealthString((BZPServerHealth)state); }

#ifdef __cplusplus
}
#endif //__cplusplus