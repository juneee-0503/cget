#include "cget/config/config_manager.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>
#include <utility>

#include "cget/core/errors.h"
#include "cget/core/logger.h"
#include "cget/persistence/json.h"
#include "cget/ratelimit/bandwidth.h"

namespace cget {
namespace {

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

json::Value::Object downloadToJson(const DownloadConfig& config) {
    json::Value::Object out;
    out["max_threads"] = json::Value(static_cast<std::uint64_t>(config.maxThreads));
    out["max_active_tasks"] = json::Value(static_cast<std::uint64_t>(config.maxActiveTasks));
    out["max_download_rate_bytes_per_sec"] = json::Value(config.maxDownloadRateBytesPerSec);
    return out;
}

json::Value::Object schedulerToJson(const SchedulerConfig& config) {
    json::Value::Object out;
    out["max_global_workers"] = json::Value(static_cast<std::uint64_t>(config.maxGlobalWorkers));
    out["max_concurrent_tasks"] = json::Value(static_cast<std::uint64_t>(config.maxConcurrentTasks));
    out["max_chunks_per_task"] = json::Value(static_cast<std::uint64_t>(config.maxChunksPerTask));
    out["max_chunk_queue_size"] = json::Value(static_cast<std::uint64_t>(config.maxChunkQueueSize));
    out["policy"] = json::Value(toString(config.policy));
    return out;
}

json::Value::Object metricsToJson(const MetricsConfig& config) {
    json::Value::Object out;
    out["enabled"] = json::Value(config.enabled);
    out["sample_interval_ms"] = json::Value(static_cast<std::uint64_t>(config.sampleIntervalMs));
    return out;
}

json::Value::Object rateLimitToJson(const RateLimitConfig& config) {
    json::Value::Object out;
    out["global"] = json::Value(config.globalBytesPerSec ? std::to_string(*config.globalBytesPerSec) : "unlimited");
    out["default_per_task"] =
        json::Value(config.defaultPerTaskBytesPerSec ? std::to_string(*config.defaultPerTaskBytesPerSec)
                                                     : "unlimited");
    return out;
}

json::Value::Object networkToJson(const NetworkConfig& config) {
    json::Value::Object out;
    out["max_retries"] = json::Value(static_cast<std::uint64_t>(config.maxRetries));
    out["retry_base_delay_ms"] = json::Value(static_cast<std::uint64_t>(config.retryBaseDelayMs));
    out["proxy"] = config.proxyUrl ? json::Value(*config.proxyUrl) : json::Value(nullptr);
    return out;
}

json::Value::Object persistenceToJson(const PersistenceConfig& config) {
    json::Value::Object out;
    out["flush_interval_ms"] = json::Value(static_cast<std::uint64_t>(config.flushIntervalMs));
    return out;
}

json::Value::Object loggingToJson(const LoggingConfig& config) {
    json::Value::Object out;
    out["level"] = json::Value(config.level);
    out["console"] = json::Value(config.console);
    out["max_file_bytes"] = json::Value(config.maxFileBytes);
    out["max_rotated_files"] = json::Value(static_cast<std::uint64_t>(config.maxRotatedFiles));
    return out;
}

json::Value::Object toJson(const Config& config) {
    json::Value::Object out;
    out["download"] = json::Value(downloadToJson(config.download));
    out["scheduler"] = json::Value(schedulerToJson(config.scheduler));
    out["metrics"] = json::Value(metricsToJson(config.metrics));
    out["rate_limit"] = json::Value(rateLimitToJson(config.rateLimit));
    out["network"] = json::Value(networkToJson(config.network));
    out["persistence"] = json::Value(persistenceToJson(config.persistence));
    out["logging"] = json::Value(loggingToJson(config.logging));
    return out;
}

std::string uniqueTempPathFor(const std::filesystem::path& path) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << path.string() << "." << ticks << "." << std::this_thread::get_id() << ".tmp";
    return out.str();
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::uint32_t parsePositiveUintLocal(const std::string& key, const std::string& value) {
    if (value.empty()) {
        throw CgetError(ErrorCode::InvalidCommandError, key + " requires a positive integer");
    }
    std::uint64_t parsed = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') {
            throw CgetError(ErrorCode::InvalidCommandError, key + " requires a positive integer");
        }
        parsed = parsed * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (parsed == 0 || parsed > std::numeric_limits<std::uint32_t>::max()) {
        throw CgetError(ErrorCode::InvalidCommandError, key + " is out of range");
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint64_t parseUint64Local(const std::string& key, const std::string& value) {
    if (value.empty()) {
        throw CgetError(ErrorCode::InvalidCommandError, key + " requires an integer");
    }
    std::uint64_t parsed = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') {
            throw CgetError(ErrorCode::InvalidCommandError, key + " requires an integer");
        }
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            throw CgetError(ErrorCode::InvalidCommandError, key + " is out of range");
        }
        parsed = parsed * 10U + digit;
    }
    return parsed;
}

