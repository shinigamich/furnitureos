// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace fos::core {

/// Broad error categories. Modules add detail through Error::message / Error::context.
enum class ErrorCode {
    InvalidArgument,
    NotFound,
    AlreadyExists,
    ParseError,
    CyclicDependency,
    ConstraintViolation,
    IoError,
    CorruptData,
    UnsupportedVersion,
    Unsupported,
    Cancelled,
    Internal,
};

const char* toString(ErrorCode code) noexcept;

/// Structured error. `message` is a complete, human-readable (English) sentence;
/// `context` optionally names the entity/parameter/file the error refers to.
struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string message;
    std::string context;
};

/// Minimal expected-like result type (C++20 baseline; std::expected is C++23).
/// Expected failures are reported through Result; exceptions are reserved for
/// programming errors and out-of-memory.
template <class T> class [[nodiscard]] Result {
public:
    Result(T value) : v_(std::move(value)) {}
    Result(Error error) : v_(std::move(error)) {}

    bool ok() const noexcept { return std::holds_alternative<T>(v_); }
    explicit operator bool() const noexcept { return ok(); }

    T& value() & { assert(ok()); return std::get<T>(v_); }
    const T& value() const& { assert(ok()); return std::get<T>(v_); }
    T&& value() && { assert(ok()); return std::get<T>(std::move(v_)); }
    const Error& error() const { assert(!ok()); return std::get<Error>(v_); }

    T valueOr(T fallback) const& { return ok() ? std::get<T>(v_) : std::move(fallback); }

private:
    std::variant<T, Error> v_;
};

template <> class [[nodiscard]] Result<void> {
public:
    Result() = default;
    Result(Error error) : err_(std::move(error)), ok_(false) {}
    bool ok() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }
    const Error& error() const { assert(!ok_); return err_; }

private:
    Error err_;
    bool ok_ = true;
};

inline Error makeError(ErrorCode code, std::string message, std::string context = {}) {
    return Error{code, std::move(message), std::move(context)};
}

inline const char* toString(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::NotFound: return "NotFound";
    case ErrorCode::AlreadyExists: return "AlreadyExists";
    case ErrorCode::ParseError: return "ParseError";
    case ErrorCode::CyclicDependency: return "CyclicDependency";
    case ErrorCode::ConstraintViolation: return "ConstraintViolation";
    case ErrorCode::IoError: return "IoError";
    case ErrorCode::CorruptData: return "CorruptData";
    case ErrorCode::UnsupportedVersion: return "UnsupportedVersion";
    case ErrorCode::Unsupported: return "Unsupported";
    case ErrorCode::Cancelled: return "Cancelled";
    case ErrorCode::Internal: return "Internal";
    }
    return "Unknown";
}

} // namespace fos::core
