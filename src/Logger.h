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
// This is our Logger, which allows for applications to use their own logging mechanisms by registering log receivers for each of
// the logging categories.
//
// >>
// >>>  DISCUSSION
// >>
//
// See the discussion at the top of Logger.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <sstream>
#include <string_view>
#include <functional>
#include <concepts>
#include <memory>

#include "../include/BzPeri.h"
#include <bzp/FormatCompat.h>

namespace bzp {

// Modern C++20 log level concept
template<typename T>
concept LogLevel = std::same_as<T, const char*> || std::same_as<T, std::string_view> || std::same_as<T, std::string>;

// Our handy stringstream macro (maintained for compatibility)
#define SSTR std::ostringstream().flush()

// Modern C++20 format-based logging macros
#define LOG_DEBUG_F(...) Logger::debug(bzp::safeFormat(__VA_ARGS__))
#define LOG_INFO_F(...) Logger::info(bzp::safeFormat(__VA_ARGS__))
#define LOG_WARN_F(...) Logger::warn(bzp::safeFormat(__VA_ARGS__))
#define LOG_ERROR_F(...) Logger::error(bzp::safeFormat(__VA_ARGS__))

class Logger
{
public:

	//
	// Registration
	//

	// Register logging receiver for DEBUG logging.  To register a logging level, simply call with a delegate that performs the
	// appropriate logging action. To unregister, call with `nullptr`
	static void registerDebugReceiver(BZPLogReceiver receiver);

	// Register logging receiver for INFO logging.  To register a logging level, simply call with a delegate that performs the
	// appropriate logging action. To unregister, call with `nullptr`
	static void registerInfoReceiver(BZPLogReceiver receiver);

	// Register logging receiver for STATUS logging.  To register a logging level, simply call with a delegate that performs the
	// appropriate logging action. To unregister, call with `nullptr`
	static void registerStatusReceiver(BZPLogReceiver receiver);

	// Register logging receiver for WARN logging.  To register a logging level, simply call with a delegate that performs the
	// appropriate logging action. To unregister, call with `nullptr`
	static void registerWarnReceiver(BZPLogReceiver receiver);

	// Register logging receiver for ERROR logging.  To register a logging level, simply call with a delegate that performs the
	// appropriate logging action. To unregister, call with `nullptr`
	static void registerErrorReceiver(BZPLogReceiver receiver);

	// Register logging receiver for FATAL logging.  To register a logging level, simply call with a delegate that performs the
	// appropriate logging action. To unregister, call with `nullptr`
	static void registerFatalReceiver(BZPLogReceiver receiver);

	// Register logging receiver for ALWAYS logging.  To register a logging level, simply call with a delegate that performs the
	// appropriate logging action. To unregister, call with `nullptr`
	static void registerAlwaysReceiver(BZPLogReceiver receiver);

	// Register logging receiver for TRACE logging.  To register a logging level, simply call with a delegate that performs the
	// appropriate logging action. To unregister, call with `nullptr`
	static void registerTraceReceiver(BZPLogReceiver receiver);


	//
	// Logging actions
	//

	// Log a DEBUG entry with a C string
	static void debug(const char *pText);

	// Log a DEBUG entry with a string
	static void debug(const std::string &text);

	// Log a DEBUG entry using a stream
	static void debug(const std::ostream &text);

	// Log a INFO entry with a C string
	static void info(const char *pText);

	// Log a INFO entry with a string
	static void info(const std::string &text);

	// Log a INFO entry using a stream
	static void info(const std::ostream &text);

	// Log a STATUS entry with a C string
	static void status(const char *pText);

	// Log a STATUS entry with a string
	static void status(const std::string &text);

	// Log a STATUS entry using a stream
	static void status(const std::ostream &text);

	// Log a WARN entry with a C string
	static void warn(const char *pText);

	// Log a WARN entry with a string
	static void warn(const std::string &text);

	// Log a WARN entry using a stream
	static void warn(const std::ostream &text);

	// Log a ERROR entry with a C string
	static void error(const char *pText);

	// Log a ERROR entry with a string
	static void error(const std::string &text);

