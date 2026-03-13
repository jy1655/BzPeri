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

#include "../BzPeri.h"
#include <bzp/FormatCompat.h>

namespace bzp {

#ifndef BZP_COMPILED_LOG_LEVEL_VALUE
#define BZP_COMPILED_LOG_LEVEL_VALUE 0
#endif

// Modern C++20 log level concept
template<typename T>
concept LogLevel = std::same_as<T, const char*> || std::same_as<T, std::string_view> || std::same_as<T, std::string>;

// Our handy stringstream macro (maintained for compatibility)
#define SSTR std::ostringstream().flush()

// Modern C++20 format-based logging macros that skip formatting when the level is disabled.
#define LOG_DEBUG_F(...) do { if (::bzp::Logger::isDebugCompiledIn() && ::bzp::Logger::isDebugEnabled()) ::bzp::Logger::debug(::bzp::safeFormat(__VA_ARGS__)); } while (0)
#define LOG_INFO_F(...) do { if (::bzp::Logger::isInfoCompiledIn() && ::bzp::Logger::isInfoEnabled()) ::bzp::Logger::info(::bzp::safeFormat(__VA_ARGS__)); } while (0)
#define LOG_WARN_F(...) do { if (::bzp::Logger::isWarnCompiledIn() && ::bzp::Logger::isWarnEnabled()) ::bzp::Logger::warn(::bzp::safeFormat(__VA_ARGS__)); } while (0)
#define LOG_ERROR_F(...) do { if (::bzp::Logger::isErrorCompiledIn() && ::bzp::Logger::isErrorEnabled()) ::bzp::Logger::error(::bzp::safeFormat(__VA_ARGS__)); } while (0)
#define LOG_STATUS_F(...) do { if (::bzp::Logger::isStatusCompiledIn() && ::bzp::Logger::isStatusEnabled()) ::bzp::Logger::status(::bzp::safeFormat(__VA_ARGS__)); } while (0)
#define LOG_TRACE_F(...) do { if (::bzp::Logger::isTraceCompiledIn() && ::bzp::Logger::isTraceEnabled()) ::bzp::Logger::trace(::bzp::safeFormat(__VA_ARGS__)); } while (0)

// Stream-based helpers for hot paths that still use SSTR-style logging.
#define LOG_DEBUG_STREAM(expr) do { if (::bzp::Logger::isDebugCompiledIn() && ::bzp::Logger::isDebugEnabled()) ::bzp::Logger::debug((expr)); } while (0)
#define LOG_INFO_STREAM(expr) do { if (::bzp::Logger::isInfoCompiledIn() && ::bzp::Logger::isInfoEnabled()) ::bzp::Logger::info((expr)); } while (0)
#define LOG_WARN_STREAM(expr) do { if (::bzp::Logger::isWarnCompiledIn() && ::bzp::Logger::isWarnEnabled()) ::bzp::Logger::warn((expr)); } while (0)
#define LOG_ERROR_STREAM(expr) do { if (::bzp::Logger::isErrorCompiledIn() && ::bzp::Logger::isErrorEnabled()) ::bzp::Logger::error((expr)); } while (0)
#define LOG_STATUS_STREAM(expr) do { if (::bzp::Logger::isStatusCompiledIn() && ::bzp::Logger::isStatusEnabled()) ::bzp::Logger::status((expr)); } while (0)
#define LOG_TRACE_STREAM(expr) do { if (::bzp::Logger::isTraceCompiledIn() && ::bzp::Logger::isTraceEnabled()) ::bzp::Logger::trace((expr)); } while (0)

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

	static constexpr int compiledLogLevelValue() noexcept { return BZP_COMPILED_LOG_LEVEL_VALUE; }
	static constexpr bool isTraceCompiledIn() noexcept { return compiledLogLevelValue() <= 0; }
	static constexpr bool isDebugCompiledIn() noexcept { return compiledLogLevelValue() <= 1; }
	static constexpr bool isInfoCompiledIn() noexcept { return compiledLogLevelValue() <= 2; }
	static constexpr bool isStatusCompiledIn() noexcept { return compiledLogLevelValue() <= 3; }
	static constexpr bool isWarnCompiledIn() noexcept { return compiledLogLevelValue() <= 4; }
	static constexpr bool isErrorCompiledIn() noexcept { return compiledLogLevelValue() <= 5; }
	static constexpr bool isFatalCompiledIn() noexcept { return compiledLogLevelValue() <= 6; }
	static constexpr bool isAlwaysCompiledIn() noexcept { return compiledLogLevelValue() <= 7; }

	static bool isDebugEnabled() noexcept { return logReceiverDebug != nullptr; }
	static bool isInfoEnabled() noexcept { return logReceiverInfo != nullptr; }
	static bool isStatusEnabled() noexcept { return logReceiverStatus != nullptr; }
	static bool isWarnEnabled() noexcept { return logReceiverWarn != nullptr; }
	static bool isErrorEnabled() noexcept { return logReceiverError != nullptr; }
	static bool isFatalEnabled() noexcept { return logReceiverFatal != nullptr; }
	static bool isAlwaysEnabled() noexcept { return logReceiverAlways != nullptr; }
	static bool isTraceEnabled() noexcept { return logReceiverTrace != nullptr; }


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
		if constexpr (isDebugCompiledIn()) {
			if (!ctx.component.empty()) {
				debug(safeFormat("[{}] {}", ctx.component, std::forward<T>(message)));
			} else {
				debug(std::forward<T>(message));
			}
		}
	}

	template<LogLevel T>
	static void infoWithContext(T&& message, const LogContext& ctx = {}) noexcept
	{
		if constexpr (isInfoCompiledIn()) {
			if (!ctx.component.empty()) {
				info(safeFormat("[{}] {}", ctx.component, std::forward<T>(message)));
			} else {
				info(std::forward<T>(message));
			}
		}
	}
};

}; // namespace bzp
