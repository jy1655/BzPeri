#include "StandaloneWorkflow.h"

#include <gio/gio.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

namespace bzp::standalone {

namespace {

constexpr int kSessionFormatVersion = 2;
constexpr std::size_t kDefaultInspectEventDisplayCount = 8;
constexpr std::size_t kVerboseInspectEventDisplayCount = 16;

std::string joinCommands(const std::vector<std::string> &commands)
{
	std::ostringstream stream;
	for (std::size_t i = 0; i < commands.size(); ++i)
	{
		if (i != 0)
		{
			stream << '\n';
		}
		stream << "run: " << commands[i];
	}
	return stream.str();
}

void appendSection(std::ostringstream &stream, const std::string &title)
{
	stream << '\n' << title << '\n';
}

void appendCheckLines(std::ostringstream &stream, const DoctorCheck &check)
{
	stream << verdictLabel(check.verdict) << "  " << check.label << '\n';
	stream << "      " << check.detail << '\n';
}

std::string formatTimestamp(long long timestampMs)
{
	if (timestampMs <= 0)
	{
		return "--:--:--";
	}

	const std::time_t seconds = static_cast<std::time_t>(timestampMs / 1000);
	std::tm localTime = {};
	localtime_r(&seconds, &localTime);

	std::ostringstream stream;
	stream << std::put_time(&localTime, "%H:%M:%S");
	return stream.str();
}

void appendStringList(GKeyFile *keyFile, const char *group, const char *key, const std::vector<std::string> &values)
{
	if (values.empty())
	{
		return;
	}

	std::vector<const gchar *> stringRefs;
	stringRefs.reserve(values.size());
	for (const auto &value : values)
	{
		stringRefs.push_back(value.c_str());
	}

	g_key_file_set_string_list(keyFile, group, key, stringRefs.data(), stringRefs.size());
}

std::vector<std::string> loadStringList(GKeyFile *keyFile, const char *group, const char *key)
{
	gsize length = 0;
	GError *error = nullptr;
	gchar **values = g_key_file_get_string_list(keyFile, group, key, &length, &error);
	if (values == nullptr)
	{
		if (error != nullptr)
		{
			g_error_free(error);
		}
		return {};
	}

	std::vector<std::string> result;
	result.reserve(length);
	for (gsize i = 0; i < length; ++i)
	{
		result.emplace_back(values[i] != nullptr ? values[i] : "");
	}
	g_strfreev(values);
	return result;
}

std::string loadStringValue(GKeyFile *keyFile, const char *group, const char *key)
{
	GError *error = nullptr;
	gchar *value = g_key_file_get_string(keyFile, group, key, &error);
	if (value == nullptr)
	{
		if (error != nullptr)
		{
			g_error_free(error);
		}
		return "";
	}

	std::string result(value);
	g_free(value);
	return result;
}

int loadIntegerValue(GKeyFile *keyFile, const char *group, const char *key, int fallback = 0)
{
	GError *error = nullptr;
	const int value = g_key_file_get_integer(keyFile, group, key, &error);
	if (error != nullptr)
	{
		g_error_free(error);
		return fallback;
	}
	return value;
}

long long loadInt64Value(GKeyFile *keyFile, const char *group, const char *key, long long fallback = 0)
{
	GError *error = nullptr;
	const gint64 value = g_key_file_get_int64(keyFile, group, key, &error);
	if (error != nullptr)
	{
		g_error_free(error);
		return fallback;
	}
	return static_cast<long long>(value);
}

bool loadBooleanValue(GKeyFile *keyFile, const char *group, const char *key, bool fallback = false)
{
	GError *error = nullptr;
	const gboolean value = g_key_file_get_boolean(keyFile, group, key, &error);
	if (error != nullptr)
	{
		g_error_free(error);
		return fallback;
	}
	return value != FALSE;
}

void setError(std::string *errorOut, std::string message)
{
	if (errorOut != nullptr)
	{
		*errorOut = std::move(message);
	}
}

} // namespace

DoctorSnapshot collectDoctorSnapshot(const DoctorProbe &probe, const std::string &preferredAdapter, const std::string &ownedName)
{
	DoctorSnapshot snapshot;
	snapshot.systemBusReachable = probe.checkSystemBus(&snapshot.systemBusDetail);
	if (!snapshot.systemBusReachable)
	{
		return snapshot;
	}

	snapshot.bluezServiceReachable = probe.checkBluezService(&snapshot.bluezServiceDetail);
	snapshot.serviceNameAvailable = probe.checkServiceOwnership(ownedName, &snapshot.serviceNameDetail);
	snapshot.policyInstalled = probe.checkPolicy(&snapshot.policyPath);
	snapshot.experimentalHelperAvailable = probe.checkExperimentalHelper(
		&snapshot.experimentalHelperPath,
		&snapshot.experimentalModeEnabled,
		&snapshot.experimentalDetail);
	snapshot.adapterProbeSucceeded = probe.probeAdapter(
		preferredAdapter,
		&snapshot.poweredAdapterAvailable,
		&snapshot.adapterSummary,
		&snapshot.adapterLines,
		&snapshot.adapterProbeDetail);
	return snapshot;
}

DoctorReport evaluateDoctorSnapshot(const DoctorSnapshot &snapshot, const std::string &binaryName)
{
	DoctorReport report;
	report.adapterLines = snapshot.adapterLines;
	report.checks = {
		{"System bus", snapshot.systemBusReachable ? Verdict::Pass : Verdict::Fail,
			snapshot.systemBusReachable ? snapshot.systemBusDetail : (snapshot.systemBusDetail.empty() ? "Failed to connect to the system D-Bus." : snapshot.systemBusDetail)},
		{"BlueZ service", snapshot.bluezServiceReachable ? Verdict::Pass : Verdict::Fail,
			snapshot.bluezServiceReachable ? snapshot.bluezServiceDetail : (snapshot.bluezServiceDetail.empty() ? "org.bluez is not currently owned on the system bus." : snapshot.bluezServiceDetail)},
		{"D-Bus owned name", snapshot.serviceNameAvailable ? Verdict::Pass : Verdict::Fail,
			snapshot.serviceNameAvailable ? snapshot.serviceNameDetail : (snapshot.serviceNameDetail.empty() ? "Current process could not own com.bzperi on the system bus." : snapshot.serviceNameDetail)},
		{"Adapter readiness",
			(!snapshot.adapterProbeSucceeded || !snapshot.poweredAdapterAvailable) ? Verdict::Fail : Verdict::Pass,
			(!snapshot.adapterProbeSucceeded || !snapshot.poweredAdapterAvailable)
				? (!snapshot.adapterProbeDetail.empty() ? snapshot.adapterProbeDetail : "No powered BLE adapter is ready for BzPeri.")
				: snapshot.adapterSummary},
		{"D-Bus policy", snapshot.policyInstalled ? Verdict::Pass : Verdict::Fail,
			snapshot.policyInstalled
				? ("Found " + snapshot.policyPath)
				: ("Missing required policy file: " + (snapshot.policyPath.empty() ? "/etc/dbus-1/system.d/com.bzperi.conf" : snapshot.policyPath))},
		{"BlueZ experimental mode",
			!snapshot.experimentalHelperAvailable ? Verdict::Warn : (snapshot.experimentalModeEnabled ? Verdict::Pass : Verdict::Warn),
			snapshot.experimentalHelperAvailable
				? (snapshot.experimentalDetail.empty()
					? (snapshot.experimentalModeEnabled
						? "Experimental mode is enabled."
						: "Experimental mode is not enabled.")
					: snapshot.experimentalDetail)
				: (snapshot.experimentalDetail.empty() ? "BlueZ experimental helper not found; use manual bluetoothd --experimental guidance if needed." : snapshot.experimentalDetail)},
	};

	const bool hasFail = std::any_of(report.checks.begin(), report.checks.end(), [](const DoctorCheck &check) {
		return check.verdict == Verdict::Fail;
	});
	const bool hasWarn = std::any_of(report.checks.begin(), report.checks.end(), [](const DoctorCheck &check) {
		return check.verdict == Verdict::Warn;
	});
	report.overall = hasFail ? Verdict::Fail : (hasWarn ? Verdict::Warn : Verdict::Pass);

	auto addNextCommand = [&report](const std::string &command) {
		if (std::find(report.nextCommands.begin(), report.nextCommands.end(), command) == report.nextCommands.end())
		{
			report.nextCommands.push_back(command);
		}
	};

	if (!snapshot.systemBusReachable)
	{
		addNextCommand("sudo systemctl status dbus");
	}

	if (!snapshot.bluezServiceReachable)
	{
		addNextCommand("sudo systemctl status bluetooth");
	}

	if (!snapshot.serviceNameAvailable)
	{
		addNextCommand("sudo " + binaryName + " doctor");
		addNextCommand("sudo " + binaryName + " demo");
	}

	if (!snapshot.adapterProbeSucceeded)
	{
		addNextCommand("sudo bluetoothctl list");
	}
	else if (!snapshot.poweredAdapterAvailable)
	{
		addNextCommand("sudo bluetoothctl power on");
		addNextCommand(binaryName + " demo --list-adapters");
	}

	if (!snapshot.policyInstalled)
	{
		addNextCommand("sudo cp dbus/com.bzperi.conf /etc/dbus-1/system.d/");
		addNextCommand("sudo systemctl reload dbus");
	}

	if (snapshot.experimentalHelperAvailable)
	{
		addNextCommand("sudo " + snapshot.experimentalHelperPath + " status");
		if (!snapshot.experimentalModeEnabled)
		{
			addNextCommand("sudo " + snapshot.experimentalHelperPath + " enable");
		}
	}

	if (report.overall != Verdict::Fail)
	{
		addNextCommand(binaryName + " demo");
	}

	return report;
}

int exitCodeForDoctorReport(const DoctorReport &report) noexcept
{
	return report.overall == Verdict::Fail ? 1 : 0;
}

std::string formatDoctorReport(const DoctorReport &report, const std::string &binaryName)
{
	std::ostringstream stream;
	stream << "BzPeri Doctor\n";
	stream << "STATUS  " << verdictLabel(report.overall) << '\n';
	stream << "Checks the local host for the " << binaryName << " happy path.\n";

	appendSection(stream, "CHECKS");
	for (const auto &check : report.checks)
	{
		appendCheckLines(stream, check);
	}

	if (!report.adapterLines.empty())
	{
		appendSection(stream, "ADAPTERS");
		for (const auto &line : report.adapterLines)
		{
			stream << line << '\n';
		}
	}

	appendSection(stream, "NEXT");
	stream << joinCommands(report.nextCommands) << '\n';
	return stream.str();
}

std::string verdictLabel(Verdict verdict)
{
	switch (verdict)
	{
	case Verdict::Pass:
		return "PASS";
	case Verdict::Warn:
		return "WARN";
	case Verdict::Fail:
	default:
		return "FAIL";
	}
}

InspectSessionStore::InspectSessionStore(std::string path, std::size_t eventLimit)
	: path_(std::move(path)),
	  eventLimit_(eventLimit)
{
}

std::string InspectSessionStore::defaultSessionPath()
{
	try
	{
		const gchar *runtimeDir = g_get_user_runtime_dir();
		std::filesystem::path basePath = (runtimeDir != nullptr && *runtimeDir != '\0')
			? std::filesystem::path(runtimeDir)
			: std::filesystem::temp_directory_path();
		basePath /= "bzperi";
		basePath /= "bzp-standalone-session.ini";
		return basePath.string();
	}
	catch (...)
	{
		return "/tmp/bzperi/bzp-standalone-session.ini";
	}
}

bool InspectSessionStore::save(const InspectSessionSnapshot &snapshot, std::string *errorOut) const
{
	try
	{
		const std::filesystem::path filePath(path_);
		if (filePath.has_parent_path())
		{
			std::filesystem::create_directories(filePath.parent_path());
		}
	}
	catch (const std::exception &error)
	{
		setError(errorOut, error.what());
		return false;
	}

	GKeyFile *keyFile = g_key_file_new();
	g_key_file_set_integer(keyFile, "session", "format_version", snapshot.formatVersion);
	g_key_file_set_integer(keyFile, "session", "pid", snapshot.pid);
	g_key_file_set_int64(keyFile, "session", "updated_at_ms", snapshot.updatedAtMs);
	g_key_file_set_string(keyFile, "session", "service_name", snapshot.serviceName.c_str());
	g_key_file_set_string(keyFile, "session", "advertising_name", snapshot.advertisingName.c_str());
	g_key_file_set_string(keyFile, "session", "advertising_short_name", snapshot.advertisingShortName.c_str());
	g_key_file_set_string(keyFile, "session", "sample_namespace", snapshot.sampleNamespace.c_str());
	g_key_file_set_string(keyFile, "session", "object_root", snapshot.objectRoot.c_str());
	g_key_file_set_string(keyFile, "session", "selected_object_path", snapshot.selectedObjectPath.c_str());
	g_key_file_set_string(keyFile, "session", "write_probe_path", snapshot.writeProbePath.c_str());
	g_key_file_set_string(keyFile, "session", "selected_object_summary", snapshot.selectedObjectSummary.c_str());
	g_key_file_set_string(keyFile, "session", "adapter_path", snapshot.adapterPath.c_str());
	g_key_file_set_string(keyFile, "session", "adapter_address", snapshot.adapterAddress.c_str());
	g_key_file_set_string(keyFile, "session", "adapter_alias", snapshot.adapterAlias.c_str());
	g_key_file_set_boolean(keyFile, "session", "adapter_powered", snapshot.adapterPowered);
	g_key_file_set_boolean(keyFile, "session", "include_sample_services", snapshot.includeSampleServices);
	g_key_file_set_boolean(keyFile, "session", "manual_loop_mode", snapshot.manualLoopMode);
	g_key_file_set_boolean(keyFile, "session", "advertising_enabled", snapshot.advertisingEnabled);
	g_key_file_set_integer(keyFile, "session", "active_connections", snapshot.activeConnections);
	g_key_file_set_integer(keyFile, "session", "object_count", snapshot.objectCount);
	g_key_file_set_integer(keyFile, "session", "run_state", snapshot.runState);
	g_key_file_set_integer(keyFile, "session", "health", snapshot.health);
	g_key_file_set_int64(keyFile, "session", "dropped_event_count", static_cast<gint64>(snapshot.droppedEventCount));

	appendStringList(keyFile, "session", "object_tree", snapshot.objectTreeLines);
	appendStringList(keyFile, "session", "selected_object_lines", snapshot.selectedObjectLines);
	appendStringList(keyFile, "session", "warnings", snapshot.warnings);

	g_key_file_set_integer(keyFile, "events", "count", static_cast<int>(snapshot.events.size()));
	for (std::size_t index = 0; index < snapshot.events.size(); ++index)
	{
		const auto group = "event_" + std::to_string(index);
		g_key_file_set_int64(keyFile, group.c_str(), "timestamp_ms", snapshot.events[index].timestampMs);
		g_key_file_set_string(keyFile, group.c_str(), "level", eventLevelLabel(snapshot.events[index].level).c_str());
		g_key_file_set_string(keyFile, group.c_str(), "component", snapshot.events[index].component.c_str());
		g_key_file_set_string(keyFile, group.c_str(), "message", snapshot.events[index].message.c_str());
	}

	gsize dataSize = 0;
	GError *error = nullptr;
	gchar *data = g_key_file_to_data(keyFile, &dataSize, &error);
	if (data == nullptr)
	{
		setError(errorOut, error != nullptr ? error->message : "Failed to serialize inspect session");
		if (error != nullptr)
		{
			g_error_free(error);
		}
		g_key_file_unref(keyFile);
		return false;
	}

	const gboolean writeResult = g_file_set_contents(path_.c_str(), data, static_cast<gssize>(dataSize), &error);
	g_free(data);
	g_key_file_unref(keyFile);
	if (writeResult == FALSE)
	{
		setError(errorOut, error != nullptr ? error->message : "Failed to write inspect session file");
		if (error != nullptr)
		{
			g_error_free(error);
		}
		return false;
	}

	return true;
}

std::optional<InspectSessionSnapshot> InspectSessionStore::load(std::string *errorOut) const
{
	GError *error = nullptr;
	GKeyFile *keyFile = g_key_file_new();
	const gboolean loaded = g_key_file_load_from_file(keyFile, path_.c_str(), G_KEY_FILE_NONE, &error);
	if (loaded == FALSE)
	{
		if (error != nullptr && error->domain == G_FILE_ERROR && error->code == G_FILE_ERROR_NOENT)
		{
			g_error_free(error);
			g_key_file_unref(keyFile);
			return std::nullopt;
		}

		setError(errorOut, error != nullptr ? error->message : "Failed to read inspect session file");
		if (error != nullptr)
		{
			g_error_free(error);
		}
		g_key_file_unref(keyFile);
		return std::nullopt;
	}

	InspectSessionSnapshot snapshot;
	snapshot.formatVersion = loadIntegerValue(keyFile, "session", "format_version", kSessionFormatVersion);
	snapshot.pid = loadIntegerValue(keyFile, "session", "pid", 0);
	snapshot.updatedAtMs = loadInt64Value(keyFile, "session", "updated_at_ms", 0);
	snapshot.serviceName = loadStringValue(keyFile, "session", "service_name");
	snapshot.advertisingName = loadStringValue(keyFile, "session", "advertising_name");
	snapshot.advertisingShortName = loadStringValue(keyFile, "session", "advertising_short_name");
	snapshot.sampleNamespace = loadStringValue(keyFile, "session", "sample_namespace");
	snapshot.objectRoot = loadStringValue(keyFile, "session", "object_root");
	snapshot.selectedObjectPath = loadStringValue(keyFile, "session", "selected_object_path");
	snapshot.writeProbePath = loadStringValue(keyFile, "session", "write_probe_path");
	snapshot.selectedObjectSummary = loadStringValue(keyFile, "session", "selected_object_summary");
	snapshot.adapterPath = loadStringValue(keyFile, "session", "adapter_path");
	snapshot.adapterAddress = loadStringValue(keyFile, "session", "adapter_address");
	snapshot.adapterAlias = loadStringValue(keyFile, "session", "adapter_alias");
	snapshot.adapterPowered = loadBooleanValue(keyFile, "session", "adapter_powered", false);
	snapshot.includeSampleServices = loadBooleanValue(keyFile, "session", "include_sample_services", true);
	snapshot.manualLoopMode = loadBooleanValue(keyFile, "session", "manual_loop_mode", false);
	snapshot.advertisingEnabled = loadBooleanValue(keyFile, "session", "advertising_enabled", false);
	snapshot.activeConnections = loadIntegerValue(keyFile, "session", "active_connections", 0);
	snapshot.objectCount = loadIntegerValue(keyFile, "session", "object_count", 0);
	snapshot.runState = loadIntegerValue(keyFile, "session", "run_state", 0);
	snapshot.health = loadIntegerValue(keyFile, "session", "health", 0);
	snapshot.droppedEventCount = static_cast<std::size_t>(loadInt64Value(keyFile, "session", "dropped_event_count", 0));
	snapshot.objectTreeLines = loadStringList(keyFile, "session", "object_tree");
	snapshot.selectedObjectLines = loadStringList(keyFile, "session", "selected_object_lines");
	snapshot.warnings = loadStringList(keyFile, "session", "warnings");

	const int eventCount = loadIntegerValue(keyFile, "events", "count", 0);
	for (int index = 0; index < eventCount; ++index)
	{
		const auto group = "event_" + std::to_string(index);
		const auto levelText = loadStringValue(keyFile, group.c_str(), "level");
		EventLevel level = EventLevel::Info;
		if (levelText == "TRACE")
		{
			level = EventLevel::Trace;
		}
		else if (levelText == "DEBUG")
		{
			level = EventLevel::Debug;
		}
		else if (levelText == "STATUS")
		{
			level = EventLevel::Status;
		}
		else if (levelText == "WARN")
		{
			level = EventLevel::Warn;
		}
		else if (levelText == "ERROR")
		{
			level = EventLevel::Error;
		}
		else if (levelText == "FATAL")
		{
			level = EventLevel::Fatal;
		}

		snapshot.events.push_back({
			loadInt64Value(keyFile, group.c_str(), "timestamp_ms", 0),
			level,
			loadStringValue(keyFile, group.c_str(), "component"),
			loadStringValue(keyFile, group.c_str(), "message"),
		});
	}

	g_key_file_unref(keyFile);
	return snapshot;
}

bool InspectSessionStore::clear(std::string *errorOut) const
{
	std::error_code errorCode;
	const bool removed = std::filesystem::remove(path_, errorCode);
	if (!removed && errorCode)
	{
		setError(errorOut, errorCode.message());
		return false;
	}
	return true;
}

long long currentTimeMs() noexcept
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

void appendInspectEvent(InspectSessionSnapshot &snapshot, const InspectEvent &event, std::size_t eventLimit)
{
	snapshot.events.push_back(event);
	if (snapshot.events.size() > eventLimit)
	{
		const auto overflowCount = snapshot.events.size() - eventLimit;
		snapshot.events.erase(snapshot.events.begin(), snapshot.events.begin() + static_cast<std::ptrdiff_t>(overflowCount));
		snapshot.droppedEventCount += overflowCount;
	}
	snapshot.updatedAtMs = currentTimeMs();
}

bool shouldDisplayEvent(const InspectEvent &event, bool verboseEvents) noexcept
{
	if (verboseEvents)
	{
		return true;
	}

	switch (event.level)
	{
	case EventLevel::Warn:
	case EventLevel::Error:
	case EventLevel::Fatal:
	case EventLevel::Status:
		return true;
	case EventLevel::Info:
		return event.message.find("op=Connection") != std::string::npos
			|| event.message.find("op=Initialize") != std::string::npos
			|| event.message.find("result=Failed") != std::string::npos
			|| event.message.find("result=Retry") != std::string::npos;
	case EventLevel::Trace:
	case EventLevel::Debug:
	default:
		return false;
	}
}

std::string eventLevelLabel(EventLevel level)
{
	switch (level)
	{
	case EventLevel::Trace:
		return "TRACE";
	case EventLevel::Debug:
		return "DEBUG";
	case EventLevel::Info:
		return "INFO";
	case EventLevel::Status:
		return "STATUS";
	case EventLevel::Warn:
		return "WARN";
	case EventLevel::Error:
		return "ERROR";
	case EventLevel::Fatal:
	default:
		return "FATAL";
	}
}

std::string formatInspectReport(const InspectSessionSnapshot &snapshot, const std::string &binaryName, bool verboseEvents, bool showTree)
{
	std::ostringstream stream;
	stream << "BzPeri Inspect\n";
	stream << "STATUS  PASS\n";
	stream << "Attached to managed session from pid " << snapshot.pid << ".\n";

	appendSection(stream, "FACTS");
	stream << "service: " << snapshot.serviceName << '\n';
	stream << "advertising: " << snapshot.advertisingName;
	if (!snapshot.advertisingShortName.empty())
	{
		stream << " (short: " << snapshot.advertisingShortName << ")";
	}
	stream << '\n';
	stream << "adapter: " << snapshot.adapterPath;
	if (!snapshot.adapterAddress.empty())
	{
		stream << " [" << snapshot.adapterAddress << "]";
	}
	stream << '\n';
	stream << "adapter powered: " << (snapshot.adapterPowered ? "yes" : "no") << '\n';
	stream << "sample services: " << (snapshot.includeSampleServices ? "enabled" : "disabled") << '\n';
	stream << "loop mode: " << (snapshot.manualLoopMode ? "manual" : "threaded") << '\n';
	stream << "object root: " << snapshot.objectRoot << '\n';
	stream << "selected object: " << snapshot.selectedObjectPath << '\n';
	if (!snapshot.writeProbePath.empty())
	{
		stream << "write probe: " << snapshot.writeProbePath << '\n';
	}
	stream << "objects: " << snapshot.objectCount << '\n';
	stream << "active connections: " << snapshot.activeConnections << '\n';
	stream << "advertising active: " << (snapshot.advertisingEnabled ? "yes" : "no") << '\n';
	if (!showTree && !snapshot.objectTreeLines.empty())
	{
		stream << "tree: hidden by default (" << snapshot.objectTreeLines.size() << " lines)\n";
	}

	if (!snapshot.selectedObjectSummary.empty() || !snapshot.selectedObjectLines.empty())
	{
		appendSection(stream, "DETAIL");
		if (!snapshot.selectedObjectSummary.empty())
		{
			stream << snapshot.selectedObjectSummary << '\n';
		}
		for (const auto &line : snapshot.selectedObjectLines)
		{
			stream << line << '\n';
		}
	}

	if (!snapshot.warnings.empty())
	{
		appendSection(stream, "WARNINGS");
		for (const auto &warning : snapshot.warnings)
		{
			stream << "WARN  " << warning << '\n';
		}
	}

	appendSection(stream, "EVENTS");
	std::vector<InspectEvent> visibleEvents;
	for (const auto &event : snapshot.events)
	{
		if (shouldDisplayEvent(event, verboseEvents))
		{
			visibleEvents.push_back(event);
		}
	}
	const auto hiddenByFilterCount = snapshot.events.size() >= visibleEvents.size()
		? snapshot.events.size() - visibleEvents.size()
		: 0U;

	const std::size_t maxVisibleEvents = verboseEvents ? kVerboseInspectEventDisplayCount : kDefaultInspectEventDisplayCount;
	std::size_t hiddenByDisplayLimitCount = 0;
	if (visibleEvents.size() > maxVisibleEvents)
	{
		hiddenByDisplayLimitCount = visibleEvents.size() - maxVisibleEvents;
		visibleEvents.erase(visibleEvents.begin(), visibleEvents.end() - static_cast<std::ptrdiff_t>(maxVisibleEvents));
	}

	if (visibleEvents.empty())
	{
		stream << "INFO  No live events captured yet.\n";
	}
	else
	{
		for (const auto &event : visibleEvents)
		{
			stream << eventLevelLabel(event.level) << "  " << formatTimestamp(event.timestampMs);
			if (!event.component.empty())
			{
				stream << " [" << event.component << "]";
			}
			stream << ' ' << event.message << '\n';
		}
	}
	if (!verboseEvents && hiddenByFilterCount > 0)
	{
		stream << "INFO  " << hiddenByFilterCount << " low-signal events hidden; rerun with "
			<< binaryName << " inspect --live --verbose-events to view them.\n";
	}
	if (hiddenByDisplayLimitCount > 0)
	{
		stream << "INFO  " << hiddenByDisplayLimitCount << " older visible events hidden to keep the terminal readable.\n";
	}
	if (snapshot.droppedEventCount > 0)
	{
		stream << "WARN  Event history truncated; " << snapshot.droppedEventCount << " older events were dropped from the ring buffer.\n";
	}

	if (showTree && !snapshot.objectTreeLines.empty())
	{
		appendSection(stream, "TREE");
		for (const auto &line : snapshot.objectTreeLines)
		{
			stream << line << '\n';
		}
	}

	appendSection(stream, "NEXT");
	if (!showTree && !snapshot.objectTreeLines.empty())
	{
		stream << "run: " << binaryName << " inspect --live --show-tree\n";
	}
	if (!verboseEvents && hiddenByFilterCount > 0)
	{
		stream << "run: " << binaryName << " inspect --live --verbose-events\n";
	}
	stream << "run: Ctrl-C to stop inspecting\n";
	return stream.str();
}

std::string formatMissingSessionReport(const std::string &binaryName)
{
	std::ostringstream stream;
	stream << "BzPeri Inspect\n";
	stream << "STATUS  FAIL\n";
	stream << "No managed BzPeri session found.\n";

	appendSection(stream, "WARNINGS");
	stream << "WARN  " << binaryName << " inspect --live only attaches to sessions launched by "
		<< binaryName << " demo.\n";

	appendSection(stream, "NEXT");
	stream << "run: " << binaryName << " demo\n";
	stream << "run: " << binaryName << " inspect --live\n";
	return stream.str();
}

bool isInspectSessionStale(const InspectSessionSnapshot &snapshot, bool (*isProcessAlive)(int pid)) noexcept
{
	if (snapshot.pid <= 0)
	{
		return true;
	}
	if (isProcessAlive == nullptr)
	{
		return false;
	}
	return !isProcessAlive(snapshot.pid);
}

std::string formatStaleSessionReport(const InspectSessionSnapshot &snapshot, const std::string &binaryName)
{
	std::ostringstream stream;
	stream << "BzPeri Inspect\n";
	stream << "STATUS  FAIL\n";
	stream << "Found a stale managed session file for pid " << snapshot.pid << ".\n";

	appendSection(stream, "WARNINGS");
	stream << "WARN  The previous " << binaryName << " demo session is no longer running, but its inspect state file still exists.\n";

	appendSection(stream, "NEXT");
	stream << "run: " << binaryName << " demo\n";
	stream << "run: " << binaryName << " inspect --live\n";
	return stream.str();
}

} // namespace bzp::standalone
