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
//         `bzpWait()`. If the server has not yet reached the `EStopped` state when `bzpWait()` is called, it will block until the
//         server has done so.
//
//         To avoid the blocking behavior of `bzpWait()`, ensure that the server has stopped before calling it. This can be done
//         by ensuring `bzpGetServerRunState() == EStopped`. Even if the server has stopped, it is recommended to call `bzpWait()`
//         to ensure the server has cleaned up all threads and other internals.
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

#include "../include/BzPeri.h"
#include "../include/BzPeriConfigurator.h"
#include "SampleServices.h"

//
// Constants
//

// Maximum time to wait for any single async process to timeout during initialization
static const int kMaxAsyncInitTimeoutMS = 30 * 1000;

//
// Server data values
//

// The battery level ("battery/level") reported by the server (see Server.cpp)
static uint8_t serverDataBatteryLevel = 78;

// The text string ("text/string") used by our custom text string service (see Server.cpp)
static std::string serverDataTextString = "Hello, world!";

// Cached D-Bus path for the sample battery characteristic
static std::string batteryLevelObjectPath;

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

// We setup a couple Unix signals to perform graceful shutdown in the case of SIGTERM or get an SIGING (CTRL-C)
void signalHandler(int signum)
{
	switch (signum)
	{
		case SIGINT:
			LogStatus("SIGINT recieved, shutting down");
			bzpTriggerShutdown();
			break;
		case SIGTERM:
			LogStatus("SIGTERM recieved, shutting down");
			bzpTriggerShutdown();
			break;
	}
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
	signal(SIGINT, signalHandler);
	signal(SIGTERM, signalHandler);

	// Register our loggers
	bzpLogRegisterDebug(LogDebug);
	bzpLogRegisterInfo(LogInfo);
	bzpLogRegisterStatus(LogStatus);
	bzpLogRegisterWarn(LogWarn);
	bzpLogRegisterError(LogError);
	bzpLogRegisterFatal(LogFatal);
	bzpLogRegisterAlways(LogAlways);
	bzpLogRegisterTrace(LogTrace);

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
	if (!bzpStartWithBondable(serviceName.c_str(), advertisingName.c_str(), advertisingShortName.c_str(), dataGetter, dataSetter, kMaxAsyncInitTimeoutMS, 1))
	{
		return -1;
	}

	// Wait for the server to start the shutdown process
	//
	// While we wait, every 15 ticks, drop the battery level by one percent until we reach 0
	while (bzpGetServerRunState() < EStopping)
	{
		std::this_thread::sleep_for(std::chrono::seconds(15));

		serverDataBatteryLevel = std::max(serverDataBatteryLevel - 1, 0);
		if (!batteryLevelObjectPath.empty())
		{
			bzpNofifyUpdatedCharacteristic(batteryLevelObjectPath.c_str());
		}
	}

	// Wait for the server to come to a complete stop (CTRL-C from the command line)
	if (!bzpWait())
	{
		return -1;
	}

	// Return the final server health status as a success (0) or error (-1)
  	return bzpGetServerHealth() == EOk ? 0 : 1;
}
