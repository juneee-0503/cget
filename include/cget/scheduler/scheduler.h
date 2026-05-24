#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "cget/config/config_manager.h"
#include "cget/core/types.h"

namespace cget {

struct ChunkJob {
    TaskId taskId;
    std::uint64_t chunkIndex = 0;
    std::uint32_t priority = 0;
    std::chrono::steady_clock::time_point createdAt = std::chrono::steady_clock::now();
};

struct TaskQuota {
    TaskId taskId;
    std::uint32_t maxConcurrentChunks = 0;
    std::uint32_t runningChunks = 0;
    std::uint32_t queuedChunks = 0;
};

struct SchedulerSnapshot {
    std::uint32_t totalWorkers = 0;
    std::uint32_t busyWorkers = 0;
    double workerUtilization = 0.0;
    std::uint32_t activeTasks = 0;
    std::uint32_t queuedTasks = 0;
    std::uint64_t taskQueueSize = 0;
    std::uint64_t chunkQueueSize = 0;
    std::uint32_t maxChunksPerTask = 0;
    std::string policy = "fifo";
};

class Scheduler {
public:
    using ChunkExecutor = std::function<void(DownloadTask&, Chunk&)>;

    explicit Scheduler(SchedulerConfig config);

    [[nodiscard]] SchedulerSnapshot snapshot() const;
    [[nodiscard]] const std::vector<ChunkJob>& scheduledJobs() const;
    void run(std::vector<DownloadTask>& tasks, const ChunkExecutor& executor);

private:
    [[nodiscard]] bool hasSchedulableChunk(const DownloadTask& task) const;
    [[nodiscard]] std::optional<std::size_t> selectNextChunk(DownloadTask& task) const;

    SchedulerConfig config_;
    mutable std::mutex mutex_;
    SchedulerSnapshot snapshot_;
    std::vector<ChunkJob> scheduledJobs_;
};

}  // namespace cget
