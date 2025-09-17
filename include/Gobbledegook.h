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
	static inline void ggkLogRegisterDebug(GGKLogReceiver receiver) { bzpLogRegisterDebug(receiver); }
	static inline void ggkLogRegisterInfo(GGKLogReceiver receiver) { bzpLogRegisterInfo(receiver); }
	static inline void ggkLogRegisterStatus(GGKLogReceiver receiver) { bzpLogRegisterStatus(receiver); }
	static inline void ggkLogRegisterWarn(GGKLogReceiver receiver) { bzpLogRegisterWarn(receiver); }
	static inline void ggkLogRegisterError(GGKLogReceiver receiver) { bzpLogRegisterError(receiver); }
	static inline void ggkLogRegisterFatal(GGKLogReceiver receiver) { bzpLogRegisterFatal(receiver); }
	static inline void ggkLogRegisterAlways(GGKLogReceiver receiver) { bzpLogRegisterAlways(receiver); }
	static inline void ggkLogRegisterTrace(GGKLogReceiver receiver) { bzpLogRegisterTrace(receiver); }

	static inline int ggkNofifyUpdatedCharacteristic(const char *pObjectPath) { return bzpNofifyUpdatedCharacteristic(pObjectPath); }
	static inline int ggkNofifyUpdatedDescriptor(const char *pObjectPath) { return bzpNofifyUpdatedDescriptor(pObjectPath); }
	static inline int ggkPushUpdateQueue(const char *pObjectPath, const char *pInterfaceName) { return bzpPushUpdateQueue(pObjectPath, pInterfaceName); }
	static inline int ggkPopUpdateQueue(char *pElement, int elementLen, int keep) { return bzpPopUpdateQueue(pElement, elementLen, keep); }
	static inline int ggkUpdateQueueIsEmpty() { return bzpUpdateQueueIsEmpty(); }
	static inline int ggkUpdateQueueSize() { return bzpUpdateQueueSize(); }
	static inline void ggkUpdateQueueClear() { bzpUpdateQueueClear(); }

	static inline int ggkStart(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		GGKServerDataGetter getter, GGKServerDataSetter setter, int maxAsyncInitTimeoutMS) {
		return bzpStart(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, maxAsyncInitTimeoutMS);
	}

	static inline int ggkStartWithBondable(const char *pServiceName, const char *pAdvertisingName, const char *pAdvertisingShortName,
		GGKServerDataGetter getter, GGKServerDataSetter setter, int maxAsyncInitTimeoutMS, int enableBondable) {
		return bzpStartWithBondable(pServiceName, pAdvertisingName, pAdvertisingShortName, getter, setter, maxAsyncInitTimeoutMS, enableBondable);
	}

	static inline int ggkWait() { return bzpWait(); }
	static inline void ggkTriggerShutdown() { bzpTriggerShutdown(); }
	static inline int ggkShutdownAndWait() { return bzpShutdownAndWait(); }

	static inline enum GGKServerRunState ggkGetServerRunState() { return (GGKServerRunState)bzpGetServerRunState(); }
	static inline const char *ggkGetServerRunStateString(enum GGKServerRunState state) { return bzpGetServerRunStateString((BZPServerRunState)state); }
	static inline int ggkIsServerRunning() { return bzpIsServerRunning(); }

	static inline enum GGKServerHealth ggkGetServerHealth() { return (GGKServerHealth)bzpGetServerHealth(); }
	static inline const char *ggkGetServerHealthString(enum GGKServerHealth state) { return bzpGetServerHealthString((BZPServerHealth)state); }

#ifdef __cplusplus
}
#endif //__cplusplus