std::string canonicalKeyName(const std::string& key) {
    if (key == "max_threads" || key == "download.max_threads") return "download.max_threads";
    if (key == "max_active_tasks" || key == "download.max_active_tasks") return "download.max_active_tasks";
    if (key == "max_download_rate_bytes_per_sec" || key == "download.max_download_rate_bytes_per_sec") {
        return "download.max_download_rate_bytes_per_sec";
    }
    if (key == "scheduler.max_global_workers") return "scheduler.max_global_workers";
    if (key == "scheduler.max_concurrent_tasks") return "scheduler.max_concurrent_tasks";
    if (key == "scheduler.max_chunks_per_task") return "scheduler.max_chunks_per_task";
    if (key == "scheduler.max_chunk_queue_size") return "scheduler.max_chunk_queue_size";
    if (key == "scheduler.policy") return "scheduler.policy";
    if (key == "metrics.enabled") return "metrics.enabled";
    if (key == "metrics.sample_interval_ms") return "metrics.sample_interval_ms";
    if (key == "global_rate_limit" || key == "rate_limit.global") return "rate_limit.global";
    if (key == "task_default_rate_limit" || key == "rate_limit.default_per_task") {
        return "rate_limit.default_per_task";
    }
    if (key == "max_retries" || key == "network.max_retries") return "network.max_retries";
    if (key == "retry_base_delay_ms" || key == "network.retry_base_delay_ms") {
        return "network.retry_base_delay_ms";
    }
    if (key == "proxy" || key == "network.proxy") return "network.proxy";
    if (key == "persistence.flush_interval_ms") return "persistence.flush_interval_ms";
    if (key == "logging.level") return "logging.level";
    if (key == "logging.console") return "logging.console";
    if (key == "logging.max_file_bytes") return "logging.max_file_bytes";
    if (key == "logging.max_rotated_files") return "logging.max_rotated_files";
    throw CgetError(ErrorCode::InvalidCommandError, "unknown config key: " + key);
}

bool isValidLogLevelName(const std::string& value) {
    const auto normalized = lower(value);
    return normalized == "trace" || normalized == "debug" || normalized == "info" || normalized == "warn" ||
           normalized == "warning" || normalized == "error" || normalized == "fatal" || normalized == "off" ||
           normalized == "none";
}

void readUint32(const json::Value& object, const std::string& key, std::uint32_t& out) {
    if (object.contains(key)) {
        out = static_cast<std::uint32_t>(object.at(key).asUint64());
    }
}

void readUint64(const json::Value& object, const std::string& key, std::uint64_t& out) {
    if (object.contains(key)) {
        out = object.at(key).asUint64();
    }
}

void readString(const json::Value& object, const std::string& key, std::string& out) {
    if (object.contains(key) && !object.at(key).isNull()) {
        out = object.at(key).asString();
    }
}

