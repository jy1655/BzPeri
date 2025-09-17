#pragma once

#include "Logger.h"
#include <sstream>
#include <string>

namespace ggk {

// Modern structured logging for BlueZ operations
// Provides consistent format: [Component] op=Operation prop=Property path=Path result=Result
class StructuredLogger {
public:
    StructuredLogger(const std::string& component) : component_(component) {}

    struct LogEntry {
        std::string component;
        std::string operation;
        std::string property;
        std::string path;
        std::string result;
        std::string error;
        std::string extra;

        std::string format() const {
            std::ostringstream oss;
            oss << "[" << component << "]";

            if (!operation.empty()) oss << " op=" << operation;
            if (!property.empty()) oss << " prop=" << property;
            if (!path.empty()) oss << " path=" << path;
            if (!result.empty()) oss << " result=" << result;
            if (!error.empty()) oss << " err=" << error;
            if (!extra.empty()) oss << " " << extra;

            return oss.str();
        }
    };

    // Fluent interface for building log entries
    class EntryBuilder {
    public:
        EntryBuilder(const std::string& component) { entry_.component = component; }

        EntryBuilder& op(const std::string& operation) { entry_.operation = operation; return *this; }
        EntryBuilder& prop(const std::string& property) { entry_.property = property; return *this; }
        EntryBuilder& path(const std::string& path) { entry_.path = path; return *this; }
        EntryBuilder& result(const std::string& result) { entry_.result = result; return *this; }
        EntryBuilder& error(const std::string& error) { entry_.error = error; return *this; }
        EntryBuilder& extra(const std::string& extra) { entry_.extra = extra; return *this; }

        void info() { Logger::info(entry_.format().c_str()); }
        void warn() { Logger::warn(entry_.format().c_str()); }
        void error() { Logger::error(entry_.format().c_str()); }
        void debug() { Logger::debug(entry_.format().c_str()); }

    private:
        LogEntry entry_;
    };

    EntryBuilder log() { return EntryBuilder(component_); }

    // Convenience methods for common BlueZ operations
    void logAdapterOperation(const std::string& op, const std::string& prop, const std::string& path, bool success, const std::string& error = "") {
        log().op(op).prop(prop).path(path).result(success ? "Success" : "Failed").error(error).info();
    }

    void logRetryAttempt(const std::string& op, int attempt, int maxAttempts, const std::string& error = "") {
        std::ostringstream extra;
        extra << "attempt=" << attempt << "/" << maxAttempts;
        log().op(op).result("Retry").error(error).extra(extra.str()).debug();
    }

    void logConnectionEvent(const std::string& devicePath, bool connected) {
        log().op("Connection").path(devicePath).result(connected ? "Connected" : "Disconnected").info();
    }

private:
    std::string component_;
};

// Global structured loggers for major components
extern StructuredLogger bluezLogger;
extern StructuredLogger gattLogger;
extern StructuredLogger dbusLogger;

} // namespace ggk