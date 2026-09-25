#pragma once

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace vcontroller {

/// Severity levels, ordered from least to most verbose.
enum class LogLevel {
    Error = 0,
    Warning = 1,
    Info = 2,
    Debug = 3,
};

/// Minimal thread-safe file logger.
///
/// A single process-wide instance is used throughout the plugin. All public
/// methods are safe to call concurrently from multiple threads (the game's
/// input thread, the plugin's tick thread, and the clutch worker thread
/// all log). Until openFile() succeeds, messages go to OutputDebugString
/// (visible with WINEDEBUG=+debugstr or a Windows debugger).
class Logger {
public:
    static Logger& instance();

    /// Starts writing to `path`, truncating it. The plugin has no console,
    /// so this is where every message ends up.
    bool openFile(const std::filesystem::path& path);

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
    std::FILE* file_ = nullptr;
};

} // namespace vcontroller
