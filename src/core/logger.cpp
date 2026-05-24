#include "cget/core/logger.h"

#include <fstream>
#include <utility>

#include "cget/core/types.h"

namespace cget {

Logger::Logger(FileSystemService fileSystem) : fileSystem_(std::move(fileSystem)) {}

void Logger::info(const std::string& message) const {
    write("INFO", message);
}

void Logger::warn(const std::string& message) const {
    write("WARN", message);
}

void Logger::error(const std::string& message) const {
    write("ERROR", message);
}

void Logger::write(const std::string& level, const std::string& message) const {
    try {
        fileSystem_.ensureDirectories();
        std::ofstream output(fileSystem_.logsDir() / "cget.log", std::ios::app);
        output << formatTimestamp(std::chrono::system_clock::now()) << " [" << level << "] " << message << '\n';
    } catch (...) {
        // Logging must never break download or CLI control paths.
    }
}

}  // namespace cget
