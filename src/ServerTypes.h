// Copyright 2017-2019 Paul Nettle
//
// This file is part of Gobbledegook.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

#pragma once

#include "../include/Gobbledegook.h"

namespace ggk {

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
[[nodiscard]] constexpr GGKServerRunState toC(ServerRunState state) noexcept
{
    return static_cast<GGKServerRunState>(state);
}

[[nodiscard]] constexpr ServerRunState fromC(GGKServerRunState state) noexcept
{
    return static_cast<ServerRunState>(state);
}

[[nodiscard]] constexpr GGKServerHealth toC(ServerHealth health) noexcept
{
    return static_cast<GGKServerHealth>(health);
}

[[nodiscard]] constexpr ServerHealth fromC(GGKServerHealth health) noexcept
{
    return static_cast<ServerHealth>(health);
}

// Safe string conversion with bounds checking
[[nodiscard]] const char* serverRunStateToString(ServerRunState state) noexcept;
[[nodiscard]] const char* serverHealthToString(ServerHealth health) noexcept;

}; // namespace ggk