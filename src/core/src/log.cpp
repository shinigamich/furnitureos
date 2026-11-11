// SPDX-License-Identifier: AGPL-3.0-only
#include "fos/core/log.hpp"

#include <atomic>
#include <iostream>
#include <mutex>

namespace fos::core {
namespace {
std::mutex g_mutex;
LogSink g_sink;
std::atomic<LogLevel> g_level{LogLevel::Info};

const char* name(LogLevel l) {
    switch (l) {
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warning: return "warning";
    case LogLevel::Error: return "error";
    }
    return "?";
}
} // namespace

void setLogSink(LogSink sink) {
    std::lock_guard lock(g_mutex);
    g_sink = std::move(sink);
}

void setLogLevel(LogLevel minimum) { g_level.store(minimum); }

void log(LogLevel level, std::string_view category, std::string_view message) {
    if (level < g_level.load()) return;
    std::lock_guard lock(g_mutex);
    if (g_sink) {
        g_sink(level, category, message);
    } else {
        std::cerr << '[' << name(level) << "] " << category << ": " << message << '\n';
    }
}

} // namespace fos::core
