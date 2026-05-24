#include "cget/config/config_manager.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>
#include <utility>

#include "cget/core/errors.h"
#include "cget/persistence/json.h"

namespace cget {
namespace {

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

json::Value::Object toJson(const Config& config) {
    json::Value::Object out;
    out["max_threads"] = json::Value(static_cast<std::uint64_t>(config.maxThreads));
    out["max_active_tasks"] = json::Value(static_cast<std::uint64_t>(config.maxActiveTasks));
    out["max_retries"] = json::Value(static_cast<std::uint64_t>(config.maxRetries));
    out["retry_base_delay_ms"] = json::Value(static_cast<std::uint64_t>(config.retryBaseDelayMs));
    out["max_download_rate_bytes_per_sec"] = json::Value(config.maxDownloadRateBytesPerSec);
    out["proxy_url"] = config.proxyUrl ? json::Value(*config.proxyUrl) : json::Value(nullptr);
    return out;
}

std::string uniqueTempPathFor(const std::filesystem::path& path) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream out;
    out << path.string() << "." << ticks << "." << std::this_thread::get_id() << ".tmp";
    return out.str();
}

std::string valueForKey(const Config& config, const std::string& key) {
    if (key == "max_threads") return std::to_string(config.maxThreads);
    if (key == "max_active_tasks") return std::to_string(config.maxActiveTasks);
    if (key == "max_retries") return std::to_string(config.maxRetries);
    if (key == "retry_base_delay_ms") return std::to_string(config.retryBaseDelayMs);
    if (key == "max_download_rate_bytes_per_sec") return std::to_string(config.maxDownloadRateBytesPerSec);
    if (key == "proxy") return config.proxyUrl.value_or("none");
    throw CgetError(ErrorCode::InvalidCommandError, "unknown config key: " + key);
}

}  // namespace

ConfigManager::ConfigManager(FileSystemService fileSystem) : fileSystem_(std::move(fileSystem)) {}

Config ConfigManager::load() const {
    fileSystem_.ensureDirectories();
    const auto path = fileSystem_.configPath();
    if (!std::filesystem::exists(path)) {
        Config config;
        save(config);
        return config;
    }

    try {
        const auto root = json::parse(readTextFile(path));
        Config config;
        if (root.contains("max_threads")) {
            config.maxThreads = static_cast<std::uint32_t>(root.at("max_threads").asUint64());
        }
        if (root.contains("max_active_tasks")) {
            config.maxActiveTasks = static_cast<std::uint32_t>(root.at("max_active_tasks").asUint64());
        }
        if (root.contains("max_retries")) {
            config.maxRetries = static_cast<std::uint32_t>(root.at("max_retries").asUint64());
        }
        if (root.contains("retry_base_delay_ms")) {
            config.retryBaseDelayMs = static_cast<std::uint32_t>(root.at("retry_base_delay_ms").asUint64());
        }
        if (root.contains("max_download_rate_bytes_per_sec")) {
            config.maxDownloadRateBytesPerSec = root.at("max_download_rate_bytes_per_sec").asUint64();
        }
        if (root.contains("proxy_url") && !root.at("proxy_url").isNull()) {
            config.proxyUrl = root.at("proxy_url").asString();
        }
        config.maxThreads = std::max<std::uint32_t>(1, std::min<std::uint32_t>(config.maxThreads, 32));
        config.maxActiveTasks = std::max<std::uint32_t>(1, std::min<std::uint32_t>(config.maxActiveTasks, 16));
        config.maxRetries = std::min<std::uint32_t>(config.maxRetries, 10);
        config.retryBaseDelayMs = std::max<std::uint32_t>(100, std::min<std::uint32_t>(config.retryBaseDelayMs, 60000));
        return config;
    } catch (const std::exception& error) {
        throw CgetError(ErrorCode::JsonCorruptedError, "failed to read config: " + std::string(error.what()));
    }
}

void ConfigManager::save(const Config& config) const {
    fileSystem_.ensureDirectories();
    const auto path = fileSystem_.configPath();
    const auto tmp = uniqueTempPathFor(path);
    {
        std::ofstream output(tmp, std::ios::trunc);
        if (!output) {
            throw CgetError(ErrorCode::PermissionDeniedError, "failed to write config: " + tmp);
        }
        output << json::stringify(json::Value(toJson(config)));
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
    auto config = load();
    if (key == "max_threads") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed > 32) {
            throw CgetError(ErrorCode::InvalidCommandError, "max_threads must be <= 32");
        }
        config.maxThreads = parsed;
    } else if (key == "max_active_tasks") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed > 16) {
            throw CgetError(ErrorCode::InvalidCommandError, "max_active_tasks must be <= 16");
        }
        config.maxActiveTasks = parsed;
    } else if (key == "max_retries") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed > 10) {
            throw CgetError(ErrorCode::InvalidCommandError, "max_retries must be <= 10");
        }
        config.maxRetries = parsed;
    } else if (key == "retry_base_delay_ms") {
        const auto parsed = parsePositiveUint(key, value);
        if (parsed < 100 || parsed > 60000) {
            throw CgetError(ErrorCode::InvalidCommandError, "retry_base_delay_ms must be between 100 and 60000");
        }
        config.retryBaseDelayMs = parsed;
    } else if (key == "max_download_rate_bytes_per_sec") {
        config.maxDownloadRateBytesPerSec = parseUint64(key, value);
    } else if (key == "proxy") {
        if (value == "none" || value == "off" || value == "disable" || value == "disabled") {
            config.proxyUrl.reset();
        } else if (value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0 ||
                   value.rfind("socks5://", 0) == 0) {
            config.proxyUrl = value;
        } else {
            throw CgetError(ErrorCode::InvalidCommandError,
                           "proxy must be none, http://..., https://..., or socks5://...");
        }
    }
    save(config);
}

std::vector<std::pair<std::string, std::string>> ConfigManager::entries() const {
    const auto config = load();
    return {
        {"max_threads", std::to_string(config.maxThreads)},
        {"max_active_tasks", std::to_string(config.maxActiveTasks)},
        {"max_retries", std::to_string(config.maxRetries)},
        {"retry_base_delay_ms", std::to_string(config.retryBaseDelayMs)},
        {"max_download_rate_bytes_per_sec", std::to_string(config.maxDownloadRateBytesPerSec)},
        {"proxy", config.proxyUrl.value_or("none")},
    };
}

void ConfigManager::validateKey(const std::string& key) {
    if (key == "max_threads" || key == "max_active_tasks" || key == "max_retries" ||
        key == "retry_base_delay_ms" || key == "max_download_rate_bytes_per_sec" || key == "proxy") {
        return;
    }
    throw CgetError(ErrorCode::InvalidCommandError, "unknown config key: " + key);
}

std::uint32_t ConfigManager::parsePositiveUint(const std::string& key, const std::string& value) {
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

std::uint64_t ConfigManager::parseUint64(const std::string& key, const std::string& value) {
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

}  // namespace cget
