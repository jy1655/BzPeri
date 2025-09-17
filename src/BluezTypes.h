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
// BlueZ D-Bus types, enums, and result handling for modern BlueZ integration
//
// >>
// >>>  DISCUSSION
// >>
//
// This file provides standardized types and error handling for BlueZ D-Bus operations.
// It includes proper GError mapping, result types, and feature detection capabilities.
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#pragma once

#include <gio/gio.h>
#include <string>
#include <variant>
#include <vector>
#include <optional>

namespace ggk {

// BlueZ operation result types
enum class BluezError
{
	Success = 0,
	PermissionDenied,
	NotReady,
	NotSupported,
	InProgress,
	Failed,
	Timeout,
	InvalidArgs,
	AlreadyExists,
	NotFound,
	ConnectionFailed,
	Unknown
};

// Result type for BlueZ operations
template<typename T = void>
class BluezResult
{
public:
	BluezResult() : error_(BluezError::Success) {}
	BluezResult(BluezError error, std::string message = "")
		: error_(error), errorMessage_(std::move(message)) {}
	BluezResult(T&& value) : value_(std::move(value)), error_(BluezError::Success) {}
	BluezResult(const T& value) : value_(value), error_(BluezError::Success) {}

	bool isSuccess() const { return error_ == BluezError::Success; }
	bool hasError() const { return error_ != BluezError::Success; }

	BluezError error() const { return error_; }
	const std::string& errorMessage() const { return errorMessage_; }

	const T& value() const { return value_; }
	T& value() { return value_; }

	// Convert GError to BluezError
	static BluezResult<T> fromGError(GError* error);
	static BluezError mapGErrorDomain(GError* error);

private:
	T value_{};
	BluezError error_;
	std::string errorMessage_;
};

// Specialized template for void
template<>
class BluezResult<void>
{
public:
	BluezResult() : error_(BluezError::Success) {}
	BluezResult(BluezError error, std::string message = "")
		: error_(error), errorMessage_(std::move(message)) {}

	bool isSuccess() const { return error_ == BluezError::Success; }
	bool hasError() const { return error_ != BluezError::Success; }

	BluezError error() const { return error_; }
	const std::string& errorMessage() const { return errorMessage_; }

	static BluezResult<void> fromGError(GError* error);

private:
	BluezError error_;
	std::string errorMessage_;
};

// Adapter information structure
struct AdapterInfo
{
	std::string path;
	std::string address;
	std::string name;
	std::string alias;
	bool powered = false;
	bool discoverable = false;
	bool connectable = false;
	bool pairable = false;
	bool discovering = false;
	std::vector<std::string> uuids;
};

// Device information structure
struct DeviceInfo
{
	std::string path;
	std::string address;
	std::string name;
	std::string alias;
	bool connected = false;
	bool paired = false;
	bool trusted = false;
	int16_t rssi = 0;
	std::vector<std::string> uuids;
};

// BlueZ feature capabilities
struct BluezCapabilities
{
	bool hasLEAdvertisingManager = false;
	bool hasGattManager = false;
	bool hasAcquireWrite = false;
	bool hasAcquireNotify = false;
	bool hasExtendedAdvertising = false;
	std::string bluezVersion;
};

// Retry policy configuration
struct RetryPolicy
{
	int maxAttempts = 3;
	int baseDelayMs = 100;
	int maxDelayMs = 5000;
	double backoffMultiplier = 2.0;

	// Calculate delay for attempt number (0-based)
	int getDelayMs(int attempt) const;
};

// Operation timeout configuration
struct TimeoutConfig
{
	int defaultTimeoutMs = 5000;
	int propertyTimeoutMs = 3000;
	int connectionTimeoutMs = 10000;
	int discoveryTimeoutMs = 30000;
};

// Convert BluezError to human-readable string
std::string bluezErrorToString(BluezError error);

// Map D-Bus error names to BluezError
BluezError mapDBusErrorName(const std::string& errorName);

// Check if error is retryable
bool isRetryableError(BluezError error);

} // namespace ggk