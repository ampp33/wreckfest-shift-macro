#pragma once

#include <mutex>
#include <string>
#include <string_view>

namespace vwheel {

/// Severity levels, ordered from least to most verbose.
enum class LogLevel {
    Error = 0,
    Warning = 1,
    Info = 2,
    Debug = 3,
};

/// Minimal thread-safe console logger.
///
/// A single process-wide instance is used throughout the daemon. All public
/// methods are safe to call concurrently from multiple threads (the clutch
/// worker thread and the main epoll thread both log).
class Logger {
public:
    static Logger& instance();

    /// Sets the minimum level that will be printed. Anything more verbose
    /// than this threshold is silently discarded.
    void setLevel(LogLevel level);
    LogLevel level() const;

    void error(std::string_view message);
    void warning(std::string_view message);
    void info(std::string_view message);
    void debug(std::string_view message);

    /// Parses a config string ("error", "warning", "info", "debug") into a
    /// LogLevel. Falls back to Info on an unrecognized value.
    static LogLevel parseLevel(std::string_view text);

private:
    Logger() = default;

    void log(LogLevel level, std::string_view message);

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
};

} // namespace vwheel