void readBool(const json::Value& object, const std::string& key, bool& out) {
    if (object.contains(key)) {
        out = object.at(key).asBool();
    }
}

void applyLegacyConfig(const json::Value& root, Config& config) {
    readUint32(root, "max_threads", config.download.maxThreads);
    readUint32(root, "max_active_tasks", config.download.maxActiveTasks);
    readUint32(root, "max_retries", config.network.maxRetries);
    readUint32(root, "retry_base_delay_ms", config.network.retryBaseDelayMs);
    readUint64(root, "max_download_rate_bytes_per_sec", config.download.maxDownloadRateBytesPerSec);
    if (root.contains("proxy_url") && !root.at("proxy_url").isNull()) {
        config.network.proxyUrl = root.at("proxy_url").asString();
    }
}

void applyNestedConfig(const json::Value& root, Config& config) {
    if (root.contains("download") && root.at("download").isObject()) {
        const auto& download = root.at("download");
        readUint32(download, "max_threads", config.download.maxThreads);
        readUint32(download, "max_active_tasks", config.download.maxActiveTasks);
        readUint64(download, "max_download_rate_bytes_per_sec", config.download.maxDownloadRateBytesPerSec);
    }
    if (root.contains("scheduler") && root.at("scheduler").isObject()) {
        const auto& scheduler = root.at("scheduler");
        readUint32(scheduler, "max_global_workers", config.scheduler.maxGlobalWorkers);
        readUint32(scheduler, "max_concurrent_tasks", config.scheduler.maxConcurrentTasks);
        readUint32(scheduler, "max_chunks_per_task", config.scheduler.maxChunksPerTask);
        readUint32(scheduler, "max_chunk_queue_size", config.scheduler.maxChunkQueueSize);
        if (scheduler.contains("policy") && !scheduler.at("policy").isNull()) {
            config.scheduler.policy = schedulingPolicyFromString(scheduler.at("policy").asString());
        }
    }
    if (root.contains("metrics") && root.at("metrics").isObject()) {
        const auto& metrics = root.at("metrics");
        readBool(metrics, "enabled", config.metrics.enabled);
        readUint32(metrics, "sample_interval_ms", config.metrics.sampleIntervalMs);
    }
    if (root.contains("rate_limit") && root.at("rate_limit").isObject()) {
        const auto& rateLimit = root.at("rate_limit");
        if (rateLimit.contains("global") && !rateLimit.at("global").isNull()) {
            config.rateLimit.globalBytesPerSec = parseBandwidthLimit(rateLimit.at("global").asString());
        }
        if (rateLimit.contains("default_per_task") && !rateLimit.at("default_per_task").isNull()) {
            config.rateLimit.defaultPerTaskBytesPerSec =
                parseBandwidthLimit(rateLimit.at("default_per_task").asString());
        }
    }
    if (root.contains("network") && root.at("network").isObject()) {
        const auto& network = root.at("network");
        readUint32(network, "max_retries", config.network.maxRetries);
        readUint32(network, "retry_base_delay_ms", config.network.retryBaseDelayMs);
        if (network.contains("proxy") && !network.at("proxy").isNull()) {
            config.network.proxyUrl = network.at("proxy").asString();
        }
    }
    if (root.contains("persistence") && root.at("persistence").isObject()) {
        const auto& persistence = root.at("persistence");
        readUint32(persistence, "flush_interval_ms", config.persistence.flushIntervalMs);
    }
    if (root.contains("logging") && root.at("logging").isObject()) {
        const auto& logging = root.at("logging");
        readString(logging, "level", config.logging.level);
        readBool(logging, "console", config.logging.console);
        readUint64(logging, "max_file_bytes", config.logging.maxFileBytes);
        readUint32(logging, "max_rotated_files", config.logging.maxRotatedFiles);
    }
}

bool parseBoolValue(const std::string& value, bool& out) {
    const auto normalized = lower(value);
    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        out = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        out = false;
        return true;
    }
    return false;
}

