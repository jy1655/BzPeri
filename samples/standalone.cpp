// Copyright 2017-2019 Paul Nettle
// Copyright (c) 2025 BzPeri Contributors
//
// This file is part of BzPeri.
//
// Licensed under MIT License (see LICENSE file)

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// >>
// >>>  INSIDE THIS FILE
// >>
//
// This is an example single-file stand-alone application that runs a BzPeri server.
//
// >>
// >>>  DISCUSSION
// >>
//
// Very little is required ("MUST") by a stand-alone application to instantiate a valid BzPeri server. There are also some
// things that are recommended ("SHOULD").
//
// * A stand-alone application MUST:
//
//     * Start the server via a call to `bzpStart()`.
//
//         Once started the server will run on its own thread. In the `v0.2.x` line, a stand-alone application may also choose
//         the manual run-loop path (`bzpStartManual*()` plus `bzpRunLoopIteration*()`) when it wants to own the event loop.
//
//         Two of the parameters to `bzpStart()` are delegates responsible for providing data accessors for the server, a
//         `BZPServerDataGetter` delegate and a 'BZPServerDataSetter' delegate. The getter method simply receives a string name (for
//         example, "battery/level") and returns a void pointer to that data (for example: `(void *)&batteryLevel`). The setter does
//         the same only in reverse.
//
//         While the server is running, you will likely need to update the data being served. This is done by calling
//         `bzpNotifyUpdatedCharacteristic()` or `bzpNotifyUpdatedDescriptor()` with the full path to the characteristic or delegate
//         whose data has been updated. This will trigger your server's `onUpdatedValue()` method, which can perform whatever
//         actions are needed such as sending out a change notification (or in BlueZ parlance, a "PropertiesChanged" signal.)
//
// * A stand-alone application SHOULD:
//
//     * Shutdown the server before termination
//
//         Triggering the server to begin shutting down is done via a call to `bzpTriggerShutdown()`. This is a non-blocking method
//         that begins the asynchronous shutdown process.
//
//         Before your application terminates, it should wait for the server to be completely stopped. This is done via a call to
//         `bzpWait()` or the bounded form `bzpWaitForShutdown(timeoutMS)`. If the server has not yet reached the `EStopped` state
//         when `bzpWait()` is called, it will block until the server has done so.
//
//         To avoid the blocking behavior of `bzpWait()`, use `bzpWaitForShutdown(timeoutMS)` or wait for
//         `bzpGetServerRunState() == EStopped` first. Even if the server has stopped, it is recommended to call `bzpWait()` (or
//         `bzpWaitForShutdown(0)`) to ensure the server has cleaned up all threads and other internals.
//
//         If you want to keep things simple, there is a method `bzpShutdownAndWait()` which will trigger the shutdown and then
//         block until the server has stopped.
//
//     * Implement signal handling to provide a clean shut-down
//
//         This is done by calling `bzpTriggerShutdown()` from any signal received that can terminate your application. For an
//         example of this, search for all occurrences of the string "signalHandler" in the code below.
//
//     * Register a custom logging mechanism with the server
//
//         This is done by calling each of the log registeration methods:
//
//             `bzpLogRegisterDebug()`
//             `bzpLogRegisterInfo()`
//             `bzpLogRegisterStatus()`
//             `bzpLogRegisterWarn()`
//             `bzpLogRegisterError()`
//             `bzpLogRegisterFatal()`
//             `bzpLogRegisterAlways()`
//             `bzpLogRegisterTrace()`
//
//         Each registration method manages a different log level. For a full description of these levels, see the header comment
//         in Logger.cpp.
//
//         The code below includes a simple logging mechanism that logs to stdout and filters logs based on a few command-line
//         options to specify the level of verbosity.
//
// >>
// >>>  Building with BZPERI
// >>
//
// The BzPeri distribution includes this file as part of the BzPeri files with everything compiling to a single stand-alone
// binary. It remains a useful integration sample even though BzPeri now also ships as a reusable library/package.
// The sample intentionally exercises the public C API surface, logging controls, and runtime options that downstream users are
// most likely to need when validating a new release such as `v0.2.0`.
//
// If it is important to you or your build process that BzPeri exist as a library, you are welcome to do so. Just configure
// your build process to build the BzPeri files (minus this file) as a library and link against that instead. All that is
// required by applications linking to a BzPeri library is to include `include/BzPeri.h`.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <signal.h>
#include <cerrno>
#include <unistd.h>
#include <gio/gio.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <thread>

#include "../include/BzPeri.h"
#include "../include/BzPeriConfigurator.h"
#include "../include/bzp/BluezAdapter.h"
#include "../include/bzp/DBusInterface.h"
#include "../include/bzp/GattInterface.h"
#include "../include/bzp/Server.h"
#include "SampleServices.h"
#include "../src/StandaloneWorkflow.h"

//
// Constants
//

// Maximum time to wait for any single async process to timeout during initialization
static const int kMaxAsyncInitTimeoutMS = 30 * 1000;

static const char *describeCompiledLogLevel(BZPCompiledLogLevel level)
{
	switch (level)
	{
	case BZP_COMPILED_LOG_LEVEL_TRACE:
		return "trace";
	case BZP_COMPILED_LOG_LEVEL_DEBUG:
		return "debug";
	case BZP_COMPILED_LOG_LEVEL_INFO:
		return "info";
	case BZP_COMPILED_LOG_LEVEL_STATUS:
		return "status";
	case BZP_COMPILED_LOG_LEVEL_WARN:
		return "warn";
	case BZP_COMPILED_LOG_LEVEL_ERROR:
		return "error";
	case BZP_COMPILED_LOG_LEVEL_FATAL:
		return "fatal";
	case BZP_COMPILED_LOG_LEVEL_ALWAYS:
		return "always";
	default:
		return "unknown";
	}
}

static std::string describeGLibCaptureTargets(unsigned int targets)
{
	if (targets == BZP_GLIB_LOG_CAPTURE_TARGET_ALL)
	{
		return "all";
	}

	std::vector<std::string> parts;
	if ((targets & BZP_GLIB_LOG_CAPTURE_TARGET_LOG) != 0)
	{
		parts.emplace_back("log");
	}
	if ((targets & BZP_GLIB_LOG_CAPTURE_TARGET_PRINT) != 0)
	{
		parts.emplace_back("print");
	}
	if ((targets & BZP_GLIB_LOG_CAPTURE_TARGET_PRINTERR) != 0)
	{
		parts.emplace_back("printerr");
	}

	std::ostringstream stream;
	for (size_t i = 0; i < parts.size(); ++i)
	{
		if (i != 0)
		{
			stream << ',';
		}
		stream << parts[i];
	}
	return stream.str();
}

static std::string describeGLibCaptureDomains(unsigned int domains)
{
	if (domains == BZP_GLIB_LOG_CAPTURE_DOMAIN_ALL)
	{
		return "all";
	}

	std::vector<std::string> parts;
	if ((domains & BZP_GLIB_LOG_CAPTURE_DOMAIN_DEFAULT) != 0)
	{
		parts.emplace_back("default");
	}
	if ((domains & BZP_GLIB_LOG_CAPTURE_DOMAIN_GLIB) != 0)
	{
		parts.emplace_back("glib");
	}
	if ((domains & BZP_GLIB_LOG_CAPTURE_DOMAIN_GIO) != 0)
	{
		parts.emplace_back("gio");
	}
	if ((domains & BZP_GLIB_LOG_CAPTURE_DOMAIN_BLUEZ) != 0)
	{
		parts.emplace_back("bluez");
	}
	if ((domains & BZP_GLIB_LOG_CAPTURE_DOMAIN_OTHER) != 0)
	{
		parts.emplace_back("other");
	}

	std::ostringstream stream;
	for (size_t i = 0; i < parts.size(); ++i)
	{
		if (i != 0)
		{
			stream << ',';
		}
		stream << parts[i];
	}
	return stream.str();
}

