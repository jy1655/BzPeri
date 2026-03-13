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
// things that are reocommended ("SHOULD").
//
// * A stand-alone application MUST:
//
//     * Start the server via a call to `bzpStart()`.
//
//         Once started the server will run on its own thread.
//
//         Two of the parameters to `bzpStart()` are delegates responsible for providing data accessors for the server, a
//         `BZPServerDataGetter` delegate and a 'BZPServerDataSetter' delegate. The getter method simply receives a string name (for
//         example, "battery/level") and returns a void pointer to that data (for example: `(void *)&batteryLevel`). The setter does
//         the same only in reverse.
//
//         While the server is running, you will likely need to update the data being served. This is done by calling
//         `bzpNofifyUpdatedCharacteristic()` or `bzpNofifyUpdatedDescriptor()` with the full path to the characteristic or delegate
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
// The BzPeri distribution includes this file as part of the BzPeri files with everything compiling to a single, stand-
// alone binary. It is built this way because BzPeri is not intended to be a generic library. You will need to make your
// custom modifications to it. Don't worry, a lot of work went into BzPeri to make it almost trivial to customize
// (see Server.cpp).
//
// If it is important to you or your build process that BzPeri exist as a library, you are welcome to do so. Just configure
// your build process to build the BzPeri files (minus this file) as a library and link against that instead. All that is
// required by applications linking to a BzPeri library is to include `include/BzPeri.h`.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#include <signal.h>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <thread>
#include <sstream>
#include <cstdlib>
#include <vector>

#include "../include/BzPeri.h"
#include "../include/BzPeriConfigurator.h"
#include "SampleServices.h"

//
// Constants
//

// Maximum time to wait for any single async process to timeout during initialization
static const int kMaxAsyncInitTimeoutMS = 30 * 1000;

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

// Signal state is shared with the sample's main loop and must stay async-signal-safe.
static volatile sig_atomic_t pendingShutdownSignal = 0;

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

// Our full set of logging methods (we just log to stdout)
//
// NOTE: Some methods will only log if the appropriate `logLevel` is set
void LogDebug(const char *pText) { if (logLevel <= Debug) { std::cout << "  DEBUG: " << pText << std::endl; } }
void LogInfo(const char *pText) { if (logLevel <= Verbose) { std::cout << "   INFO: " << pText << std::endl; } }
void LogStatus(const char *pText) { if (logLevel <= Normal) { std::cout << " STATUS: " << pText << std::endl; } }
void LogWarn(const char *pText) { std::cout << "WARNING: " << pText << std::endl; }
void LogError(const char *pText) { std::cout << "!!ERROR: " << pText << std::endl; }
void LogFatal(const char *pText) { std::cout << "**FATAL: " << pText << std::endl; }
void LogAlways(const char *pText) { std::cout << "..Log..: " << pText << std::endl; }
void LogTrace(const char *pText) { std::cout << "-Trace-: " << pText << std::endl; }

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
		return &serverDataBatteryLevel;
	}
	else if (strName == "text/string")
	{
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
		return 1;
	}
	else if (strName == "text/string")
	{
		serverDataTextString = static_cast<const char *>(pData);
		LogDebug((std::string("Server data: text string set to '") + serverDataTextString + "'").c_str());
		return 1;
	}

	LogWarn((std::string("Unknown name for server data setter request: '") + pName + "'").c_str());

	return 0;
}

//
// Entry point
//

