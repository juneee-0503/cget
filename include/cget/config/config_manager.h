#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cget/filesystem/filesystem_service.h"

namespace cget {

struct DownloadConfig {
    std::uint32_t maxThreads = 8;
    std::uint32_t maxActiveTasks = 2;
    std::uint64_t maxDownloadRateBytesPerSec = 0;
};

enum class SchedulingPolicyType {
    Fifo,
    SmallTaskFirst
};

struct SchedulerConfig {
    std::uint32_t maxGlobalWorkers = 16;
    std::uint32_t maxConcurrentTasks = 4;
    std::uint32_t maxChunksPerTask = 4;
    std::uint32_t maxChunkQueueSize = 1024;
    SchedulingPolicyType policy = SchedulingPolicyType::Fifo;
};

struct MetricsConfig {
    bool enabled = true;
    std::uint32_t sampleIntervalMs = 1000;
};

struct RateLimitConfig {
    std::optional<std::uint64_t> globalBytesPerSec;
    std::optional<std::uint64_t> defaultPerTaskBytesPerSec;
};

struct NetworkConfig {
    std::uint32_t maxRetries = 3;
    std::uint32_t retryBaseDelayMs = 1000;
    std::optional<std::string> proxyUrl;
};

struct PersistenceConfig {
    std::uint32_t flushIntervalMs = 1000;
};

struct LoggingConfig {
    std::string level = "info";
    bool console = false;
    std::uint64_t maxFileBytes = 1024 * 1024;
    std::uint32_t maxRotatedFiles = 3;
};

struct Config {
    DownloadConfig download;
    SchedulerConfig scheduler;
    MetricsConfig metrics;
    RateLimitConfig rateLimit;
    NetworkConfig network;
    PersistenceConfig persistence;
    LoggingConfig logging;
};

[[nodiscard]] std::string toString(SchedulingPolicyType policy);
[[nodiscard]] SchedulingPolicyType schedulingPolicyFromString(const std::string& value);

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
    static std::string canonicalKey(const std::string& key);
    static std::uint32_t parsePositiveUint(const std::string& key, const std::string& value);
    static std::uint64_t parseUint64(const std::string& key, const std::string& value);
    static bool parseBool(const std::string& key, const std::string& value);

    FileSystemService fileSystem_;
};

}  // namespace cget