static bool parseGLibCaptureTargets(const std::string &value, unsigned int *targetsOut)
{
	if (!targetsOut)
	{
		return false;
	}

	std::string normalized = value;
	std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	if (normalized == "all")
	{
		*targetsOut = BZP_GLIB_LOG_CAPTURE_TARGET_ALL;
		return true;
	}

	unsigned int targets = 0;
	std::stringstream stream(normalized);
	std::string part;
	while (std::getline(stream, part, ','))
	{
		if (part == "log")
		{
			targets |= BZP_GLIB_LOG_CAPTURE_TARGET_LOG;
		}
		else if (part == "print")
		{
			targets |= BZP_GLIB_LOG_CAPTURE_TARGET_PRINT;
		}
		else if (part == "printerr")
		{
			targets |= BZP_GLIB_LOG_CAPTURE_TARGET_PRINTERR;
		}
		else if (!part.empty())
		{
			return false;
		}
	}

	if (targets == 0)
	{
		return false;
	}

	*targetsOut = targets;
	return true;
}

static bool parseGLibCaptureDomains(const std::string &value, unsigned int *domainsOut)
{
	if (!domainsOut)
	{
		return false;
	}

	std::string normalized = value;
	std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	if (normalized == "all")
	{
		*domainsOut = BZP_GLIB_LOG_CAPTURE_DOMAIN_ALL;
		return true;
	}

	unsigned int domains = 0;
	std::stringstream stream(normalized);
	std::string part;
	while (std::getline(stream, part, ','))
	{
		if (part == "default")
		{
			domains |= BZP_GLIB_LOG_CAPTURE_DOMAIN_DEFAULT;
		}
		else if (part == "glib")
		{
			domains |= BZP_GLIB_LOG_CAPTURE_DOMAIN_GLIB;
		}
		else if (part == "gio")
		{
			domains |= BZP_GLIB_LOG_CAPTURE_DOMAIN_GIO;
		}
		else if (part == "bluez")
		{
			domains |= BZP_GLIB_LOG_CAPTURE_DOMAIN_BLUEZ;
		}
		else if (part == "other")
		{
			domains |= BZP_GLIB_LOG_CAPTURE_DOMAIN_OTHER;
		}
		else if (!part.empty())
		{
			return false;
		}
	}

	if (domains == 0)
	{
		return false;
	}

	*domainsOut = domains;
	return true;
}

//
// Server data values
//

// The battery level ("battery/level") reported by the server (see Server.cpp)
static uint8_t serverDataBatteryLevel = 78;

// The text string ("text/string") used by our custom text string service (see Server.cpp)
static std::string serverDataTextString = "Hello, world!";

// Cached D-Bus path for the sample battery characteristic
static std::string batteryLevelObjectPath;
static std::string textStringObjectPath;

// Signal state is shared with the sample's main loop and must stay async-signal-safe.
static volatile sig_atomic_t pendingShutdownSignal = 0;

// Terminal-first inspect state is only active while a managed demo session is running.
static std::unique_ptr<bzp::standalone::InspectSessionStore> inspectSessionStore;
static std::unique_ptr<bzp::standalone::InspectSessionSnapshot> inspectSessionSnapshot;
static std::recursive_mutex inspectSessionMutex;

//
// Logging
//

enum LogLevel
{
	Debug,
	Verbose,
	Normal,
	ErrorsOnly
};

// Our log level - defaulted to 'Normal' but can be modified via command-line options
LogLevel logLevel = Normal;

namespace {
void LogDebug(const char *pText);
void LogInfo(const char *pText);
void LogStatus(const char *pText);
void LogWarn(const char *pText);
void LogError(const char *pText);
void LogFatal(const char *pText);
void LogAlways(const char *pText);
void LogTrace(const char *pText);
void recordInspectSemanticEvent(bzp::standalone::EventLevel level, const std::string &component, const std::string &message, bool refreshSnapshot = false);
}

//
// Signal handling
//

// We setup a couple Unix signals to request graceful shutdown in the case of SIGTERM or SIGINT (CTRL-C).
void signalHandler(int signum)
{
	pendingShutdownSignal = signum;
}

void installSignalHandler(int signum)
{
	struct sigaction action = {};
	action.sa_handler = signalHandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	sigaction(signum, &action, nullptr);
}

//
// Server data management
//

// Called by the server when it wants to retrieve a named value
//
// This method conforms to `BZPServerDataGetter` and is passed to the server via our call to `bzpStart()`.
//
// The server calls this method from its own thread, so we must ensure our implementation is thread-safe. In our case, we're simply
// sending over stored values, so we don't need to take any additional steps to ensure thread-safety.
const void *dataGetter(const char *pName)
{
	if (nullptr == pName)
	{
		LogError("NULL name sent to server data getter");
		return nullptr;
	}

	std::string strName = pName;

	if (strName == "battery/level")
	{
		recordInspectSemanticEvent(
			bzp::standalone::EventLevel::Status,
			"gatt",
			std::string("read path=") + (batteryLevelObjectPath.empty() ? "/com/.../battery/level" : batteryLevelObjectPath)
				+ " value=" + std::to_string(serverDataBatteryLevel) + "%");
		return &serverDataBatteryLevel;
	}
	else if (strName == "text/string")
	{
		recordInspectSemanticEvent(
			bzp::standalone::EventLevel::Status,
			"gatt",
			std::string("read path=") + (textStringObjectPath.empty() ? "/com/.../text/string" : textStringObjectPath)
				+ " value='" + serverDataTextString + "'");
		return serverDataTextString.c_str();
	}

	LogWarn((std::string("Unknown name for server data getter request: '") + pName + "'").c_str());
	return nullptr;
}

// Called by the server when it wants to update a named value
//
// This method conforms to `BZPServerDataSetter` and is passed to the server via our call to `bzpStart()`.
//
// The server calls this method from its own thread, so we must ensure our implementation is thread-safe. In our case, we're simply
// sending over stored values, so we don't need to take any additional steps to ensure thread-safety.
int dataSetter(const char *pName, const void *pData)
{
	if (nullptr == pName)
	{
		LogError("NULL name sent to server data setter");
		return 0;
	}
	if (nullptr == pData)
	{
		LogError("NULL pData sent to server data setter");
		return 0;
	}

	std::string strName = pName;

	if (strName == "battery/level")
	{
		serverDataBatteryLevel = *static_cast<const uint8_t *>(pData);
		LogDebug((std::string("Server data: battery level set to ") + std::to_string(serverDataBatteryLevel)).c_str());
		recordInspectSemanticEvent(
			bzp::standalone::EventLevel::Status,
			"gatt",
			std::string("write path=") + (batteryLevelObjectPath.empty() ? "/com/.../battery/level" : batteryLevelObjectPath)
				+ " value=" + std::to_string(serverDataBatteryLevel) + "%",
			true);
		return 1;
	}
	else if (strName == "text/string")
	{
		serverDataTextString = static_cast<const char *>(pData);
		LogDebug((std::string("Server data: text string set to '") + serverDataTextString + "'").c_str());
		recordInspectSemanticEvent(
			bzp::standalone::EventLevel::Status,
			"gatt",
			std::string("write path=") + (textStringObjectPath.empty() ? "/com/.../text/string" : textStringObjectPath)
				+ " value='" + serverDataTextString + "'",
			true);
		return 1;
	}

	LogWarn((std::string("Unknown name for server data setter request: '") + pName + "'").c_str());

	return 0;
}

//
// Entry point
//