	// Log a ERROR entry using a stream
	static void error(const std::ostream &text);

	// Log a FATAL entry with a C string
	static void fatal(const char *pText);

	// Log a FATAL entry with a string
	static void fatal(const std::string &text);

	// Log a FATAL entry using a stream
	static void fatal(const std::ostream &text);

	// Log a ALWAYS entry with a C string
	static void always(const char *pText);

	// Log a ALWAYS entry with a string
	static void always(const std::string &text);

	// Log a ALWAYS entry using a stream
	static void always(const std::ostream &text);

	// Log a TRACE entry with a C string
	static void trace(const char *pText);

	// Log a TRACE entry with a string
	static void trace(const std::string &text);

	// Log a TRACE entry using a stream
	static void trace(const std::ostream &text);

private:

	// The registered log receiver for DEBUG logs - a nullptr will cause the logging for that receiver to be ignored
	static BZPLogReceiver logReceiverDebug;

	// The registered log receiver for INFO logs - a nullptr will cause the logging for that receiver to be ignored
	static BZPLogReceiver logReceiverInfo;

	// The registered log receiver for STATUS logs - a nullptr will cause the logging for that receiver to be ignored
	static BZPLogReceiver logReceiverStatus;

	// The registered log receiver for WARN logs - a nullptr will cause the logging for that receiver to be ignored
	static BZPLogReceiver logReceiverWarn;

	// The registered log receiver for ERROR logs - a nullptr will cause the logging for that receiver to be ignored
	static BZPLogReceiver logReceiverError;

	// The registered log receiver for FATAL logs - a nullptr will cause the logging for that receiver to be ignored
	static BZPLogReceiver logReceiverFatal;

	// The registered log receiver for ALWAYS logs - a nullptr will cause the logging for that receiver to be ignored
	static BZPLogReceiver logReceiverAlways;

	// The registered log receiver for TRACE logs - a nullptr will cause the logging for that receiver to be ignored
	static BZPLogReceiver logReceiverTrace;

public:
	//
	// Modern C++20 logging methods with concepts and perfect forwarding
	//

	// Generic logging with string_view (most efficient)
	template<LogLevel T>
	static void debug(T&& message) noexcept
	{
		if constexpr (std::same_as<std::decay_t<T>, std::string_view>) {
			debug(std::string{message}.c_str());
		} else {
			debug(std::forward<T>(message));
		}
	}

	template<LogLevel T>
	static void info(T&& message) noexcept
	{
		if constexpr (std::same_as<std::decay_t<T>, std::string_view>) {
			info(std::string{message}.c_str());
		} else {
			info(std::forward<T>(message));
		}
	}

	template<LogLevel T>
	static void warn(T&& message) noexcept
	{
		if constexpr (std::same_as<std::decay_t<T>, std::string_view>) {
			warn(std::string{message}.c_str());
		} else {
			warn(std::forward<T>(message));
		}
	}

	template<LogLevel T>
	static void error(T&& message) noexcept
	{
		if constexpr (std::same_as<std::decay_t<T>, std::string_view>) {
			error(std::string{message}.c_str());
		} else {
			error(std::forward<T>(message));
		}
	}

	// Structured logging with context (modern C++20 approach)
	struct LogContext
	{
		std::string_view component;
		std::string_view function;
		int line = 0;

		constexpr LogContext(std::string_view comp = {}, std::string_view func = {}, int ln = 0) noexcept
			: component(comp), function(func), line(ln) {}
	};

	template<LogLevel T>
	static void debugWithContext(T&& message, const LogContext& ctx = {}) noexcept
	{
		if (!ctx.component.empty()) {
			debug(safeFormat("[{}] {}", ctx.component, std::forward<T>(message)));
		} else {
			debug(std::forward<T>(message));
		}
	}

	template<LogLevel T>
	static void infoWithContext(T&& message, const LogContext& ctx = {}) noexcept
	{
		if (!ctx.component.empty()) {
			info(safeFormat("[{}] {}", ctx.component, std::forward<T>(message)));
		} else {
			info(std::forward<T>(message));
		}
	}
};

}; // namespace bzp