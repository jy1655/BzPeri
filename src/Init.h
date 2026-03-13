// Copyright 2017-2019 Paul Nettle
//
// This file is part of Gobbledegook.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file in the root of the source tree.

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  INSIDE THIS FILE
// >>
//
// Herein lies the code that manages the full initialization (including the running) of the server
//
// >>
// >>>  DISCUSSION
// >>
//
// See the discussion at the top of Init.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <BzPeri.h>

namespace bzp {

struct Server;
class BluezAdapter;

// Trigger a graceful, asynchronous shutdown of the server
//
// This method is non-blocking and as such, will only trigger the shutdown process but not wait for it
void shutdown();

// Entry point for the asynchronous server thread
//
// This method should not be called directly, instead, direct your attention over to `bzpStart()`
void runServerThread(Server *serverContext, BluezAdapter *adapterContext);

// Initialize the dedicated GLib runtime without creating the internal server thread.
//
// After this succeeds, the caller must drive the runtime by repeatedly calling
// `runServerLoopIteration()`.
bool startServerLoopManually(Server *serverContext, BluezAdapter *adapterContext);

// Run one iteration of the manually-owned GLib runtime.
//
// Returns non-zero if work was dispatched or shutdown cleanup completed.
int runServerLoopIteration(int mayBlock);
int runServerLoopIterationFor(int timeoutMS);
bool attachServerLoopToCurrentThread();
bool detachServerLoopFromCurrentThread();
bool isManualServerLoopMode();
bool hasServerLoopOwner();
bool isCurrentThreadServerLoopOwner();
bool prepareServerLoopPoll(int *timeoutMS, int *requiredFDCount, int *dispatchReady);
bool queryServerLoopPoll(BZPPollFD *pollFDs, int pollFDCount, int *requiredFDCount);
bool checkServerLoopPoll(const BZPPollFD *pollFDs, int pollFDCount);
int dispatchServerLoopPoll();
bool cancelServerLoopPoll();

// Queue a callback to execute on the dedicated GLib runtime.
bool invokeOnServerLoop(void (*callback)(void *), void *userData);

}; // namespace bzp
