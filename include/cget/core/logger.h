#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "cget/filesystem/filesystem_service.h"

namespace cget {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Off
};

struct LoggerOptions {
    LogLevel level = LogLevel::Info;
    bool console = false;
    std::uint64_t maxFileBytes = 1024 * 1024;
    std::uint32_t maxRotatedFiles = 3;
};

class Logger {
public:
    explicit Logger(FileSystemService fileSystem);
    Logger(FileSystemService fileSystem, LoggerOptions options);

    void configure(LoggerOptions options);
    void trace(const std::string& message) const;
    void debug(const std::string& message) const;
    void info(const std::string& message) const;
    void warn(const std::string& message) const;
    void error(const std::string& message) const;
    void fatal(const std::string& message) const;

private:
    [[nodiscard]] bool shouldWrite(LogLevel level) const;
    void rotateIfNeeded() const;
    void write(LogLevel level, const std::string& message) const;

    FileSystemService fileSystem_;
    LoggerOptions options_;
    mutable std::mutex mutex_;
};

[[nodiscard]] std::string toString(LogLevel level);
[[nodiscard]] LogLevel logLevelFromString(const std::string& value);

}  // namespace cget
