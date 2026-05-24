#include "cget/ratelimit/rate_limiter.h"

#include <algorithm>

namespace cget {

TokenBucket::TokenBucket(std::uint64_t rateBytesPerSec, std::uint64_t capacityBytes)
    : rateBytesPerSec_(rateBytesPerSec),
      capacityBytes_(std::max<std::uint64_t>(1, capacityBytes)),
      tokens_(static_cast<double>(capacityBytes_)),
      lastRefill_(std::chrono::steady_clock::now()) {}

void TokenBucket::acquire(std::uint64_t bytes) {
    if (bytes == 0 || rateBytesPerSec_ == 0) {
        return;
    }
    std::unique_lock lock(mutex_);
    std::uint64_t remaining = bytes;
    while (remaining > 0) {
        const auto request = std::min<std::uint64_t>(remaining, capacityBytes_);
        while (true) {
            refillLocked();
            const double required = static_cast<double>(request);
            if (tokens_ >= required) {
                tokens_ -= required;
                remaining -= request;
                break;
            }
            const double missing = required - tokens_;
            const double waitSeconds = missing / static_cast<double>(rateBytesPerSec_);
            cv_.wait_for(lock, std::chrono::duration<double>(waitSeconds));
        }
    }
}

bool TokenBucket::tryAcquire(std::uint64_t bytes) {
    if (bytes == 0 || rateBytesPerSec_ == 0) {
        return true;
    }
    std::lock_guard lock(mutex_);
    refillLocked();
    const double required = static_cast<double>(bytes);
    if (tokens_ < required) {
        return false;
    }
    tokens_ -= required;
    return true;
}

void TokenBucket::setRate(std::uint64_t rateBytesPerSec) {
    std::lock_guard lock(mutex_);
    refillLocked();
    rateBytesPerSec_ = rateBytesPerSec;
    capacityBytes_ = std::max<std::uint64_t>(1, rateBytesPerSec);
    tokens_ = std::min<double>(tokens_, static_cast<double>(capacityBytes_));
    cv_.notify_all();
}

std::uint64_t TokenBucket::rate() const {
    std::lock_guard lock(mutex_);
    return rateBytesPerSec_;
}

void TokenBucket::refillLocked() {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - lastRefill_).count();
    tokens_ = std::min<double>(static_cast<double>(capacityBytes_), tokens_ + elapsed * static_cast<double>(rateBytesPerSec_));
    lastRefill_ = now;
}

RateLimiter::RateLimiter(std::optional<std::uint64_t> globalLimit)
    : globalBucket_(makeBucket(globalLimit)) {}

void RateLimiter::setGlobalLimit(std::optional<std::uint64_t> bytesPerSec) {
    std::lock_guard lock(mutex_);
    if (globalBucket_ != nullptr && bytesPerSec && globalBucket_->rate() == *bytesPerSec) {
        return;
    }
    globalBucket_ = makeBucket(bytesPerSec);
}

void RateLimiter::setTaskLimit(const TaskId& id, std::optional<std::uint64_t> bytesPerSec) {
    std::lock_guard lock(mutex_);
    if (!bytesPerSec || *bytesPerSec == 0) {
        taskBuckets_.erase(id);
        return;
    }
    const auto existing = taskBuckets_.find(id);
    if (existing != taskBuckets_.end()) {
        if (existing->second->rate() != *bytesPerSec) {
            existing->second->setRate(*bytesPerSec);
        }
        return;
    }
    taskBuckets_[id] = makeBucket(bytesPerSec);
}

void RateLimiter::acquire(const TaskId& id, std::uint64_t bytes) {
    if (bytes == 0) {
        return;
    }

    std::shared_ptr<TokenBucket> taskBucket;
    std::shared_ptr<TokenBucket> globalBucket;
    {
        std::lock_guard lock(mutex_);
        const auto taskIt = taskBuckets_.find(id);
        if (taskIt != taskBuckets_.end()) {
            taskBucket = taskIt->second;
        }
        globalBucket = globalBucket_;
    }

    if (taskBucket != nullptr) {
        taskBucket->acquire(bytes);
    }
    if (globalBucket != nullptr) {
        globalBucket->acquire(bytes);
    }
}

std::shared_ptr<TokenBucket> RateLimiter::makeBucket(std::optional<std::uint64_t> bytesPerSec) {
    if (!bytesPerSec || *bytesPerSec == 0) {
        return nullptr;
    }
    return std::make_shared<TokenBucket>(*bytesPerSec, *bytesPerSec);
}

}  // namespace cget