void applyEnv(Config& config) {
    if (const char* value = std::getenv("CGET_MAX_THREADS")) {
        config.download.maxThreads = parsePositiveUintLocal("CGET_MAX_THREADS", value);
    }
    if (const char* value = std::getenv("CGET_MAX_ACTIVE_TASKS")) {
        config.download.maxActiveTasks = parsePositiveUintLocal("CGET_MAX_ACTIVE_TASKS", value);
    }
    if (const char* value = std::getenv("CGET_MAX_RETRIES")) {
        config.network.maxRetries = parsePositiveUintLocal("CGET_MAX_RETRIES", value);
    }
    if (const char* value = std::getenv("CGET_RETRY_BASE_DELAY_MS")) {
        config.network.retryBaseDelayMs = parsePositiveUintLocal("CGET_RETRY_BASE_DELAY_MS", value);
    }
    if (const char* value = std::getenv("CGET_MAX_DOWNLOAD_RATE_BYTES_PER_SEC")) {
        config.download.maxDownloadRateBytesPerSec =
            parseUint64Local("CGET_MAX_DOWNLOAD_RATE_BYTES_PER_SEC", value);
    }
    if (const char* value = std::getenv("CGET_MAX_GLOBAL_WORKERS")) {
        config.scheduler.maxGlobalWorkers = parsePositiveUintLocal("CGET_MAX_GLOBAL_WORKERS", value);
    }
    if (const char* value = std::getenv("CGET_MAX_CHUNKS_PER_TASK")) {
        config.scheduler.maxChunksPerTask = parsePositiveUintLocal("CGET_MAX_CHUNKS_PER_TASK", value);
    }
    if (const char* value = std::getenv("CGET_GLOBAL_RATE_LIMIT")) {
        config.rateLimit.globalBytesPerSec = parseBandwidthLimit(value);
    }
    if (const char* value = std::getenv("CGET_TASK_DEFAULT_RATE_LIMIT")) {
        config.rateLimit.defaultPerTaskBytesPerSec = parseBandwidthLimit(value);
    }
    if (const char* value = std::getenv("CGET_PROXY")) {
        if (*value == '\0' || std::string(value) == "none") {
            config.network.proxyUrl.reset();
        } else {
            config.network.proxyUrl = value;
        }
    }
    if (const char* value = std::getenv("CGET_LOG_LEVEL")) {
        if (isValidLogLevelName(value)) {
            config.logging.level = lower(value);
        }
    }
    if (const char* value = std::getenv("CGET_LOG_CONSOLE")) {
        bool parsed = false;
        if (parseBoolValue(value, parsed)) {
            config.logging.console = parsed;
        }
    }
}

void normalize(Config& config) {
    config.download.maxThreads = std::max<std::uint32_t>(1, std::min<std::uint32_t>(config.download.maxThreads, 32));
    config.download.maxActiveTasks =
        std::max<std::uint32_t>(1, std::min<std::uint32_t>(config.download.maxActiveTasks, 16));
    config.scheduler.maxGlobalWorkers =
        std::max<std::uint32_t>(1, std::min<std::uint32_t>(config.scheduler.maxGlobalWorkers, 128));
    config.scheduler.maxConcurrentTasks =
        std::max<std::uint32_t>(1, std::min<std::uint32_t>(config.scheduler.maxConcurrentTasks, 64));
    config.scheduler.maxChunksPerTask =
        std::max<std::uint32_t>(1, std::min<std::uint32_t>(config.scheduler.maxChunksPerTask, 64));
    config.scheduler.maxChunkQueueSize =
        std::max<std::uint32_t>(1, std::min<std::uint32_t>(config.scheduler.maxChunkQueueSize, 65536));
    config.metrics.sampleIntervalMs =
        std::max<std::uint32_t>(100, std::min<std::uint32_t>(config.metrics.sampleIntervalMs, 60000));
    config.network.maxRetries = std::min<std::uint32_t>(config.network.maxRetries, 10);
    config.network.retryBaseDelayMs =
        std::max<std::uint32_t>(100, std::min<std::uint32_t>(config.network.retryBaseDelayMs, 60000));
    config.persistence.flushIntervalMs =
        std::max<std::uint32_t>(100, std::min<std::uint32_t>(config.persistence.flushIntervalMs, 60000));
    config.logging.level = isValidLogLevelName(config.logging.level) ? lower(config.logging.level) : "info";
    config.logging.maxRotatedFiles = std::min<std::uint32_t>(config.logging.maxRotatedFiles, 16);
}

