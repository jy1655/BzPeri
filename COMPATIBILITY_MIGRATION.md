# Compatibility Migration Guide

This guide covers the deprecated compatibility layers that still exist in BzPeri for existing users.

It is intentionally separate from [BLUEZ_MIGRATION.md](/home/pi5test2/Developments/BzPeri/BLUEZ_MIGRATION.md), which focuses on the internal BlueZ implementation migration from HCI management commands to D-Bus.

## Scope

Use this guide if your application currently depends on one or more of the following:

- `#include "Gobbledegook.h"`
- `ggk*` C APIs
- deprecated `bzpNofify*` function names
- deprecated singleton/global access such as `TheServer` or `BluezAdapter::getInstance()`
- deprecated raw GLib callback or raw `GVariant *` extension points
- legacy `0/1` or `void` C APIs where detailed result codes are now available

## 1. Gobbledegook Header and `ggk*` APIs

The compatibility header [`Gobbledegook.h`](/home/pi5test2/Developments/BzPeri/include/Gobbledegook.h) is still shipped, but it is deprecated and intended only for transition.

Recommended migration:

1. Replace `#include "Gobbledegook.h"` with `#include <BzPeri.h>`.
2. Rename `ggk*` APIs to their `bzp*` equivalents.
3. Rename `GGK*` types to `BZP*`.

Examples:

```cpp
// Old
#include "Gobbledegook.h"
ggkStart("bzperi.device", "name", "short", getter, setter, 30000);

// New
#include <BzPeri.h>
bzpStart("bzperi.device", "name", "short", getter, setter, 30000);
```

## 2. Corrected `bzpNotify*` Names

Older releases exported misspelled update helpers:

- `bzpNofifyUpdatedCharacteristic()`
- `bzpNofifyUpdatedDescriptor()`

These remain available as deprecated compatibility shims. New code should use:

- `bzpNotifyUpdatedCharacteristic()`
- `bzpNotifyUpdatedDescriptor()`

If detailed failure reasons matter, prefer:

- `bzpNotifyUpdatedCharacteristicEx()`
- `bzpNotifyUpdatedDescriptorEx()`

## 3. Singleton and Global Compatibility

The old singleton/global access model is still available by default for compatibility:

- `TheServer`
- `BluezAdapter::getInstance()`

New code should avoid them and use:

- `getActiveServer()` / `getActiveServerPtr()`
- `getActiveBluezAdapter()` / `getActiveBluezAdapterPtr()`
- `getRuntimeServer()` / `getRuntimeServerPtr()`
- `getRuntimeBluezAdapterPtr()`

If you do not need the deprecated singleton/global layer at all, build with:

```bash
cmake -DENABLE_LEGACY_SINGLETON_COMPAT=OFF
```

With that option enabled, the deprecated singleton/global implementation units are omitted from the build.

## 4. Raw GLib Callback Compatibility

BzPeri now treats wrapper request objects as the canonical public extension surface:

- `DBusMethodCallRef`
- `DBusReplyRef`
- `DBusSignalRef`
- `DBusNotificationRef`
- `DBusUpdateRef`
- `DBusVariantRef`

Deprecated raw GLib callback and value paths are still available by default for older users, including:

- raw method callbacks
- raw property getter/setter registration
- raw signal/reply/notification helpers
- raw `GVariant *` convenience overloads
- raw `Utils::gvariantFrom*()` helpers

If you do not need that legacy surface, build with:

```bash
cmake -DENABLE_LEGACY_RAW_GLIB_COMPAT=OFF
```

That setting removes the deprecated raw GLib compatibility layer from the public API surface and leaves the wrapper request-object APIs as the supported extension path.

## 5. Legacy `0/1` and `void` APIs

Most older C helpers are still present as thin compatibility wrappers, but new code should prefer the newer `Ex` variants when failure reasons matter.

Common examples:

- startup:
  `bzpStartEx()`, `bzpStartWithBondableEx()`, `bzpStartManualEx()`
- wait/shutdown:
  `bzpWaitEx()`, `bzpWaitForStateEx()`, `bzpWaitForShutdownEx()`, `bzpShutdownAndWaitEx()`, `bzpTriggerShutdownEx()`
- update queue:
  `bzpNotifyUpdatedCharacteristicEx()`, `bzpNotifyUpdatedDescriptorEx()`, `bzpPushUpdateQueueEx()`, `bzpPopUpdateQueueEx()`, `bzpUpdateQueueClearEx()`
- GLib capture:
  `bzpSetGLibLogCaptureModeEx()`, `bzpSetGLibLogCaptureTargetsEx()`, `bzpSetGLibLogCaptureDomainsEx()`, `bzpInstallGLibLogCaptureEx()`, `bzpRestoreGLibLogCaptureEx()`
- manual run loop:
  `bzpRunLoopIterationEx()`, `bzpRunLoopAttachEx()`, `bzpRunLoopPollPrepareEx()`, `bzpRunLoopDriveUntilStateEx()`

## 6. Practical Migration Order

For an existing application, the least disruptive sequence is:

1. Move from `Gobbledegook.h` / `ggk*` to `BzPeri.h` / `bzp*`.
2. Replace `bzpNofify*` with `bzpNotify*`.
3. Stop using `TheServer` and `BluezAdapter::getInstance()`.
4. Move custom raw GLib callbacks to wrapper request objects.
5. Replace important `0/1` and `void` calls with `Ex` variants where diagnostics matter.
6. Once clean, build with both:

```bash
cmake -DENABLE_LEGACY_SINGLETON_COMPAT=OFF -DENABLE_LEGACY_RAW_GLIB_COMPAT=OFF
```

If that build succeeds, your application is no longer depending on the major compatibility shims.

## 7. Removal Outlook

The deprecated compatibility paths still exist to avoid breaking current users.

The intended long-term direction is:

- keep wrapper request-object APIs as the primary public surface
- keep compatibility layers opt-out in current releases
- remove deprecated layers only in a future major-version cleanup

For current releases, the supported way to prepare is to build and test with the compatibility options disabled.
