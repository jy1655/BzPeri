// Copyright 2017-2019 Paul Nettle
//
// This file is part of Gobbledegook.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

#include "ServerTypes.h"

namespace ggk {

const char* serverRunStateToString(ServerRunState state) noexcept
{
    switch(state)
    {
        case ServerRunState::Uninitialized: return "Uninitialized";
        case ServerRunState::Initializing: return "Initializing";
        case ServerRunState::Running: return "Running";
        case ServerRunState::Stopping: return "Stopping";
        case ServerRunState::Stopped: return "Stopped";
        default: return "Unknown";
    }
}

const char* serverHealthToString(ServerHealth health) noexcept
{
    switch(health)
    {
        case ServerHealth::Ok: return "Ok";
        case ServerHealth::FailedInit: return "Failed initialization";
        case ServerHealth::FailedRun: return "Failed run";
        default: return "Unknown";
    }
}

}; // namespace ggk