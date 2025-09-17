// Copyright (c) 2025 JaeYoung Hwang (BzPeri Project)
//
// This file is part of BzPeri.
//
// Licensed under MIT License (see LICENSE file)
//
// Modern C++20 error handling with std::expected

#pragma once

#include <system_error>
#include <expected>
#include <string_view>
#include <source_location>
#include <stacktrace>
#include <format>

#include "config.h"

namespace bzp::error {

// Comprehensive error categories for BzPeri
enum class BzPeriErrorCode : int
{
    Success = 0,

    // Initialization errors (1-99)
    InitializationFailed = 1,
    ConfigurationInvalid = 2,
    DependencyMissing = 3,
    PermissionDenied = 4,
    ResourceExhausted = 5,

    // BlueZ/D-Bus errors (100-199)
    BlueZNotAvailable = 100,
    DBusConnectionFailed = 101,
    DBusPermissionDenied = 102,
    BlueZVersionIncompatible = 103,
    AdapterNotFound = 104,
    AdapterNotPowered = 105,
    ServiceRegistrationFailed = 106,
    CharacteristicRegistrationFailed = 107,
    DescriptorRegistrationFailed = 108,

    // GATT operation errors (200-299)
    GattServiceNotFound = 200,
    GattCharacteristicNotFound = 201,
    GattDescriptorNotFound = 202,
    GattInvalidUuid = 203,
    GattInvalidProperty = 204,
    GattReadFailed = 205,
    GattWriteFailed = 206,
    GattNotifyFailed = 207,
    GattIndicateFailed = 208,
    GattMtuExceeded = 209,
    GattSecurityViolation = 210,

    // Connection errors (300-399)
    ConnectionFailed = 300,
    ConnectionTimeout = 301,
    ConnectionLost = 302,
    ConnectionRejected = 303,
    PairingFailed = 304,
    AuthenticationFailed = 305,
    EncryptionFailed = 306,

    // Data operation errors (400-499)
    DataValidationFailed = 400,
    DataConversionFailed = 401,
    DataCorrupted = 402,
    DataTooLarge = 403,
    DataProviderError = 404,

    // System errors (500-599)
    SystemResourceUnavailable = 500,
    SystemCallFailed = 501,
    ThreadingError = 502,
    MemoryAllocationFailed = 503,
    FileOperationFailed = 504,

    // Internal errors (600-699)
    InternalStateCorrupted = 600,
    UnexpectedNullPointer = 601,
    LogicError = 602,
    NotImplemented = 603,
    Deprecated = 604,

    // Generic errors (700+)
    Unknown = 700,
    Timeout = 701,
    Cancelled = 702,
    InvalidArgument = 703,
    OutOfRange = 704
};

// Error category for BzPeri errors
class BzPeriErrorCategory : public std::error_category
{
public:
    [[nodiscard]] const char* name() const noexcept override
    {
        return "bzperi";
    }

    [[nodiscard]] std::string message(int condition) const override;

    [[nodiscard]] bool equivalent(const std::error_code& code, int condition) const noexcept override;
};

// Get the singleton error category
[[nodiscard]] const BzPeriErrorCategory& bzperi_category() noexcept;

// Helper function to create error codes
[[nodiscard]] inline std::error_code make_error_code(BzPeriErrorCode e) noexcept
{
    return {static_cast<int>(e), bzperi_category()};
}

// Enhanced error information with context
struct ErrorContext
{
    std::error_code error;
    std::string_view component;
    std::string_view operation;
    std::source_location location;
    std::string details;

#if __cpp_lib_stacktrace >= 202011L
    std::stacktrace trace;
#endif

    ErrorContext(std::error_code ec,
                std::string_view comp = {},
                std::string_view op = {},
                std::source_location loc = std::source_location::current(),
                std::string det = {})
        : error(ec), component(comp), operation(op), location(loc), details(std::move(det))
    {
#if __cpp_lib_stacktrace >= 202011L
        trace = std::stacktrace::current();
#endif
    }

    [[nodiscard]] std::string toString() const;
};

// Result type for operations that can fail
template<typename T>
using Result = std::expected<T, ErrorContext>;

// Void result for operations that don't return values
using VoidResult = Result<void>;

// Convenience macros for error creation
#define BZP_ERROR(code) ::bzp::error::ErrorContext{::bzp::error::make_error_code(::bzp::error::BzPeriErrorCode::code)}

#define BZP_ERROR_WITH_DETAILS(code, details) \
    ::bzp::error::ErrorContext{::bzp::error::make_error_code(::bzp::error::BzPeriErrorCode::code), {}, {}, std::source_location::current(), details}

#define BZP_ERROR_COMPONENT(code, component, operation) \
    ::bzp::error::ErrorContext{::bzp::error::make_error_code(::bzp::error::BzPeriErrorCode::code), component, operation}

// Error handling utilities
namespace utils {
    // Convert system errno to our error codes
    [[nodiscard]] BzPeriErrorCode errnoToBzPeriError(int err) noexcept;

    // Convert D-Bus error names to our error codes
    [[nodiscard]] BzPeriErrorCode dbusErrorToBzPeriError(std::string_view errorName) noexcept;

    // Format error messages with context
    [[nodiscard]] std::string formatError(const ErrorContext& ctx) noexcept;

    // Log error with appropriate level based on severity
    void logError(const ErrorContext& ctx) noexcept;

    // Check if error is recoverable
    [[nodiscard]] bool isRecoverable(BzPeriErrorCode code) noexcept;

    // Get error severity level
    enum class Severity { Info, Warning, Error, Critical, Fatal };
    [[nodiscard]] Severity getSeverity(BzPeriErrorCode code) noexcept;
}

// Exception class for when exceptions are needed (discouraged in favor of Result<T>)
class BzPeriException : public std::system_error
{
public:
    explicit BzPeriException(const ErrorContext& ctx)
        : std::system_error(ctx.error), context_(ctx)
    {
    }

    [[nodiscard]] const ErrorContext& context() const noexcept { return context_; }

private:
    ErrorContext context_;
};

// RAII error scope for automatic error logging
class ErrorScope
{
public:
    explicit ErrorScope(std::string_view component, std::string_view operation = {}) noexcept
        : component_(component), operation_(operation)
    {
    }

    ~ErrorScope() noexcept
    {
        if (hasError_) {
            utils::logError(lastError_);
        }
    }

    void recordError(const ErrorContext& ctx) noexcept
    {
        hasError_ = true;
        lastError_ = ctx;
    }

    template<typename T>
    Result<T> checkResult(Result<T>&& result) noexcept
    {
        if (!result.has_value()) {
            recordError(result.error());
        }
        return std::move(result);
    }

private:
    std::string_view component_;
    std::string_view operation_;
    bool hasError_ = false;
    ErrorContext lastError_{make_error_code(BzPeriErrorCode::Success)};
};

} // namespace bzp::error

// Enable std::error_code support
namespace std {
    template<>
    struct is_error_code_enum<bzp::error::BzPeriErrorCode> : true_type {};
}