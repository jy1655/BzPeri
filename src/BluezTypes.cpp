// Copyright (c) 2025 BzPeri Contributors
// Licensed under MIT License (see LICENSE file)
//
// BlueZ type definitions and utilities

#include <bzp/BluezTypes.h>
#include <bzp/Logger.h>
#include <algorithm>
#include <cmath>
#include <random>

namespace bzp {

// RetryPolicy::getDelayMs implementation with jitter
int RetryPolicy::getDelayMs(int attempt) const
{
	if (attempt <= 0) return 0;

	double delay = baseDelayMs * std::pow(backoffMultiplier, attempt - 1);
	delay = std::min(delay, static_cast<double>(maxDelayMs));

	// Add jitter: ±30% uniform randomization to avoid thundering herd
	static thread_local std::random_device rd;
	static thread_local std::mt19937 gen(rd());
	std::uniform_real_distribution<> jitter(0.7, 1.3);

	delay *= jitter(gen);
	return static_cast<int>(std::max(delay, 1.0)); // Minimum 1ms delay
}

// Convert BluezError to human-readable string
std::string bluezErrorToString(BluezError error)
{
	switch (error)
	{
		case BluezError::Success: return "Success";
		case BluezError::PermissionDenied: return "Permission denied - check polkit rules or run with sudo";
		case BluezError::NotReady: return "BlueZ service not ready - check bluetoothd status";
		case BluezError::NotSupported: return "Operation not supported by BlueZ or hardware";
		case BluezError::InProgress: return "Operation already in progress";
		case BluezError::Failed: return "Operation failed";
		case BluezError::Timeout: return "Operation timed out";
		case BluezError::InvalidArgs: return "Invalid arguments provided";
		case BluezError::AlreadyExists: return "Resource already exists";
		case BluezError::NotFound: return "Resource not found";
		case BluezError::ConnectionFailed: return "Connection failed";
		case BluezError::Unknown:
		default: return "Unknown error";
	}
}

// Map D-Bus error names to BluezError
BluezError mapDBusErrorName(const std::string& errorName)
{
	if (errorName.find("PermissionDenied") != std::string::npos ||
		errorName.find("AccessDenied") != std::string::npos)
		return BluezError::PermissionDenied;

	if (errorName.find("NotReady") != std::string::npos)
		return BluezError::NotReady;

	if (errorName.find("NotSupported") != std::string::npos ||
		errorName.find("NotImplemented") != std::string::npos)
		return BluezError::NotSupported;

	if (errorName.find("InProgress") != std::string::npos)
		return BluezError::InProgress;

	if (errorName.find("Failed") != std::string::npos)
		return BluezError::Failed;

	if (errorName.find("InvalidArguments") != std::string::npos ||
		errorName.find("InvalidArgs") != std::string::npos)
		return BluezError::InvalidArgs;

	if (errorName.find("AlreadyExists") != std::string::npos)
		return BluezError::AlreadyExists;

	if (errorName.find("DoesNotExist") != std::string::npos ||
		errorName.find("NotFound") != std::string::npos)
		return BluezError::NotFound;

	if (errorName.find("Timeout") != std::string::npos)
		return BluezError::Timeout;

	return BluezError::Unknown;
}

// Check if error is retryable
bool isRetryableError(BluezError error)
{
	switch (error)
	{
		case BluezError::InProgress:
		case BluezError::NotReady:
		case BluezError::Timeout:
		case BluezError::Failed:  // Some failed operations can be retried
			return true;

		case BluezError::PermissionDenied:
		case BluezError::NotSupported:
		case BluezError::InvalidArgs:
		case BluezError::AlreadyExists:
			return false;

		default:
			return false;
	}
}

// Template specialization for GError mapping
template<typename T>
BluezResult<T> BluezResult<T>::fromGError(GError* error)
{
	if (!error)
		return BluezResult<T>();  // Success

	BluezError mappedError = mapDBusErrorName(error->message ? error->message : "");
	std::string message = error->message ? error->message : "Unknown error";

	Logger::warn(SSTR << "D-Bus error: " << message << " (mapped to: " << bluezErrorToString(mappedError) << ")");

	return BluezResult<T>(mappedError, message);
}

template<typename T>
BluezError BluezResult<T>::mapGErrorDomain(GError* error)
{
	if (!error)
		return BluezError::Success;

	// Check GIO/D-Bus specific error domains
	if (error->domain == G_IO_ERROR)
	{
		switch (error->code)
		{
			case G_IO_ERROR_PERMISSION_DENIED:
				return BluezError::PermissionDenied;
			case G_IO_ERROR_TIMED_OUT:
				return BluezError::Timeout;
			case G_IO_ERROR_NOT_FOUND:
				return BluezError::NotFound;
			case G_IO_ERROR_FAILED:
				return BluezError::Failed;
			default:
				break;
		}
	}
	else if (error->domain == G_DBUS_ERROR)
	{
		switch (error->code)
		{
			case G_DBUS_ERROR_ACCESS_DENIED:
				return BluezError::PermissionDenied;
			case G_DBUS_ERROR_TIMEOUT:
				return BluezError::Timeout;
			case G_DBUS_ERROR_UNKNOWN_METHOD:
			case G_DBUS_ERROR_UNKNOWN_INTERFACE:
				return BluezError::NotSupported;
			case G_DBUS_ERROR_INVALID_ARGS:
				return BluezError::InvalidArgs;
			case G_DBUS_ERROR_FAILED:
				return BluezError::Failed;
			default:
				break;
		}
	}

	// Fallback to message-based mapping
	return mapDBusErrorName(error->message ? error->message : "");
}

// Specialization for void result
BluezResult<void> BluezResult<void>::fromGError(GError* error)
{
	if (!error)
		return BluezResult<void>();  // Success

	BluezError mappedError = mapDBusErrorName(error->message ? error->message : "");
	std::string message = error->message ? error->message : "Unknown error";

	Logger::warn(SSTR << "D-Bus error: " << message << " (mapped to: " << bluezErrorToString(mappedError) << ")");

	return BluezResult<void>(mappedError, message);
}

} // namespace bzp

// Explicit template instantiations to ensure symbols are emitted from this TU
// for pointer specializations used across the library.
template bzp::BluezResult<GVariant*> bzp::BluezResult<GVariant*>::fromGError(GError*);
template bzp::BluezError bzp::BluezResult<GVariant*>::mapGErrorDomain(GError*);
