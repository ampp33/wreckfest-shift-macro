#include "logging/Logger.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace vcontroller {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

bool Logger::openFile(const std::filesystem::path& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::FILE* file = _wfopen(path.c_str(), L"w");
    if (file == nullptr) {
        return false;
    }
    if (file_ != nullptr) {
        std::fclose(file_);
    }
    file_ = file;
    return true;
}

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

namespace {
constexpr std::string_view levelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Debug:   return "DEBUG";
    }
    return "?????";
}
} // namespace

void Logger::log(LogLevel level, std::string_view message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (level > level_) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowTimeT = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch()) % 1000;

    std::tm tmBuf{};
    localtime_s(&tmBuf, &nowTimeT);

    std::array<char, 32> timeBuf{};
    std::strftime(timeBuf.data(), timeBuf.size(), "%H:%M:%S", &tmBuf);

    std::array<char, 1024> line{};
    std::snprintf(line.data(), line.size(), "[%s.%03ld] [%.*s] %.*s\n", timeBuf.data(),
                  static_cast<long>(ms.count()), static_cast<int>(levelTag(level).size()),
                  levelTag(level).data(), static_cast<int>(message.size()), message.data());

    if (file_ != nullptr) {
        std::fputs(line.data(), file_);
        // Flushed per line so the log survives the game crashing or being
        // killed, which is exactly when you'd want to read it.
        std::fflush(file_);
    } else {
        OutputDebugStringA(line.data());
    }
}

} // namespace vcontroller
