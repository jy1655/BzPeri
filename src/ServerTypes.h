// Copyright 2017-2019 Paul Nettle
//
// This file is part of Gobbledegook.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

#pragma once

#include "../include/BzPeri.h"

namespace bzp {

// Modern C++ enums for internal use
enum class ServerRunState : int
{
    Uninitialized = EUninitialized,
    Initializing = EInitializing,
    Running = ERunning,
    Stopping = EStopping,
    Stopped = EStopped
};

enum class ServerHealth : int
{
    Ok = EOk,
    FailedInit = EFailedInit,
    FailedRun = EFailedRun
};

// Safe conversion functions
[[nodiscard]] constexpr BZPServerRunState toC(ServerRunState state) noexcept
{
    return static_cast<BZPServerRunState>(state);
}

[[nodiscard]] constexpr ServerRunState fromC(BZPServerRunState state) noexcept
{
    return static_cast<ServerRunState>(state);
}

[[nodiscard]] constexpr BZPServerHealth toC(ServerHealth health) noexcept
{
    return static_cast<BZPServerHealth>(health);
}

[[nodiscard]] constexpr ServerHealth fromC(BZPServerHealth health) noexcept
{
    return static_cast<ServerHealth>(health);
}

// Safe string conversion with bounds checking
[[nodiscard]] const char* serverRunStateToString(ServerRunState state) noexcept;
[[nodiscard]] const char* serverHealthToString(ServerHealth health) noexcept;

}; // namespace bzp