int main(int argc, char **ppArgv)
{
	std::string serviceName = "bzperi";
	std::string advertisingName = "BzPeri";
	std::string advertisingShortName = "BzPeri";
	std::string sampleNamespace = "samples";
	bool includeSampleServices = true;
	bool manualLoopMode = false;
	BZPGLibLogCaptureMode glibCaptureMode = bzpGetConfiguredGLibLogCaptureMode();
	unsigned int glibCaptureTargets = bzpGetConfiguredGLibLogCaptureTargets();
	unsigned int glibCaptureDomains = BZP_GLIB_LOG_CAPTURE_DOMAIN_ALL;
	bool installHostManagedCapture = false;

	// A basic command-line parser
	for (int i = 1; i < argc; ++i)
	{
		std::string arg = ppArgv[i];
		if (arg == "-q")
		{
			logLevel = ErrorsOnly;
		}
		else if (arg == "-v")
		{
			logLevel = Verbose;
		}
		else if  (arg == "-d")
		{
			logLevel = Debug;
		}
		else if (arg.substr(0, 10) == "--adapter=")
		{
			std::string adapterName = arg.substr(10);
			// Set environment variable for BluezAdapter to use
			setenv("BLUEZ_ADAPTER", adapterName.c_str(), 1);
			LogStatus((std::string("Using BlueZ adapter: ") + adapterName).c_str());
		}
		else if (arg == "--list-adapters")
		{
			LogStatus("Available BlueZ adapters will be listed during startup");
			setenv("BLUEZ_LIST_ADAPTERS", "1", 1);
		}
		else if (arg.rfind("--service-name=", 0) == 0)
		{
			serviceName = arg.substr(15);
		}
		else if (arg.rfind("--advertise-name=", 0) == 0)
		{
			advertisingName = arg.substr(17);
		}
		else if (arg.rfind("--advertise-short=", 0) == 0)
		{
			advertisingShortName = arg.substr(18);
		}
		else if (arg == "--no-sample-services")
		{
			includeSampleServices = false;
		}
		else if (arg == "--with-sample-services")
		{
			includeSampleServices = true;
		}
		else if (arg == "--manual-loop")
		{
			manualLoopMode = true;
		}
		else if (arg.rfind("--glib-log-capture=", 0) == 0)
		{
			std::string mode = arg.substr(19);
			std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			if (mode == "auto" || mode == "automatic")
			{
				glibCaptureMode = BZP_GLIB_LOG_CAPTURE_AUTOMATIC;
			}
			else if (mode == "off" || mode == "disabled" || mode == "disable")
			{
				glibCaptureMode = BZP_GLIB_LOG_CAPTURE_DISABLED;
			}
			else if (mode == "host" || mode == "host-managed")
			{
				glibCaptureMode = BZP_GLIB_LOG_CAPTURE_HOST_MANAGED;
				installHostManagedCapture = true;
			}
			else if (mode == "startup-shutdown" || mode == "transient")
			{
				glibCaptureMode = BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN;
			}
			else
			{
				LogFatal((std::string("Unknown GLib log capture mode: '") + mode + "'").c_str());
				LogFatal("Expected one of: auto, off, host, startup-shutdown");
				return -1;
			}
		}
		else if (arg.rfind("--glib-log-targets=", 0) == 0)
		{
			unsigned int parsedTargets = 0;
			const std::string targetSpec = arg.substr(19);
			if (!parseGLibCaptureTargets(targetSpec, &parsedTargets))
			{
				LogFatal((std::string("Unknown GLib log capture targets: '") + targetSpec + "'").c_str());
				LogFatal("Expected 'all' or a comma-separated subset of: log, print, printerr");
				return -1;
			}
			glibCaptureTargets = parsedTargets;
		}
		else if (arg.rfind("--glib-log-domains=", 0) == 0)
		{
			unsigned int parsedDomains = 0;
			const std::string domainSpec = arg.substr(19);
			if (!parseGLibCaptureDomains(domainSpec, &parsedDomains))
			{
				LogFatal((std::string("Unknown GLib log capture domains: '") + domainSpec + "'").c_str());
				LogFatal("Expected 'all' or a comma-separated subset of: default, glib, gio, bluez, other");
				return -1;
			}
			glibCaptureDomains = parsedDomains;
		}
		else if (arg.rfind("--sample-namespace=", 0) == 0)
		{
			sampleNamespace = arg.substr(19);
		}
		else if (arg == "--help" || arg == "-h")
		{
			LogAlways("Usage: standalone [options]");
			LogAlways("");
			LogAlways("Logging options:");
			LogAlways("  -q              Quiet mode (errors only)");
			LogAlways("  -v              Verbose mode");
			LogAlways("  -d              Debug mode");
			LogAlways("");
			LogAlways("BlueZ options:");
			LogAlways("  --adapter=NAME  Use specific adapter (e.g. hci0, hci1)");
			LogAlways("  --list-adapters List available adapters and exit");
			LogAlways("");
			LogAlways("General options:");
			LogAlways("  --service-name=NAME      Set D-Bus service namespace (default bzperi)");
			LogAlways("  --advertise-name=NAME    Set LE advertising name (default BzPeri)");
			LogAlways("  --advertise-short=NAME   Set LE advertising short name (default BzPeri)");
			LogAlways("  --sample-namespace=NODE  Namespace node for example services (default samples)");
			LogAlways("  --manual-loop            Drive BzPeri via bzpRunLoopIteration() instead of the internal thread");
			LogAlways("  --glib-log-capture=MODE Set GLib capture mode: auto, off, host, or startup-shutdown");
			LogAlways("  --glib-log-targets=SET  Set GLib capture targets: all or comma-separated log,print,printerr");
			LogAlways("  --glib-log-domains=SET  Set GLib log domains: all or comma-separated default,glib,gio,bluez,other");
			LogAlways("  --no-sample-services      Disable bundled example GATT services");
			LogAlways("  --with-sample-services    Re-enable bundled example services after disabling");
			LogAlways("  --help, -h                Show this help message");
			return 0;
		}
		else
		{
			LogFatal((std::string("Unknown parameter: '") + arg + "'").c_str());
			LogFatal("");
			LogFatal("Usage: standalone [options]");
			LogFatal("Use --help for detailed options");
			return -1;
		}
	}

	auto toLowerCopy = [](std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return value;
	};

	serviceName = toLowerCopy(serviceName);
	sampleNamespace = toLowerCopy(sampleNamespace);

	if (serviceName.empty())
	{
		LogFatal("Service name cannot be empty");
		return -1;
	}

	// Validate service name follows com.bzperi.* namespace pattern
	if (serviceName != "bzperi" && serviceName.find("bzperi.") != 0)
	{
		LogFatal("Service name must be 'bzperi' or start with 'bzperi.' (e.g., 'bzperi.myapp')");
		LogFatal("This ensures D-Bus policy compatibility and prevents conflicts");
		return -1;
	}

	if (sampleNamespace.find('/') != std::string::npos)
	{
		LogFatal("Sample namespace must not contain '/' characters");
		return -1;
	}

	if (bzp::serviceConfiguratorCount() > 0)
	{
		LogWarn("Existing service configurators cleared for standalone configuration");
	}

	bzp::clearServiceConfigurators();

	if (includeSampleServices)
	{
		bzp::samples::registerSampleServices(sampleNamespace);

		// Convert dots in service name to slashes for valid D-Bus object path
		// e.g., "bzperi.myapp" becomes "/com/bzperi/myapp"
		std::string pathServiceName = serviceName;
		std::replace(pathServiceName.begin(), pathServiceName.end(), '.', '/');
		std::string pathBase = std::string("/com/") + pathServiceName;

		if (!sampleNamespace.empty())
		{
			pathBase += "/" + sampleNamespace;
		}

		batteryLevelObjectPath = pathBase + "/battery/level";
		LogStatus((std::string("Bundled example services registered under ") + pathBase).c_str());
	}
	else
	{
		batteryLevelObjectPath.clear();
		LogStatus("Bundled example services disabled; starting with empty server");
	}

	// Setup our signal handlers
	installSignalHandler(SIGINT);
	installSignalHandler(SIGTERM);

	// Register our loggers
	bzpLogRegisterDebug(LogDebug);
	bzpLogRegisterInfo(LogInfo);
	bzpLogRegisterStatus(LogStatus);
	bzpLogRegisterWarn(LogWarn);
	bzpLogRegisterError(LogError);
	bzpLogRegisterFatal(LogFatal);
	bzpLogRegisterAlways(LogAlways);
	bzpLogRegisterTrace(LogTrace);

	bzpSetGLibLogCaptureMode(glibCaptureMode);
	bzpSetGLibLogCaptureTargets(glibCaptureTargets);
	bzpSetGLibLogCaptureDomains(glibCaptureDomains);
	if (glibCaptureMode == BZP_GLIB_LOG_CAPTURE_AUTOMATIC)
	{
		LogStatus("GLib log capture mode: automatic");
	}
	else if (glibCaptureMode == BZP_GLIB_LOG_CAPTURE_STARTUP_AND_SHUTDOWN)
	{
		LogStatus("GLib log capture mode: startup-and-shutdown");
	}
	else if (glibCaptureMode == BZP_GLIB_LOG_CAPTURE_DISABLED)
	{
		LogStatus("GLib log capture mode: disabled");
	}
	else
	{
		LogStatus("GLib log capture mode: host-managed");
	}
	LogStatus((std::string("GLib log capture targets: ") + describeGLibCaptureTargets(glibCaptureTargets)).c_str());
	LogStatus((std::string("GLib log capture domains: ") + describeGLibCaptureDomains(glibCaptureDomains)).c_str());

	bool hostManagedCaptureInstalled = false;
	if (installHostManagedCapture)
	{
		hostManagedCaptureInstalled = bzpInstallGLibLogCapture() != 0;
		if (!hostManagedCaptureInstalled)
		{
			LogFatal("Failed to install host-managed GLib log capture");
			return -1;
		}
	}

	if (manualLoopMode)
	{
		LogStatus("Using manual BzPeri run-loop mode");
	}

	// Start the server's ascync processing
	//
	// This starts the server on a thread and begins the initialization process
	//
	// !!!IMPORTANT!!!
	//
	//     This first parameter (the service name) must match tha name configured in the D-Bus permissions. See the Readme.md file
	//     for more information.
	//
	//     The last parameter (enableBondable=1) allows client devices to pair/bond with this server. This is typically
	//     required for modern BLE applications. Set to 0 to disable pairing if you need an open, non-authenticated connection.
	//
	const int started = manualLoopMode
		? bzpStartWithBondableManual(serviceName.c_str(), advertisingName.c_str(), advertisingShortName.c_str(), dataGetter, dataSetter, 1)
		: bzpStartWithBondable(serviceName.c_str(), advertisingName.c_str(), advertisingShortName.c_str(), dataGetter, dataSetter, kMaxAsyncInitTimeoutMS, 1);
	if (!started)
	{
		if (hostManagedCaptureInstalled)
		{
			bzpRestoreGLibLogCapture();
		}
		return -1;
	}

	// Wait for the server to start the shutdown process
	//
	// While we wait, every 15 ticks, drop the battery level by one percent until we reach 0
	bool shutdownTriggered = false;
	int batteryTickSeconds = 0;
	constexpr auto kLoopTick = std::chrono::milliseconds(100);
	auto lastTick = std::chrono::steady_clock::now();
	while (bzpGetServerRunState() < EStopping)
	{
		if (manualLoopMode)
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
		}
	}

	if (manualLoopMode)
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
			if (hostManagedCaptureInstalled)
			{
				bzpRestoreGLibLogCapture();
			}
			return -1;
		}

		if (hostManagedCaptureInstalled)
		{
			bzpRestoreGLibLogCapture();
		}
		return bzpGetServerHealth() == EOk ? 0 : 1;
	}

	// Wait for the server to come to a complete stop (CTRL-C from the command line)
	if (!bzpWait())
	{
		if (hostManagedCaptureInstalled)
		{
			bzpRestoreGLibLogCapture();
		}
		return -1;
	}

	if (hostManagedCaptureInstalled)
	{
		bzpRestoreGLibLogCapture();
	}

	// Return the final server health status as a success (0) or error (-1)
  	return bzpGetServerHealth() == EOk ? 0 : 1;
}
