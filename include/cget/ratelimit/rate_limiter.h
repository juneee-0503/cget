#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "cget/core/types.h"

namespace cget {

class TokenBucket {
public:
    TokenBucket(std::uint64_t rateBytesPerSec, std::uint64_t capacityBytes);

    void acquire(std::uint64_t bytes);
    [[nodiscard]] bool tryAcquire(std::uint64_t bytes);
    void setRate(std::uint64_t rateBytesPerSec);
    [[nodiscard]] std::uint64_t rate() const;

private:
    void refillLocked();

    std::uint64_t rateBytesPerSec_;
    std::uint64_t capacityBytes_;
    double tokens_;
    std::chrono::steady_clock::time_point lastRefill_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

class RateLimiter {
public:
    explicit RateLimiter(std::optional<std::uint64_t> globalLimit = std::nullopt);

    void setGlobalLimit(std::optional<std::uint64_t> bytesPerSec);
    void setTaskLimit(const TaskId& id, std::optional<std::uint64_t> bytesPerSec);
    void acquire(const TaskId& id, std::uint64_t bytes);

private:
    static std::shared_ptr<TokenBucket> makeBucket(std::optional<std::uint64_t> bytesPerSec);

    std::shared_ptr<TokenBucket> globalBucket_;
    std::unordered_map<TaskId, std::shared_ptr<TokenBucket>> taskBuckets_;
    mutable std::mutex mutex_;
};

}  // namespace cget
