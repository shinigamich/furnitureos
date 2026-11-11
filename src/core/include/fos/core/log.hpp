// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <functional>
#include <string_view>

namespace fos::core {

enum class LogLevel { Debug, Info, Warning, Error };

using LogSink = std::function<void(LogLevel, std::string_view category, std::string_view message)>;

/// Replace the process-wide sink (default: stderr). Thread-safe. Pass nullptr to restore default.
void setLogSink(LogSink sink);
void setLogLevel(LogLevel minimum);
void log(LogLevel level, std::string_view category, std::string_view message);

inline void logDebug(std::string_view c, std::string_view m) { log(LogLevel::Debug, c, m); }
inline void logInfo(std::string_view c, std::string_view m) { log(LogLevel::Info, c, m); }
inline void logWarning(std::string_view c, std::string_view m) { log(LogLevel::Warning, c, m); }
inline void logError(std::string_view c, std::string_view m) { log(LogLevel::Error, c, m); }

} // namespace fos::core