std::string valueForKey(const Config& config, const std::string& key) {
    const auto canonical = canonicalKeyName(key);
    if (canonical == "download.max_threads") return std::to_string(config.download.maxThreads);
    if (canonical == "download.max_active_tasks") return std::to_string(config.download.maxActiveTasks);
    if (canonical == "download.max_download_rate_bytes_per_sec") {
        return std::to_string(config.download.maxDownloadRateBytesPerSec);
    }
    if (canonical == "scheduler.max_global_workers") return std::to_string(config.scheduler.maxGlobalWorkers);
    if (canonical == "scheduler.max_concurrent_tasks") return std::to_string(config.scheduler.maxConcurrentTasks);
    if (canonical == "scheduler.max_chunks_per_task") return std::to_string(config.scheduler.maxChunksPerTask);
    if (canonical == "scheduler.max_chunk_queue_size") return std::to_string(config.scheduler.maxChunkQueueSize);
    if (canonical == "scheduler.policy") return toString(config.scheduler.policy);
    if (canonical == "metrics.enabled") return config.metrics.enabled ? "true" : "false";
    if (canonical == "metrics.sample_interval_ms") return std::to_string(config.metrics.sampleIntervalMs);
    if (canonical == "rate_limit.global") return formatRateLimit(config.rateLimit.globalBytesPerSec);
    if (canonical == "rate_limit.default_per_task") {
        return formatRateLimit(config.rateLimit.defaultPerTaskBytesPerSec);
    }
    if (canonical == "network.max_retries") return std::to_string(config.network.maxRetries);
    if (canonical == "network.retry_base_delay_ms") return std::to_string(config.network.retryBaseDelayMs);
    if (canonical == "network.proxy") return config.network.proxyUrl.value_or("none");
    if (canonical == "persistence.flush_interval_ms") return std::to_string(config.persistence.flushIntervalMs);
    if (canonical == "logging.level") return config.logging.level;
    if (canonical == "logging.console") return config.logging.console ? "true" : "false";
    if (canonical == "logging.max_file_bytes") return std::to_string(config.logging.maxFileBytes);
    if (canonical == "logging.max_rotated_files") return std::to_string(config.logging.maxRotatedFiles);
    throw CgetError(ErrorCode::InvalidCommandError, "unknown config key: " + key);
}

}  // namespace

std::string toString(SchedulingPolicyType policy) {
    switch (policy) {
        case SchedulingPolicyType::Fifo: return "fifo";
        case SchedulingPolicyType::SmallTaskFirst: return "small_task_first";
    }
    return "fifo";
}

SchedulingPolicyType schedulingPolicyFromString(const std::string& value) {
    const auto normalized = lower(value);
    if (normalized == "fifo") {
        return SchedulingPolicyType::Fifo;
    }
    if (normalized == "small_task_first" || normalized == "small-task-first") {
        return SchedulingPolicyType::SmallTaskFirst;
    }
    throw CgetError(ErrorCode::InvalidCommandError,
                   "scheduler.policy must be fifo or small_task_first");
}

ConfigManager::ConfigManager(FileSystemService fileSystem) : fileSystem_(std::move(fileSystem)) {}

