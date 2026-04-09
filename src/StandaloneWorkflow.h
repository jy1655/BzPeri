#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace bzp::standalone {

enum class Verdict
{
	Pass,
	Warn,
	Fail,
};

struct DoctorCheck
{
	std::string label;
	Verdict verdict = Verdict::Fail;
	std::string detail;
};

struct DoctorSnapshot
{
	bool systemBusReachable = false;
	std::string systemBusDetail;
	bool bluezServiceReachable = false;
	std::string bluezServiceDetail;
	bool serviceNameAvailable = false;
	std::string serviceNameDetail;
	bool adapterProbeSucceeded = false;
	std::string adapterProbeDetail;
	bool poweredAdapterAvailable = false;
	std::string adapterSummary;
	std::vector<std::string> adapterLines;
	bool policyInstalled = false;
	std::string policyPath;
	bool experimentalHelperAvailable = false;
	bool experimentalModeEnabled = false;
	std::string experimentalHelperPath;
	std::string experimentalDetail;
};

struct DoctorReport
{
	Verdict overall = Verdict::Fail;
	std::vector<DoctorCheck> checks;
	std::vector<std::string> adapterLines;
	std::vector<std::string> nextCommands;
};

class DoctorProbe
{
public:
	virtual ~DoctorProbe() = default;

	virtual bool checkSystemBus(std::string *detailOut) const = 0;
	virtual bool checkBluezService(std::string *detailOut) const = 0;
	virtual bool checkServiceOwnership(const std::string &ownedName, std::string *detailOut) const = 0;
	virtual bool probeAdapter(const std::string &preferredAdapter,
		bool *poweredAdapterAvailableOut,
		std::string *summaryOut,
		std::vector<std::string> *adapterLinesOut,
		std::string *detailOut) const = 0;
	virtual bool checkPolicy(std::string *pathOut) const = 0;
	virtual bool checkExperimentalHelper(std::string *pathOut, bool *modeEnabledOut, std::string *detailOut) const = 0;
};

DoctorSnapshot collectDoctorSnapshot(const DoctorProbe &probe, const std::string &preferredAdapter, const std::string &ownedName = "com.bzperi");
DoctorReport evaluateDoctorSnapshot(const DoctorSnapshot &snapshot, const std::string &binaryName);
int exitCodeForDoctorReport(const DoctorReport &report) noexcept;
std::string formatDoctorReport(const DoctorReport &report, const std::string &binaryName);
std::string verdictLabel(Verdict verdict);

enum class EventLevel
{
	Trace,
	Debug,
	Info,
	Status,
	Warn,
	Error,
	Fatal,
};

struct InspectEvent
{
	long long timestampMs = 0;
	EventLevel level = EventLevel::Info;
	std::string component;
	std::string message;
};

struct InspectSessionSnapshot
{
	int formatVersion = 2;
	int pid = 0;
	long long updatedAtMs = 0;
	std::string serviceName;
	std::string advertisingName;
	std::string advertisingShortName;
	std::string sampleNamespace;
	std::string objectRoot;
	std::string selectedObjectPath;
	std::string writeProbePath;
	std::string selectedObjectSummary;
	std::string adapterPath;
	std::string adapterAddress;
	std::string adapterAlias;
	bool adapterPowered = false;
	bool includeSampleServices = true;
	bool manualLoopMode = false;
	bool advertisingEnabled = false;
	int activeConnections = 0;
	int objectCount = 0;
	int runState = 0;
	int health = 0;
	std::size_t droppedEventCount = 0;
	std::vector<std::string> objectTreeLines;
	std::vector<std::string> selectedObjectLines;
	std::vector<std::string> warnings;
	std::vector<InspectEvent> events;
};

class InspectSessionStore
{
public:
	explicit InspectSessionStore(std::string path = defaultSessionPath(), std::size_t eventLimit = 64);

	const std::string &path() const noexcept { return path_; }
	std::size_t eventLimit() const noexcept { return eventLimit_; }

	bool save(const InspectSessionSnapshot &snapshot, std::string *errorOut = nullptr) const;
	std::optional<InspectSessionSnapshot> load(std::string *errorOut = nullptr) const;
	bool clear(std::string *errorOut = nullptr) const;

	static std::string defaultSessionPath();

private:
	std::string path_;
	std::size_t eventLimit_;
};

long long currentTimeMs() noexcept;
void appendInspectEvent(InspectSessionSnapshot &snapshot, const InspectEvent &event, std::size_t eventLimit);
bool shouldDisplayEvent(const InspectEvent &event, bool verboseEvents) noexcept;
std::string eventLevelLabel(EventLevel level);
std::string formatInspectReport(const InspectSessionSnapshot &snapshot, const std::string &binaryName, bool verboseEvents, bool showTree);
std::string formatMissingSessionReport(const std::string &binaryName);
bool isInspectSessionStale(const InspectSessionSnapshot &snapshot, bool (*isProcessAlive)(int pid)) noexcept;
std::string formatStaleSessionReport(const InspectSessionSnapshot &snapshot, const std::string &binaryName);

} // namespace bzp::standalone