namespace {

constexpr std::size_t kInspectEventLimit = 64;
constexpr int kDefaultInspectRefreshMs = 500;

enum class CommandMode
{
	TopLevelHelp,
	Demo,
	Doctor,
	Inspect,
};

struct CommonOptions
{
	LogLevel requestedLogLevel = Normal;
	std::string adapterName;
	bool listAdapters = false;
};

struct DemoOptions
{
	CommonOptions common;
	std::string serviceName = "bzperi";
	std::string advertisingName = "BzPeri";
	std::string advertisingShortName = "BzPeri";
	std::string sampleNamespace = "samples";
	bool includeSampleServices = true;
	bool manualLoopMode = false;
	BZPGLibLogCaptureMode glibCaptureMode = bzpGetConfiguredGLibLogCaptureMode();
	unsigned int glibCaptureTargets = bzpGetConfiguredGLibLogCaptureTargets();
	unsigned int glibCaptureDomains = bzpGetConfiguredGLibLogCaptureDomains();
	bool sleepIntegrationEnabled = bzpGetConfiguredPrepareForSleepIntegrationEnabled() != 0;
	bool sleepInhibitorEnabled = bzpGetConfiguredSleepInhibitorEnabled() != 0;
	bool installHostManagedCapture = false;
	bool showHelp = false;
};

struct DoctorOptions
{
	CommonOptions common;
	bool showHelp = false;
};

struct InspectOptions
{
	bool live = false;
	bool verboseEvents = false;
	bool showTree = false;
	int refreshMs = kDefaultInspectRefreshMs;
	bool showHelp = false;
};

struct CommandSelection
{
	CommandMode mode = CommandMode::Demo;
	std::size_t optionStartIndex = 1;
};

class RuntimeDoctorProbe final : public bzp::standalone::DoctorProbe
{
public:
	bool checkSystemBus(std::string *detailOut) const override
	{
		GError *error = nullptr;
		GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
		if (connection == nullptr)
		{
			if (detailOut != nullptr)
			{
				*detailOut = error != nullptr ? error->message : "Failed to connect to the system bus.";
			}
			if (error != nullptr)
			{
				g_error_free(error);
			}
			return false;
		}

		if (detailOut != nullptr)
		{
			*detailOut = "Connected to the system D-Bus.";
		}
		g_object_unref(connection);
		return true;
	}

	bool checkBluezService(std::string *detailOut) const override
	{
		GError *error = nullptr;
		GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
		if (connection == nullptr)
		{
			if (detailOut != nullptr)
			{
				*detailOut = error != nullptr ? error->message : "Failed to connect to the system bus.";
			}
			if (error != nullptr)
			{
				g_error_free(error);
			}
			return false;
		}

		GVariant *reply = g_dbus_connection_call_sync(
			connection,
			"org.freedesktop.DBus",
			"/org/freedesktop/DBus",
			"org.freedesktop.DBus",
			"NameHasOwner",
			g_variant_new("(s)", "org.bluez"),
			G_VARIANT_TYPE("(b)"),
			G_DBUS_CALL_FLAGS_NONE,
			3000,
			nullptr,
			&error);
		g_object_unref(connection);

		if (reply == nullptr)
		{
			if (detailOut != nullptr)
			{
				*detailOut = error != nullptr ? error->message : "Unable to query org.bluez ownership.";
			}
			if (error != nullptr)
			{
				g_error_free(error);
			}
			return false;
		}

		gboolean hasOwner = FALSE;
		g_variant_get(reply, "(b)", &hasOwner);
		g_variant_unref(reply);
		if (detailOut != nullptr)
		{
			*detailOut = hasOwner != FALSE
				? "org.bluez is present on the system bus."
				: "org.bluez is not currently owned on the system bus.";
		}
		return hasOwner != FALSE;
	}

	bool checkServiceOwnership(const std::string &ownedName, std::string *detailOut) const override
	{
		GError *error = nullptr;
		GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
		if (connection == nullptr)
		{
			if (detailOut != nullptr)
			{
				*detailOut = error != nullptr ? error->message : "Failed to connect to the system bus.";
			}
			if (error != nullptr)
			{
				g_error_free(error);
			}
			return false;
		}

		GVariant *reply = g_dbus_connection_call_sync(
			connection,
			"org.freedesktop.DBus",
			"/org/freedesktop/DBus",
			"org.freedesktop.DBus",
			"RequestName",
			g_variant_new("(su)", ownedName.c_str(), 4U),
			G_VARIANT_TYPE("(u)"),
			G_DBUS_CALL_FLAGS_NONE,
			3000,
			nullptr,
			&error);
		if (reply == nullptr)
		{
			if (detailOut != nullptr)
			{
				const bool looksLikePermissionIssue = error != nullptr
					&& (std::string(error->message).find("AccessDenied") != std::string::npos
						|| std::string(error->message).find("not allowed") != std::string::npos
						|| std::string(error->message).find("denied") != std::string::npos);
				if (looksLikePermissionIssue)
				{
					*detailOut = "Current user cannot own " + ownedName + " on the system bus. Re-run with sudo or install a custom D-Bus policy.";
				}
				else
				{
					*detailOut = error != nullptr ? error->message : ("Failed to request ownership for " + ownedName + ".");
				}
			}
			if (error != nullptr)
			{
				g_error_free(error);
			}
			g_object_unref(connection);
			return false;
		}

		guint resultCode = 0;
		g_variant_get(reply, "(u)", &resultCode);
		g_variant_unref(reply);

		if (resultCode == 1U || resultCode == 4U)
		{
			GVariant *releaseReply = g_dbus_connection_call_sync(
				connection,
				"org.freedesktop.DBus",
				"/org/freedesktop/DBus",
				"org.freedesktop.DBus",
				"ReleaseName",
				g_variant_new("(s)", ownedName.c_str()),
				G_VARIANT_TYPE("(u)"),
				G_DBUS_CALL_FLAGS_NONE,
				3000,
				nullptr,
				nullptr);
			if (releaseReply != nullptr)
			{
				g_variant_unref(releaseReply);
			}
			g_object_unref(connection);
			if (detailOut != nullptr)
			{
				*detailOut = "Current process can own " + ownedName + " on the system bus.";
			}
			return true;
		}

		g_object_unref(connection);
		if (detailOut != nullptr)
		{
			if (resultCode == 3U)
			{
				*detailOut = ownedName + " is already owned by another process.";
			}
			else
			{
				*detailOut = "RequestName for " + ownedName + " returned unexpected result code " + std::to_string(resultCode) + ".";
			}
		}
		return false;
	}

	bool probeAdapter(const std::string &preferredAdapter,
		bool *poweredAdapterAvailableOut,
		std::string *summaryOut,
		std::vector<std::string> *adapterLinesOut,
		std::string *detailOut) const override
	{
		if (poweredAdapterAvailableOut != nullptr)
		{
			*poweredAdapterAvailableOut = false;
		}

		auto &adapter = bzp::getActiveBluezAdapter();
		adapter.setServiceNameContext("bzperi");
		const auto initializeResult = adapter.initialize(preferredAdapter);
		if (!initializeResult.isSuccess())
		{
			if (detailOut != nullptr)
			{
				*detailOut = initializeResult.errorMessage().empty()
					? bzp::bluezErrorToString(initializeResult.error())
					: initializeResult.errorMessage();
			}
			adapter.shutdown();
			return false;
		}

		const auto adaptersResult = adapter.discoverAdapters();
		if (!adaptersResult.isSuccess())
		{
			if (detailOut != nullptr)
			{
				*detailOut = adaptersResult.errorMessage();
			}
			adapter.shutdown();
			return false;
		}

		std::ostringstream summary;
		bool first = true;
		bool poweredAdapterAvailable = false;
		for (const auto &adapterInfo : adaptersResult.value())
		{
			if (adapterLinesOut != nullptr)
			{
				adapterLinesOut->push_back(
					adapterInfo.path + " [" + adapterInfo.address + "] powered=" + (adapterInfo.powered ? "yes" : "no"));
			}
			if (!first)
			{
				summary << "; ";
			}
			first = false;
			summary << adapterInfo.path << " powered=" << (adapterInfo.powered ? "yes" : "no");
			poweredAdapterAvailable = poweredAdapterAvailable || adapterInfo.powered;
		}

		if (summaryOut != nullptr)
		{
			*summaryOut = summary.str();
		}
		if (poweredAdapterAvailableOut != nullptr)
		{
			*poweredAdapterAvailableOut = poweredAdapterAvailable;
		}
		if (detailOut != nullptr)
		{
			*detailOut = poweredAdapterAvailable
				? summary.str()
				: ("Adapters found but none are powered: " + summary.str());
		}

		adapter.shutdown();
		return true;
	}

	bool checkPolicy(std::string *pathOut) const override
	{
		const std::vector<std::string> policyCandidates = {
			"/usr/share/dbus-1/system.d/com.bzperi.conf",
			"/etc/dbus-1/system.d/com.bzperi.conf",
		};
		for (const auto &candidate : policyCandidates)
		{
			if (std::filesystem::exists(candidate))
			{
				if (pathOut != nullptr)
				{
					*pathOut = candidate;
				}
				return true;
			}
		}

		if (pathOut != nullptr)
		{
			*pathOut = "/usr/share/dbus-1/system.d/com.bzperi.conf";
		}
		return false;
	}

