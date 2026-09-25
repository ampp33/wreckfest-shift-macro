// In-memory replacement for src/logging/Logger.cpp (which needs the Win32
// API). Implements the same Logger interface, capturing lines for
// testlog::lines() instead of writing a file.

#include "TestLogger.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "logging/Logger.h"

namespace {

std::mutex g_mutex;
std::vector<std::string> g_lines;
auto g_origin = std::chrono::steady_clock::now();

} // namespace

namespace testlog {

std::vector<std::string> lines() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_lines;
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_lines.clear();
    g_origin = std::chrono::steady_clock::now();
}

} // namespace testlog

namespace vcontroller {

Logger& Logger::instance() {
    // Debug so tests capture the full trace, whatever a config says.
    static Logger* logger = [] {
        auto* created = new Logger;
        created->level_ = LogLevel::Debug;
        return created;
    }();
    return *logger;
}

bool Logger::openFile(const std::filesystem::path&) { return true; }

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::error(std::string_view message) { log(LogLevel::Error, message); }
void Logger::warning(std::string_view message) { log(LogLevel::Warning, message); }
void Logger::info(std::string_view message) { log(LogLevel::Info, message); }
void Logger::debug(std::string_view message) { log(LogLevel::Debug, message); }

LogLevel Logger::parseLevel(std::string_view text) {
    if (text == "error") return LogLevel::Error;
    if (text == "warning" || text == "warn") return LogLevel::Warning;
    if (text == "debug") return LogLevel::Debug;
    return LogLevel::Info;
}

void Logger::log(LogLevel, std::string_view message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - g_origin)
                          .count();
    char prefix[32];
    std::snprintf(prefix, sizeof prefix, "[%8.2fms] ", ms);
    g_lines.push_back(prefix + std::string(message));
}

} // namespace vcontroller