Config ConfigManager::load() const {
    fileSystem_.ensureDirectories();
    const auto path = fileSystem_.configPath();
    Config config;
    if (!std::filesystem::exists(path)) {
        save(config);
        applyEnv(config);
        normalize(config);
        return config;
    }

    try {
        const auto root = json::parse(readTextFile(path));
        applyLegacyConfig(root, config);
        applyNestedConfig(root, config);
        applyEnv(config);
        normalize(config);
        return config;
    } catch (const std::exception& error) {
        throw CgetError(ErrorCode::JsonCorruptedError, "failed to read config: " + std::string(error.what()));
    }
}

void ConfigManager::save(const Config& config) const {
    fileSystem_.ensureDirectories();
    auto normalized = config;
    normalize(normalized);
    const auto path = fileSystem_.configPath();
    const auto tmp = uniqueTempPathFor(path);
    {
        std::ofstream output(tmp, std::ios::trunc);
        if (!output) {
            throw CgetError(ErrorCode::PermissionDeniedError, "failed to write config: " + tmp);
        }
        output << json::stringify(json::Value(toJson(normalized)));
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to commit config: " + ec.message());
    }
}

std::string ConfigManager::get(const std::string& key) const {
    validateKey(key);
    return valueForKey(load(), key);
}

void ConfigManager::set(const std::string& key, const std::string& value) const {
    validateKey(key);
    const auto canonical = canonicalKey(key);
    auto config = load();
    if (canonical == "download.max_threads") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed > 32) {
            throw CgetError(ErrorCode::InvalidCommandError, "max_threads must be <= 32");
        }
        config.download.maxThreads = parsed;
    } else if (canonical == "download.max_active_tasks") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed > 16) {
            throw CgetError(ErrorCode::InvalidCommandError, "max_active_tasks must be <= 16");
        }
        config.download.maxActiveTasks = parsed;
    } else if (canonical == "download.max_download_rate_bytes_per_sec") {
        config.download.maxDownloadRateBytesPerSec = parseUint64(key, value);
    } else if (canonical == "scheduler.max_global_workers") {
        config.scheduler.maxGlobalWorkers = parsePositiveUint(key, value);
    } else if (canonical == "scheduler.max_concurrent_tasks") {
        config.scheduler.maxConcurrentTasks = parsePositiveUint(key, value);
    } else if (canonical == "scheduler.max_chunks_per_task") {
        config.scheduler.maxChunksPerTask = parsePositiveUint(key, value);
    } else if (canonical == "scheduler.max_chunk_queue_size") {
        config.scheduler.maxChunkQueueSize = parsePositiveUint(key, value);
    } else if (canonical == "scheduler.policy") {
        config.scheduler.policy = schedulingPolicyFromString(value);
    } else if (canonical == "metrics.enabled") {
        config.metrics.enabled = parseBool(key, value);
    } else if (canonical == "metrics.sample_interval_ms") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed < 100 || parsed > 60000) {
            throw CgetError(ErrorCode::InvalidCommandError, "metrics.sample_interval_ms must be between 100 and 60000");
        }
        config.metrics.sampleIntervalMs = parsed;
    } else if (canonical == "rate_limit.global") {
        config.rateLimit.globalBytesPerSec = parseBandwidthLimit(value);
    } else if (canonical == "rate_limit.default_per_task") {
        config.rateLimit.defaultPerTaskBytesPerSec = parseBandwidthLimit(value);
    } else if (canonical == "network.max_retries") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed > 10) {
            throw CgetError(ErrorCode::InvalidCommandError, "max_retries must be <= 10");
        }
        config.network.maxRetries = parsed;
    } else if (canonical == "network.retry_base_delay_ms") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed < 100 || parsed > 60000) {
            throw CgetError(ErrorCode::InvalidCommandError, "retry_base_delay_ms must be between 100 and 60000");
        }
        config.network.retryBaseDelayMs = parsed;
    } else if (canonical == "network.proxy") {
        if (value == "none" || value == "off" || value == "disable" || value == "disabled") {
            config.network.proxyUrl.reset();
        } else if (value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0 ||
                   value.rfind("socks5://", 0) == 0) {
            config.network.proxyUrl = value;
        } else {
            throw CgetError(ErrorCode::InvalidCommandError,
                           "proxy must be none, http://..., https://..., or socks5://...");
        }
    } else if (canonical == "persistence.flush_interval_ms") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed < 100 || parsed > 60000) {
            throw CgetError(ErrorCode::InvalidCommandError, "flush_interval_ms must be between 100 and 60000");
        }
        config.persistence.flushIntervalMs = parsed;
    } else if (canonical == "logging.level") {
        if (!isValidLogLevelName(value)) {
            throw CgetError(ErrorCode::InvalidCommandError,
                           "logging.level must be trace, debug, info, warn, error, fatal, or off");
        }
        config.logging.level = lower(value);
    } else if (canonical == "logging.console") {
        config.logging.console = parseBool(key, value);
    } else if (canonical == "logging.max_file_bytes") {
        config.logging.maxFileBytes = parseUint64(key, value);
    } else if (canonical == "logging.max_rotated_files") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed > 16) {
            throw CgetError(ErrorCode::InvalidCommandError, "logging.max_rotated_files must be <= 16");
        }
        config.logging.maxRotatedFiles = parsed;
    }
    save(config);
}