	bool checkExperimentalHelper(std::string *pathOut, bool *modeEnabledOut, std::string *detailOut) const override
	{
		const auto runningProcessHasExperimental = []() {
			const int processCheck = std::system("ps -eo args= | grep '[b]luetoothd' | grep -q -- '--experimental'");
			return processCheck == 0;
		};

		const std::vector<std::string> helperCandidates = {
			"/usr/share/bzperi/configure-bluez-experimental.sh",
			"scripts/configure-bluez-experimental.sh",
		};
		for (const auto &candidate : helperCandidates)
		{
			if (std::filesystem::exists(candidate))
			{
				if (pathOut != nullptr)
				{
					*pathOut = candidate;
				}
				if (modeEnabledOut != nullptr)
				{
					*modeEnabledOut = false;
				}

				const std::string command = "bash " + candidate + " check >/dev/null 2>&1";
				const int result = std::system(command.c_str());
				const bool helperEnabled = result == 0;
				const bool runningEnabled = !helperEnabled && runningProcessHasExperimental();
				const bool enabled = helperEnabled || runningEnabled;
				if (modeEnabledOut != nullptr)
				{
					*modeEnabledOut = enabled;
				}
				if (detailOut != nullptr)
				{
					if (helperEnabled)
					{
						*detailOut = "Experimental mode is enabled (" + candidate + " check succeeded).";
					}
					else if (runningEnabled)
					{
						*detailOut = "Experimental mode is enabled in the running bluetoothd process, even though " + candidate + " check failed.";
					}
					else
					{
						*detailOut = "Experimental mode is not enabled (" + candidate + " check failed).";
					}
				}
				return true;
			}
		}

		const bool runningEnabled = runningProcessHasExperimental();
		if (modeEnabledOut != nullptr)
		{
			*modeEnabledOut = runningEnabled;
		}
		if (detailOut != nullptr)
		{
			*detailOut = runningEnabled
				? "BlueZ experimental helper was not found, but the running bluetoothd process includes --experimental."
				: "BlueZ experimental helper was not found in the installed or source-build locations.";
		}
		return runningEnabled;
	}
};

std::string executableName(const char *argv0)
{
	const std::filesystem::path argvPath(argv0 != nullptr ? argv0 : "bzp-standalone");
	return argvPath.filename().string();
}

std::string toLowerCopy(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	return value;
}

std::string stripStructuredPrefix(const std::string &message, std::string *componentOut)
{
	if (!message.empty() && message.front() == '[')
	{
		const auto closeBracket = message.find(']');
		if (closeBracket != std::string::npos)
		{
			if (componentOut != nullptr)
			{
				*componentOut = message.substr(1, closeBracket - 1);
			}
			if (closeBracket + 2 <= message.size() && message[closeBracket + 1] == ' ')
			{
				return message.substr(closeBracket + 2);
			}
			return message.substr(closeBracket + 1);
		}
	}

	if (componentOut != nullptr)
	{
		componentOut->clear();
	}
	return message;
}

void persistInspectSession()
{
	std::lock_guard<std::recursive_mutex> lock(inspectSessionMutex);
	if (!inspectSessionStore || !inspectSessionSnapshot)
	{
		return;
	}

	std::string error;
	if (!inspectSessionStore->save(*inspectSessionSnapshot, &error))
	{
		std::cerr << "WARNING: failed to persist inspect session: " << error << std::endl;
	}
}

bool isProcessAlive(int pid) noexcept
{
	if (pid <= 0)
	{
		return false;
	}

	if (kill(pid, 0) == 0)
	{
		return true;
	}

	return errno == EPERM;
}

std::string formatVariantValue(GVariant *variant)
{
	if (variant == nullptr)
	{
		return "<null>";
	}

	gchar *printed = g_variant_print(variant, TRUE);
	std::string result = printed != nullptr ? printed : "<print-failed>";
	g_free(printed);
	return result;
}

std::string joinInterfaceNames(const bzp::DBusObject::InterfaceList &interfaces)
{
	std::ostringstream stream;
	bool first = true;
	for (const auto &interface : interfaces)
	{
		if (!first)
		{
			stream << ", ";
		}
		first = false;
		stream << interface->getName();
	}
	return stream.str();
}

int countObjectsRecursive(const bzp::DBusObject &object)
{
	int count = 1;
	for (const auto &child : object.getChildren())
	{
		count += countObjectsRecursive(child);
	}
	return count;
}

void appendObjectTreeLines(const bzp::DBusObject &object, int depth, std::vector<std::string> *lines)
{
	if (lines == nullptr)
	{
		return;
	}

	std::ostringstream line;
	line << std::string(static_cast<std::size_t>(depth) * 2U, ' ') << object.getPath().toString();
	if (!object.getInterfaces().empty())
	{
		line << " [" << joinInterfaceNames(object.getInterfaces()) << "]";
	}
	lines->push_back(line.str());

	for (const auto &child : object.getChildren())
	{
		appendObjectTreeLines(child, depth + 1, lines);
	}
}

bool appendSelectedObjectLines(const bzp::DBusObject &object, const std::string &selectedPath, bzp::standalone::InspectSessionSnapshot *snapshot)
{
	if (snapshot == nullptr)
	{
		return false;
	}

	if (object.getPath().toString() == selectedPath)
	{
		snapshot->selectedObjectSummary = "selected path: " + selectedPath;
		snapshot->selectedObjectLines.push_back("object path: " + selectedPath);
		snapshot->selectedObjectLines.push_back("interfaces: " + joinInterfaceNames(object.getInterfaces()));

		for (const auto &interface : object.getInterfaces())
		{
			snapshot->selectedObjectLines.push_back("interface: " + interface->getName());
			if (const auto gattInterface = std::dynamic_pointer_cast<const bzp::GattInterface>(interface))
			{
				std::string flagsValue;
				for (const auto &property : gattInterface->getProperties())
				{
					if (property.getName() == "UUID"
						|| property.getName() == "Flags"
						|| property.getName() == "Service"
						|| property.getName() == "Characteristic")
					{
						const auto value = formatVariantValue(property.getValueRef().get());
						snapshot->selectedObjectLines.push_back("  " + property.getName() + ": " + value);
						if (property.getName() == "Flags")
						{
							flagsValue = value;
						}
					}
				}
				if (flagsValue.find("notify") != std::string::npos)
				{
					snapshot->selectedObjectLines.push_back("  NotifyPath: " + selectedPath);
				}
			}
		}
		return true;
	}

	for (const auto &child : object.getChildren())
	{
		if (appendSelectedObjectLines(child, selectedPath, snapshot))
		{
			return true;
		}
	}
	return false;
}

std::optional<bzp::standalone::InspectSessionSnapshot> buildInspectSnapshot(const DemoOptions &options)
{
	const auto server = bzp::getActiveServer();
	if (!server)
	{
		return std::nullopt;
	}

	bzp::standalone::InspectSessionSnapshot snapshot;
	snapshot.pid = static_cast<int>(getpid());
	snapshot.updatedAtMs = bzp::standalone::currentTimeMs();
	snapshot.serviceName = server->getServiceName();
	snapshot.advertisingName = server->getAdvertisingName();
	snapshot.advertisingShortName = server->getAdvertisingShortName();
	snapshot.sampleNamespace = options.sampleNamespace;
	snapshot.includeSampleServices = options.includeSampleServices;
	snapshot.manualLoopMode = options.manualLoopMode;
	snapshot.runState = static_cast<int>(bzpGetServerRunState());
	snapshot.health = static_cast<int>(bzpGetServerHealth());
	snapshot.objectRoot = !server->getObjects().empty()
		? server->getObjects().begin()->getPath().toString()
		: server->getRootObject().getPath().toString();
	snapshot.selectedObjectPath = batteryLevelObjectPath.empty() ? snapshot.objectRoot : batteryLevelObjectPath;
	snapshot.writeProbePath = textStringObjectPath;

	for (const auto &object : server->getObjects())
	{
		snapshot.objectCount += countObjectsRecursive(object);
		appendObjectTreeLines(object, 0, &snapshot.objectTreeLines);
	}

	if (!appendSelectedObjectLines(server->getRootObject(), snapshot.selectedObjectPath, &snapshot))
	{
		snapshot.selectedObjectLines.push_back("No detailed object metadata captured for " + snapshot.selectedObjectPath);
	}

	if (!batteryLevelObjectPath.empty() && snapshot.selectedObjectPath == batteryLevelObjectPath)
	{
		snapshot.selectedObjectLines.push_back("sample value: " + std::to_string(serverDataBatteryLevel) + "%");
	}

	auto adapterInfo = bzp::getActiveBluezAdapter().getAdapterInfo();
	if (adapterInfo.isSuccess())
	{
		snapshot.adapterPath = adapterInfo.value().path;
		snapshot.adapterAddress = adapterInfo.value().address;
		snapshot.adapterAlias = adapterInfo.value().alias;
		snapshot.adapterPowered = adapterInfo.value().powered;
	}
	else
	{
		snapshot.warnings.push_back("Adapter metadata unavailable: " + adapterInfo.errorMessage());
	}

	snapshot.activeConnections = bzp::getActiveBluezAdapter().getActiveConnectionCount();
	snapshot.advertisingEnabled = bzp::getActiveBluezAdapter().isAdvertising();
	if (!snapshot.includeSampleServices)
	{
		snapshot.warnings.push_back("Bundled sample services are disabled; inspect output will mostly reflect the empty server root.");
	}

	return snapshot;
}

void refreshInspectSession(bool rebuildTree)
{
	std::lock_guard<std::recursive_mutex> lock(inspectSessionMutex);
	if (!inspectSessionSnapshot)
	{
		return;
	}

	const auto rebuilt = buildInspectSnapshot(DemoOptions{
		{},
		inspectSessionSnapshot->serviceName,
		inspectSessionSnapshot->advertisingName,
		inspectSessionSnapshot->advertisingShortName,
		inspectSessionSnapshot->sampleNamespace,
		inspectSessionSnapshot->includeSampleServices,
		inspectSessionSnapshot->manualLoopMode
	});
	if (!rebuilt)
	{
		return;
	}

	auto refreshed = *rebuilt;
	if (!rebuildTree)
	{
		refreshed.objectTreeLines = inspectSessionSnapshot->objectTreeLines;
	}
	refreshed.events = inspectSessionSnapshot->events;
	*inspectSessionSnapshot = std::move(refreshed);
	persistInspectSession();
}

void clearInspectSession()
{
	std::lock_guard<std::recursive_mutex> lock(inspectSessionMutex);
	if (inspectSessionStore)
	{
		std::string error;
		if (!inspectSessionStore->clear(&error) && !error.empty())
		{
			std::cerr << "WARNING: failed to clear inspect session: " << error << std::endl;
		}
	}
	inspectSessionSnapshot.reset();
	inspectSessionStore.reset();
}

void recordSessionEvent(bzp::standalone::EventLevel level, const char *text)
{
	std::lock_guard<std::recursive_mutex> lock(inspectSessionMutex);
	if (!inspectSessionStore || !inspectSessionSnapshot || text == nullptr)
	{
		return;
	}

	std::string component;
	const std::string rawMessage(text);
	const std::string message = stripStructuredPrefix(rawMessage, &component);
	bzp::standalone::appendInspectEvent(*inspectSessionSnapshot, {
		bzp::standalone::currentTimeMs(),
		level,
		component,
		message,
	}, inspectSessionStore->eventLimit());

	if (message.find("op=Connection") != std::string::npos
		|| message.find("op=Initialize") != std::string::npos
		|| message.find("shutting down") != std::string::npos)
	{
		refreshInspectSession(false);
		return;
	}

	persistInspectSession();
}

void recordInspectSemanticEvent(bzp::standalone::EventLevel level, const std::string &component, const std::string &message, bool refreshSnapshot)
{
	std::lock_guard<std::recursive_mutex> lock(inspectSessionMutex);
	if (!inspectSessionStore || !inspectSessionSnapshot || message.empty())
	{
		return;
	}

	bzp::standalone::appendInspectEvent(*inspectSessionSnapshot, {
		bzp::standalone::currentTimeMs(),
		level,
		component,
		message,
	}, inspectSessionStore->eventLimit());

	if (refreshSnapshot)
	{
		refreshInspectSession(false);
		return;
	}

	persistInspectSession();
}

void LogDebug(const char *pText) { recordSessionEvent(bzp::standalone::EventLevel::Debug, pText); if (logLevel <= Debug) { std::cout << "  DEBUG: " << pText << std::endl; } }
void LogInfo(const char *pText) { recordSessionEvent(bzp::standalone::EventLevel::Info, pText); if (logLevel <= Verbose) { std::cout << "   INFO: " << pText << std::endl; } }
void LogStatus(const char *pText) { recordSessionEvent(bzp::standalone::EventLevel::Status, pText); if (logLevel <= Normal) { std::cout << " STATUS: " << pText << std::endl; } }
void LogWarn(const char *pText) { recordSessionEvent(bzp::standalone::EventLevel::Warn, pText); std::cout << "WARNING: " << pText << std::endl; }
void LogError(const char *pText) { recordSessionEvent(bzp::standalone::EventLevel::Error, pText); std::cout << "!!ERROR: " << pText << std::endl; }
void LogFatal(const char *pText) { recordSessionEvent(bzp::standalone::EventLevel::Fatal, pText); std::cout << "**FATAL: " << pText << std::endl; }
void LogAlways(const char *pText) { recordSessionEvent(bzp::standalone::EventLevel::Status, pText); std::cout << "..Log..: " << pText << std::endl; }
void LogTrace(const char *pText)
{
	recordSessionEvent(bzp::standalone::EventLevel::Trace, pText);
	if (logLevel <= Debug)
	{
		std::cout << "-Trace-: " << pText << std::endl;
	}
}

void registerLoggers()
{
	bzpLogRegisterDebug(LogDebug);
	bzpLogRegisterInfo(LogInfo);
	bzpLogRegisterStatus(LogStatus);
	bzpLogRegisterWarn(LogWarn);
	bzpLogRegisterError(LogError);
	bzpLogRegisterFatal(LogFatal);
	bzpLogRegisterAlways(LogAlways);
	bzpLogRegisterTrace(LogTrace);
}

void applyCommonOptions(const CommonOptions &common)
{
	logLevel = common.requestedLogLevel;

	if (!common.adapterName.empty())
	{
		setenv("BLUEZ_ADAPTER", common.adapterName.c_str(), 1);
	}
	else
	{
		unsetenv("BLUEZ_ADAPTER");
	}

	if (common.listAdapters)
	{
		setenv("BLUEZ_LIST_ADAPTERS", "1", 1);
	}
	else
	{
		unsetenv("BLUEZ_LIST_ADAPTERS");
	}
}

std::string buildTopLevelHelp(const std::string &binaryName)
{
	std::ostringstream stream;
	stream << "Usage: " << binaryName << " <command> [options]\n";
	stream << "       " << binaryName << " [demo options]\n\n";
	stream << "Commands:\n";
	stream << "  doctor   Check the local host for the BzPeri happy path\n";
	stream << "  demo     Start the managed sample server\n";
	stream << "  inspect  Inspect a managed demo session (use --live)\n\n";
	stream << "Examples:\n";
	stream << "  " << binaryName << " doctor\n";
	stream << "  " << binaryName << " demo -d --adapter=hci0\n";
	stream << "  " << binaryName << " inspect --live\n\n";
	stream << "Legacy compatibility:\n";
	stream << "  " << binaryName << " --adapter=hci0 -d\n\n";
	stream << "Run '" << binaryName << " <command> --help' for command-specific options.\n";
	return stream.str();
}

std::string buildDemoHelp(const std::string &binaryName)
{
	std::ostringstream stream;
	stream << "Usage: " << binaryName << " demo [options]\n";
	stream << "       " << binaryName << " [options]\n\n";
	stream << "Logging options:\n";
	stream << "  -q                         Quiet mode (errors only)\n";
	stream << "  -v                         Verbose mode\n";
	stream << "  -d                         Debug mode\n\n";
	stream << "BlueZ options:\n";
	stream << "  --adapter=NAME             Use specific adapter (e.g. hci0)\n";
	stream << "  --list-adapters            List available adapters during startup\n\n";
	stream << "General options:\n";
	stream << "  --service-name=NAME        Set D-Bus service namespace (default bzperi)\n";
	stream << "  --advertise-name=NAME      Set LE advertising name (default BzPeri)\n";
	stream << "  --advertise-short=NAME     Set LE advertising short name (default BzPeri)\n";
	stream << "  --sample-namespace=NODE    Namespace node for example services (default samples)\n";
	stream << "  --manual-loop              Drive BzPeri via bzpRunLoopIteration()\n";
	stream << "  --sleep-integration=MODE   Enable or disable PrepareForSleep integration: on or off\n";
	stream << "  --sleep-inhibitor=MODE     Enable or disable sleep inhibitor support: on or off\n";
	stream << "  --glib-log-capture=MODE    Set GLib capture mode: auto, off, host, startup-shutdown\n";
	stream << "  --glib-log-targets=SET     Set GLib capture targets: all or log,print,printerr\n";
	stream << "  --glib-log-domains=SET     Set GLib capture domains: all or default,glib,gio,bluez,other\n";
	stream << "  --no-sample-services       Disable bundled example GATT services\n";
	stream << "  --with-sample-services     Re-enable bundled example services after disabling\n";
	return stream.str();
}

std::string buildDoctorHelp(const std::string &binaryName)
{
	std::ostringstream stream;
	stream << "Usage: " << binaryName << " doctor [options]\n\n";
	stream << "Options:\n";
	stream << "  -q                         Quiet mode (errors only)\n";
	stream << "  -v                         Verbose mode\n";
	stream << "  -d                         Debug mode\n";
	stream << "  --adapter=NAME             Probe a specific adapter\n";
	stream << "  --list-adapters            Include adapter listing during the probe\n";
	return stream.str();
}

std::string buildInspectHelp(const std::string &binaryName)
{
	std::ostringstream stream;
	stream << "Usage: " << binaryName << " inspect --live [options]\n\n";
	stream << "Options:\n";
	stream << "  --live                     Attach to the managed demo session and print updates\n";
	stream << "  --verbose-events           Show debug/info chatter in the event block\n";
	stream << "  --show-tree                Include the full object tree in the report\n";
	stream << "  --refresh-ms=INT           Poll the session file for updates (default " << kDefaultInspectRefreshMs << "ms)\n";
	return stream.str();
}

CommandSelection selectCommand(int argc, char **argv)
{
	if (argc <= 1)
	{
		return {CommandMode::Demo, 1};
	}

	const std::string firstArg = argv[1];
	if (firstArg == "--help" || firstArg == "-h")
	{
		return {CommandMode::TopLevelHelp, 2};
	}
	if (firstArg == "demo")
	{
		return {CommandMode::Demo, 2};
	}
	if (firstArg == "doctor")
	{
		return {CommandMode::Doctor, 2};
	}
	if (firstArg == "inspect")
	{
		return {CommandMode::Inspect, 2};
	}
	if (!firstArg.empty() && firstArg[0] != '-')
	{
		return {CommandMode::TopLevelHelp, 0};
	}
	return {CommandMode::Demo, 1};
}

bool parseCommonOption(const std::string &arg, CommonOptions *common)
{
	if (common == nullptr)
	{
		return false;
	}

	if (arg == "-q")
	{
		common->requestedLogLevel = ErrorsOnly;
		return true;
	}
	if (arg == "-v")
	{
		common->requestedLogLevel = Verbose;
		return true;
	}
	if (arg == "-d")
	{
		common->requestedLogLevel = Debug;
		return true;
	}
	if (arg.rfind("--adapter=", 0) == 0)
	{
		common->adapterName = arg.substr(10);
		return true;
	}
	if (arg == "--list-adapters")
	{
		common->listAdapters = true;
		return true;
	}
	return false;
}

bool parseDemoOptions(int argc, char **argv, std::size_t startIndex, DemoOptions *options, std::string *errorOut)
{
	for (std::size_t i = startIndex; i < static_cast<std::size_t>(argc); ++i)
	{
		const std::string arg = argv[i];
		if (parseCommonOption(arg, &options->common))
		{
			continue;
		}
		if (arg.rfind("--service-name=", 0) == 0)
		{
			options->serviceName = arg.substr(15);
		}
		else if (arg.rfind("--advertise-name=", 0) == 0)
		{
			options->advertisingName = arg.substr(17);
		}
		else if (arg.rfind("--advertise-short=", 0) == 0)
		{
			options->advertisingShortName = arg.substr(18);
		}
		else if (arg == "--no-sample-services")
		{
			options->includeSampleServices = false;
		}
		else if (arg == "--with-sample-services")
		{
			options->includeSampleServices = true;
		}
		else if (arg == "--manual-loop")
		{
			options->manualLoopMode = true;
		}
		else if (arg.rfind("--sleep-integration=", 0) == 0)
		{
			std::string mode = toLowerCopy(arg.substr(20));
			if (mode == "on" || mode == "enable" || mode == "enabled" || mode == "true")
			{
				options->sleepIntegrationEnabled = true;
			}
			else if (mode == "off" || mode == "disable" || mode == "disabled" || mode == "false")
			{
				options->sleepIntegrationEnabled = false;
			}
			else
			{
				*errorOut = "Unknown sleep integration mode: " + mode;
				return false;
			}
		}
		else if (arg.rfind("--sleep-inhibitor=", 0) == 0)
		{
			std::string mode = toLowerCopy(arg.substr(19));
			if (mode == "on" || mode == "enable" || mode == "enabled" || mode == "true")
			{
				options->sleepInhibitorEnabled = true;
			}
			else if (mode == "off" || mode == "disable" || mode == "disabled" || mode == "false")
			{
				options->sleepInhibitorEnabled = false;
			}
			else
			{
				*errorOut = "Unknown sleep inhibitor mode: " + mode;
				return false;
			}
		}
		else if (arg.rfind("--glib-log-capture=", 0) == 0)
		{
			const std::string mode = toLowerCopy(arg.substr(19));
			if (mode == "auto" || mode == "automatic")
			{
				options->glibCaptureMode = BZP_GLIB_LOG_CAPTURE_AUTOMATIC;
			}
			else if (mode == "off" || mode == "disabled" || mode == "disable")
			{
				options->glibCaptureMode = BZP_GLIB_LOG_CAPTURE_DISABLED;
			}
			else if (mode == "host" || mode == "host-managed")
			{
				options->glibCaptureMode = BZP_GLIB_LOG_CAPTURE_HOST_MANAGED;
				options->installHostManagedCapture = true;
			}
			else if (mode == "startup-shutdown" || mode == "transient")
			{
				options->glibCaptureMode = BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN;
			}
			else
			{
				*errorOut = "Unknown GLib log capture mode: " + mode;
				return false;
			}
		}
		else if (arg.rfind("--glib-log-targets=", 0) == 0)
		{
			unsigned int parsedTargets = 0;
			const std::string targetSpec = arg.substr(19);
			if (!parseGLibCaptureTargets(targetSpec, &parsedTargets))
			{
				*errorOut = "Unknown GLib log capture targets: " + targetSpec;
				return false;
			}
			options->glibCaptureTargets = parsedTargets;
		}
		else if (arg.rfind("--glib-log-domains=", 0) == 0)
		{
			unsigned int parsedDomains = 0;
			const std::string domainSpec = arg.substr(19);
			if (!parseGLibCaptureDomains(domainSpec, &parsedDomains))
			{
				*errorOut = "Unknown GLib log capture domains: " + domainSpec;
				return false;
			}
			options->glibCaptureDomains = parsedDomains;
		}
		else if (arg.rfind("--sample-namespace=", 0) == 0)
		{
			options->sampleNamespace = arg.substr(19);
		}
		else if (arg == "--help" || arg == "-h")
		{
			options->showHelp = true;
		}
		else
		{
			*errorOut = "Unknown parameter: " + arg;
			return false;
		}
	}
	return true;
}

bool parseDoctorOptions(int argc, char **argv, std::size_t startIndex, DoctorOptions *options, std::string *errorOut)
{
	for (std::size_t i = startIndex; i < static_cast<std::size_t>(argc); ++i)
	{
		const std::string arg = argv[i];
		if (parseCommonOption(arg, &options->common))
		{
			continue;
		}
		if (arg == "--help" || arg == "-h")
		{
			options->showHelp = true;
			continue;
		}
		*errorOut = "Unknown parameter: " + arg;
		return false;
	}
	return true;
}

bool parseInspectOptions(int argc, char **argv, std::size_t startIndex, InspectOptions *options, std::string *errorOut)
{
	for (std::size_t i = startIndex; i < static_cast<std::size_t>(argc); ++i)
	{
		const std::string arg = argv[i];
		if (arg == "--help" || arg == "-h")
		{
			options->showHelp = true;
		}
		else if (arg == "--live")
		{
			options->live = true;
		}
		else if (arg == "--verbose-events")
		{
			options->verboseEvents = true;
		}
		else if (arg == "--show-tree")
		{
			options->showTree = true;
		}
		else if (arg.rfind("--refresh-ms=", 0) == 0)
		{
			options->refreshMs = std::max(100, std::atoi(arg.substr(13).c_str()));
		}
		else
		{
			*errorOut = "Unknown parameter: " + arg;
			return false;
		}
	}

	if (!options->showHelp && !options->live)
	{
		*errorOut = "inspect currently requires --live";
		return false;
	}
	return true;
}

void printBlock(const std::string &text)
{
	std::cout << text;
	if (!text.empty() && text.back() != '\n')
	{
		std::cout << '\n';
	}
}

int validateDemoOptions(DemoOptions *options)
{
	options->serviceName = toLowerCopy(options->serviceName);
	options->sampleNamespace = toLowerCopy(options->sampleNamespace);

	if (options->serviceName.empty())
	{
		LogFatal("Service name cannot be empty");
		return -1;
	}
	if (options->serviceName != "bzperi" && options->serviceName.find("bzperi.") != 0)
	{
		LogFatal("Service name must be 'bzperi' or start with 'bzperi.' (e.g., 'bzperi.myapp')");
		LogFatal("This ensures D-Bus policy compatibility and prevents conflicts");
		return -1;
	}
	if (options->sampleNamespace.find('/') != std::string::npos)
	{
		LogFatal("Sample namespace must not contain '/' characters");
		return -1;
	}
	return 0;
}

void configureDemoServices(const DemoOptions &options)
{
	if (bzp::serviceConfiguratorCount() > 0)
	{
		LogWarn("Existing service configurators cleared for standalone configuration");
	}

	bzp::clearServiceConfigurators();

	if (options.includeSampleServices)
	{
		bzp::samples::registerSampleServices(options.sampleNamespace);
		std::string pathServiceName = options.serviceName;
		std::replace(pathServiceName.begin(), pathServiceName.end(), '.', '/');
		std::string pathBase = std::string("/com/") + pathServiceName;
		if (!options.sampleNamespace.empty())
		{
			pathBase += "/" + options.sampleNamespace;
		}
		batteryLevelObjectPath = pathBase + "/battery/level";
		textStringObjectPath = pathBase + "/text/string";
		LogStatus((std::string("Bundled example services registered under ") + pathBase).c_str());
	}
	else
	{
		batteryLevelObjectPath.clear();
		textStringObjectPath.clear();
		LogStatus("Bundled example services disabled; starting with empty server");
	}
}

bzp::standalone::DoctorSnapshot collectDoctorSnapshot(const DoctorOptions &options, const std::string &ownedName = "com.bzperi")
{
	RuntimeDoctorProbe probe;
	auto snapshot = bzp::standalone::collectDoctorSnapshot(probe, options.common.adapterName, ownedName);
	if (!options.common.listAdapters)
	{
		snapshot.adapterLines.clear();
	}
	return snapshot;
}

int runDoctor(const std::string &binaryName, const DoctorOptions &options)
{
	applyCommonOptions(options.common);
	registerLoggers();
	const auto snapshot = collectDoctorSnapshot(options);
	const auto report = bzp::standalone::evaluateDoctorSnapshot(snapshot, binaryName);
	printBlock(bzp::standalone::formatDoctorReport(report, binaryName));
	return bzp::standalone::exitCodeForDoctorReport(report);
}

int runInspect(const std::string &binaryName, const InspectOptions &options)
{
	bzp::standalone::InspectSessionStore store;
	std::string error;
	auto snapshot = store.load(&error);
	if (!snapshot)
	{
		if (!error.empty())
		{
			std::cerr << "ERROR: " << error << std::endl;
			return 1;
		}
		printBlock(bzp::standalone::formatMissingSessionReport(binaryName));
		return 1;
	}
	if (bzp::standalone::isInspectSessionStale(*snapshot, isProcessAlive))
	{
		store.clear();
		printBlock(bzp::standalone::formatStaleSessionReport(*snapshot, binaryName));
		return 1;
	}

	printBlock(bzp::standalone::formatInspectReport(*snapshot, binaryName, options.verboseEvents, options.showTree));
	long long lastUpdatedAt = snapshot->updatedAtMs;

	while (options.live)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(options.refreshMs));

