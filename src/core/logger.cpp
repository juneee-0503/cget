#include "cget/core/logger.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <fstream>
#include <utility>

#include "cget/core/types.h"

namespace cget {
namespace {

int severity(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return 0;
        case LogLevel::Debug: return 1;
        case LogLevel::Info: return 2;
        case LogLevel::Warn: return 3;
        case LogLevel::Error: return 4;
        case LogLevel::Fatal: return 5;
        case LogLevel::Off: return 6;
    }
    return 6;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

}  // namespace

Logger::Logger(FileSystemService fileSystem) : fileSystem_(std::move(fileSystem)) {}

Logger::Logger(FileSystemService fileSystem, LoggerOptions options)
    : fileSystem_(std::move(fileSystem)), options_(options) {}

void Logger::configure(LoggerOptions options) {
    std::lock_guard lock(mutex_);
    options_ = options;
}

void Logger::trace(const std::string& message) const {
    write(LogLevel::Trace, message);
}

void Logger::debug(const std::string& message) const {
    write(LogLevel::Debug, message);
}

void Logger::info(const std::string& message) const {
    write(LogLevel::Info, message);
}

void Logger::warn(const std::string& message) const {
    write(LogLevel::Warn, message);
}

void Logger::error(const std::string& message) const {
    write(LogLevel::Error, message);
}

void Logger::fatal(const std::string& message) const {
    write(LogLevel::Fatal, message);
}

bool Logger::shouldWrite(LogLevel level) const {
    return severity(level) >= severity(options_.level) && options_.level != LogLevel::Off;
}

void Logger::rotateIfNeeded() const {
    if (options_.maxFileBytes == 0 || options_.maxRotatedFiles == 0) {
        return;
    }
    const auto logPath = fileSystem_.logsDir() / "cget.log";
    std::error_code ec;
    if (!std::filesystem::exists(logPath, ec) || std::filesystem::file_size(logPath, ec) < options_.maxFileBytes) {
        return;
    }

    for (std::uint32_t index = options_.maxRotatedFiles; index > 0; --index) {
        const auto current = index == 1 ? logPath : std::filesystem::path(logPath.string() + "." + std::to_string(index - 1));
        const auto next = std::filesystem::path(logPath.string() + "." + std::to_string(index));
        if (!std::filesystem::exists(current, ec)) {
            continue;
        }
        std::filesystem::remove(next, ec);
        ec.clear();
        std::filesystem::rename(current, next, ec);
        ec.clear();
    }
}

void Logger::write(LogLevel level, const std::string& message) const {
    std::lock_guard lock(mutex_);
    if (!shouldWrite(level)) {
        return;
    }
    try {
        fileSystem_.ensureDirectories();
        rotateIfNeeded();
        std::ofstream output(fileSystem_.logsDir() / "cget.log", std::ios::app);
        const auto line = formatTimestamp(std::chrono::system_clock::now()) + " [" + toString(level) + "] " + message;
        output << line << '\n';
        if (options_.console) {
            std::cerr << line << '\n';
        }
    } catch (...) {
        // Logging must never break download or CLI control paths.
    }
}

std::string toString(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        case LogLevel::Off: return "OFF";
    }
    return "OFF";
}

LogLevel logLevelFromString(const std::string& value) {
    const auto normalized = lower(value);
    if (normalized == "trace") return LogLevel::Trace;
    if (normalized == "debug") return LogLevel::Debug;
    if (normalized == "info") return LogLevel::Info;
    if (normalized == "warn" || normalized == "warning") return LogLevel::Warn;
    if (normalized == "error") return LogLevel::Error;
    if (normalized == "fatal") return LogLevel::Fatal;
    if (normalized == "off" || normalized == "none") return LogLevel::Off;
    return LogLevel::Info;
}

}  // namespace cget
