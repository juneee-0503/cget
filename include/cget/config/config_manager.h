#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cget/filesystem/filesystem_service.h"

namespace cget {

struct Config {
    std::uint32_t maxThreads = 8;
    std::uint32_t maxActiveTasks = 2;
    std::uint32_t maxRetries = 3;
    std::uint32_t retryBaseDelayMs = 1000;
    std::uint64_t maxDownloadRateBytesPerSec = 0;
    std::optional<std::string> proxyUrl;
};

class ConfigManager {
public:
    explicit ConfigManager(FileSystemService fileSystem);

    [[nodiscard]] Config load() const;
    void save(const Config& config) const;
    [[nodiscard]] std::string get(const std::string& key) const;
    void set(const std::string& key, const std::string& value) const;
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> entries() const;

private:
    static void validateKey(const std::string& key);
    static std::uint32_t parsePositiveUint(const std::string& key, const std::string& value);
    static std::uint64_t parseUint64(const std::string& key, const std::string& value);

    FileSystemService fileSystem_;
};

}  // namespace cget