		std::string refreshedError;
		auto refreshed = store.load(&refreshedError);
		if (!refreshed)
		{
			if (!refreshedError.empty())
			{
				std::cerr << "ERROR: " << refreshedError << std::endl;
				return 1;
			}
			std::cout << '\n';
			printBlock("BzPeri Inspect\nSTATUS  WARN\nManaged session ended.\n\nNEXT\nrun: " + binaryName + " demo\n");
			return 0;
		}
		if (bzp::standalone::isInspectSessionStale(*refreshed, isProcessAlive))
		{
			store.clear();
			std::cout << '\n';
			printBlock(bzp::standalone::formatStaleSessionReport(*refreshed, binaryName));
			return 1;
		}

		if (refreshed->updatedAtMs == lastUpdatedAt)
		{
			continue;
		}

		lastUpdatedAt = refreshed->updatedAtMs;
		std::cout << '\n';
		printBlock(bzp::standalone::formatInspectReport(*refreshed, binaryName, options.verboseEvents, options.showTree));
	}

	return 0;
}

int runDemo(const std::string &binaryName, const DemoOptions &options)
{
	applyCommonOptions(options.common);
	registerLoggers();
	clearInspectSession();

	DemoOptions validated = options;
	if (validateDemoOptions(&validated) != 0)
	{
		return -1;
	}

	configureDemoServices(validated);

	installSignalHandler(SIGINT);
	installSignalHandler(SIGTERM);
	pendingShutdownSignal = 0;

	bzpSetPrepareForSleepIntegrationEnabled(validated.sleepIntegrationEnabled ? 1 : 0);
	bzpSetSleepInhibitorEnabled(validated.sleepInhibitorEnabled ? 1 : 0);
	bzpSetGLibLogCaptureMode(validated.glibCaptureMode);
	bzpSetGLibLogCaptureTargets(validated.glibCaptureTargets);
	bzpSetGLibLogCaptureDomains(validated.glibCaptureDomains);

	bool hostManagedCaptureInstalled = false;
	if (validated.installHostManagedCapture)
	{
		hostManagedCaptureInstalled = bzpInstallGLibLogCapture() != 0;
		if (!hostManagedCaptureInstalled)
		{
			LogFatal("Failed to install host-managed GLib log capture");
			return -1;
		}
	}

	auto cleanupAndReturn = [&](int code) {
		clearInspectSession();
		if (hostManagedCaptureInstalled)
		{
			bzpRestoreGLibLogCapture();
		}
		return code;
	};

	LogStatus((std::string("Compiled log level: ") + describeCompiledLogLevel(bzpGetConfiguredCompiledLogLevel())).c_str());
	LogStatus((std::string("PrepareForSleep integration: ") + (validated.sleepIntegrationEnabled ? "enabled" : "disabled")).c_str());
	LogStatus((std::string("Sleep inhibitor integration: ") + (validated.sleepInhibitorEnabled ? "enabled" : "disabled")).c_str());
	LogStatus((std::string("GLib log capture targets: ") + describeGLibCaptureTargets(validated.glibCaptureTargets)).c_str());
	LogStatus((std::string("GLib log capture domains: ") + describeGLibCaptureDomains(validated.glibCaptureDomains)).c_str());

	const int started = validated.manualLoopMode
		? bzpStartWithBondableManual(validated.serviceName.c_str(), validated.advertisingName.c_str(), validated.advertisingShortName.c_str(), dataGetter, dataSetter, 1)
		: bzpStartWithBondable(validated.serviceName.c_str(), validated.advertisingName.c_str(), validated.advertisingShortName.c_str(), dataGetter, dataSetter, kMaxAsyncInitTimeoutMS, 1);
	if (!started)
	{
		DoctorOptions followupOptions;
		followupOptions.common = validated.common;
		const auto failureSnapshot = collectDoctorSnapshot(followupOptions, "com." + validated.serviceName);
		const auto failureReport = bzp::standalone::evaluateDoctorSnapshot(failureSnapshot, binaryName);
		std::ostringstream failure;
		failure << "BzPeri Demo\n";
		failure << "STATUS  FAIL\n";
		failure << "Managed sample session failed before it could publish com." << validated.serviceName << ".\n\n";
		failure << "WARNINGS\n";
		for (const auto &check : failureReport.checks)
		{
			if (check.verdict == bzp::standalone::Verdict::Pass)
			{
				continue;
			}
			failure << bzp::standalone::verdictLabel(check.verdict) << "  " << check.label << '\n';
			failure << "      " << check.detail << '\n';
		}
		failure << "\nNEXT\n";
		failure << "run: " << binaryName << " doctor\n";
		for (const auto &command : failureReport.nextCommands)
		{
			failure << "run: " << command << '\n';
		}
		printBlock(failure.str());
		return cleanupAndReturn(-1);
	}

	auto snapshot = buildInspectSnapshot(validated);
	{
		std::lock_guard<std::recursive_mutex> lock(inspectSessionMutex);
		inspectSessionStore = std::make_unique<bzp::standalone::InspectSessionStore>();
		if (snapshot)
		{
			inspectSessionSnapshot = std::make_unique<bzp::standalone::InspectSessionSnapshot>(*snapshot);
		}
		else
		{
			inspectSessionSnapshot.reset();
		}
	}
	if (snapshot)
	{
		persistInspectSession();
	}

	std::ostringstream success;
	success << "BzPeri Demo\n";
	success << "STATUS  PASS\n";
	success << "Managed sample session is advertising and ready.\n\n";
	success << "FACTS\n";
	success << "service: " << validated.serviceName << '\n';
	success << "advertising: " << validated.advertisingName << '\n';
	success << "sample namespace: " << validated.sampleNamespace << '\n';
	success << "loop mode: " << (validated.manualLoopMode ? "manual" : "threaded") << '\n';
	if (snapshot)
	{
		success << "adapter: " << snapshot->adapterPath;
		if (!snapshot->adapterAddress.empty())
		{
			success << " [" << snapshot->adapterAddress << "]";
		}
		success << '\n';
		success << "advertising active: " << (snapshot->advertisingEnabled ? "yes" : "no") << '\n';
		success << "object root: " << snapshot->objectRoot << '\n';
		success << "probe path: " << snapshot->selectedObjectPath << '\n';
		if (!snapshot->writeProbePath.empty())
		{
			success << "write probe: " << snapshot->writeProbePath << '\n';
		}
	}
	success << "\nNEXT\n";
	success << "run: Ctrl-C to stop the demo\n";
	success << "run: " << binaryName << " inspect --live\n";
	printBlock(success.str());

	bool shutdownTriggered = false;
	int batteryTickSeconds = 0;
	constexpr auto kLoopTick = std::chrono::milliseconds(100);
	auto lastTick = std::chrono::steady_clock::now();
	while (bzpGetServerRunState() < EStopping)
	{
		if (validated.manualLoopMode)
		{
			bzpRunLoopIterationFor(static_cast<int>(kLoopTick.count()));
		}
		else
		{
			std::this_thread::sleep_for(kLoopTick);
		}

		if (pendingShutdownSignal != 0 && !shutdownTriggered)
		{
			switch (pendingShutdownSignal)
			{
			case SIGINT:
				LogStatus("SIGINT received, shutting down");
				break;
			case SIGTERM:
				LogStatus("SIGTERM received, shutting down");
				break;
			default:
				LogStatus("Termination signal received, shutting down");
				break;
			}
			bzpTriggerShutdown();
			shutdownTriggered = true;
			refreshInspectSession(false);
		}

		const auto now = std::chrono::steady_clock::now();
		if (now - lastTick < std::chrono::seconds(1))
		{
			continue;
		}

		lastTick = now;
		batteryTickSeconds += 1;
		if (batteryTickSeconds < 15)
		{
			continue;
		}

		batteryTickSeconds = 0;
		serverDataBatteryLevel = std::max(serverDataBatteryLevel - 1, 0);
		if (!batteryLevelObjectPath.empty())
		{
			bzpNotifyUpdatedCharacteristic(batteryLevelObjectPath.c_str());
			recordInspectSemanticEvent(
				bzp::standalone::EventLevel::Status,
				"gatt",
				std::string("notify path=") + batteryLevelObjectPath + " value=" + std::to_string(serverDataBatteryLevel) + "%",
				true);
		}
		else
		{
			refreshInspectSession(false);
		}
	}

	if (validated.manualLoopMode)
	{
		while (bzpGetServerRunState() != EStopped)
		{
			if (!bzpRunLoopIterationFor(static_cast<int>(kLoopTick.count())))
			{
				std::this_thread::sleep_for(kLoopTick);
			}
		}

		if (!bzpWaitForShutdown(0))
		{
			return cleanupAndReturn(-1);
		}
		return cleanupAndReturn(bzpGetServerHealth() == EOk ? 0 : 1);
	}

	if (!bzpWait())
	{
		return cleanupAndReturn(-1);
	}

	return cleanupAndReturn(bzpGetServerHealth() == EOk ? 0 : 1);
}

} // namespace