std::vector<std::pair<std::string, std::string>> ConfigManager::entries() const {
    const auto config = load();
    return {
        {"download.max_threads", std::to_string(config.download.maxThreads)},
        {"download.max_active_tasks", std::to_string(config.download.maxActiveTasks)},
        {"download.max_download_rate_bytes_per_sec", std::to_string(config.download.maxDownloadRateBytesPerSec)},
        {"scheduler.max_global_workers", std::to_string(config.scheduler.maxGlobalWorkers)},
        {"scheduler.max_concurrent_tasks", std::to_string(config.scheduler.maxConcurrentTasks)},
        {"scheduler.max_chunks_per_task", std::to_string(config.scheduler.maxChunksPerTask)},
        {"scheduler.max_chunk_queue_size", std::to_string(config.scheduler.maxChunkQueueSize)},
        {"scheduler.policy", toString(config.scheduler.policy)},
        {"metrics.enabled", config.metrics.enabled ? "true" : "false"},
        {"metrics.sample_interval_ms", std::to_string(config.metrics.sampleIntervalMs)},
        {"rate_limit.global", formatRateLimit(config.rateLimit.globalBytesPerSec)},
        {"rate_limit.default_per_task", formatRateLimit(config.rateLimit.defaultPerTaskBytesPerSec)},
        {"network.max_retries", std::to_string(config.network.maxRetries)},
        {"network.retry_base_delay_ms", std::to_string(config.network.retryBaseDelayMs)},
        {"network.proxy", config.network.proxyUrl.value_or("none")},
        {"persistence.flush_interval_ms", std::to_string(config.persistence.flushIntervalMs)},
        {"logging.level", config.logging.level},
        {"logging.console", config.logging.console ? "true" : "false"},
        {"logging.max_file_bytes", std::to_string(config.logging.maxFileBytes)},
        {"logging.max_rotated_files", std::to_string(config.logging.maxRotatedFiles)},
    };
}

void ConfigManager::validateKey(const std::string& key) {
    (void)canonicalKey(key);
}

std::string ConfigManager::canonicalKey(const std::string& key) {
    return canonicalKeyName(key);
}

std::uint32_t ConfigManager::parsePositiveUint(const std::string& key, const std::string& value) {
    return parsePositiveUintLocal(key, value);
}

std::uint64_t ConfigManager::parseUint64(const std::string& key, const std::string& value) {
    return parseUint64Local(key, value);
}

bool ConfigManager::parseBool(const std::string& key, const std::string& value) {
    bool parsed = false;
    if (!parseBoolValue(value, parsed)) {
        throw CgetError(ErrorCode::InvalidCommandError, key + " requires true or false");
    }
    return parsed;
}

}  // namespace cget