int main(int argc, char **ppArgv)
{
	const std::string binaryName = executableName(ppArgv != nullptr ? ppArgv[0] : "bzp-standalone");
	const auto command = selectCommand(argc, ppArgv);
	if (command.optionStartIndex == 0)
	{
		std::cerr << "ERROR: unknown subcommand '" << ppArgv[1] << "'\n\n";
		printBlock(buildTopLevelHelp(binaryName));
		return 1;
	}

	if (command.mode == CommandMode::TopLevelHelp)
	{
		printBlock(buildTopLevelHelp(binaryName));
		return 0;
	}

	std::string error;
	switch (command.mode)
	{
	case CommandMode::Demo:
	{
		DemoOptions options;
		if (!parseDemoOptions(argc, ppArgv, command.optionStartIndex, &options, &error))
		{
			std::cerr << "ERROR: " << error << "\n\n";
			printBlock(buildDemoHelp(binaryName));
			return 1;
		}
		if (options.showHelp)
		{
			printBlock(buildDemoHelp(binaryName));
			return 0;
		}
		return runDemo(binaryName, options);
	}
	case CommandMode::Doctor:
	{
		DoctorOptions options;
		if (!parseDoctorOptions(argc, ppArgv, command.optionStartIndex, &options, &error))
		{
			std::cerr << "ERROR: " << error << "\n\n";
			printBlock(buildDoctorHelp(binaryName));
			return 1;
		}
		if (options.showHelp)
		{
			printBlock(buildDoctorHelp(binaryName));
			return 0;
		}
		return runDoctor(binaryName, options);
	}
	case CommandMode::Inspect:
	{
		InspectOptions options;
		if (!parseInspectOptions(argc, ppArgv, command.optionStartIndex, &options, &error))
		{
			std::cerr << "ERROR: " << error << "\n\n";
			printBlock(buildInspectHelp(binaryName));
			return 1;
		}
		if (options.showHelp)
		{
			printBlock(buildInspectHelp(binaryName));
			return 0;
		}
		return runInspect(binaryName, options);
	}
	case CommandMode::TopLevelHelp:
	default:
		printBlock(buildTopLevelHelp(binaryName));
		return 0;
	}
}
