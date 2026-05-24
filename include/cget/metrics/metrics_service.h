#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "cget/core/types.h"
#include "cget/filesystem/filesystem_service.h"
#include "cget/scheduler/scheduler.h"

namespace cget {

struct TaskMetrics {
    TaskId taskId;
    std::string fileName;
    std::uint64_t totalBytes = 0;
    std::uint64_t downloadedBytes = 0;
    double progressPercent = 0.0;
    double currentSpeedBytesPerSec = 0.0;
    double averageSpeedBytesPerSec = 0.0;
    double peakSpeedBytesPerSec = 0.0;
    std::optional<std::chrono::seconds> eta;
    std::uint32_t totalChunks = 0;
    std::uint32_t completedChunks = 0;
    std::uint32_t runningChunks = 0;
    std::uint32_t failedChunks = 0;
    std::uint32_t retryCount = 0;
    TaskStatus status = TaskStatus::Created;
    std::optional<std::string> lastError;
};

struct SystemMetrics {
    std::uint32_t totalTasks = 0;
    std::uint32_t activeTasks = 0;
    std::uint32_t queuedTasks = 0;
    std::uint32_t pausedTasks = 0;
    std::uint32_t failedTasks = 0;
    std::uint32_t completedTasks = 0;
    std::uint32_t totalWorkers = 0;
    std::uint32_t busyWorkers = 0;
    double workerUtilization = 0.0;
    double globalCurrentSpeedBytesPerSec = 0.0;
    double globalAverageSpeedBytesPerSec = 0.0;
    std::uint64_t persistenceFlushCount = 0;
    std::uint64_t schedulerQueueSize = 0;
    std::string schedulerPolicy = "fifo";
    std::chrono::system_clock::time_point updatedAt = std::chrono::system_clock::now();
    bool stale = false;
};

class MetricsService {
public:
    explicit MetricsService(FileSystemService fileSystem);

    void sample(const std::vector<TaskSnapshot>& tasks, const SchedulerSnapshot& scheduler);
    [[nodiscard]] std::optional<TaskMetrics> getTaskMetrics(const TaskId& id) const;
    [[nodiscard]] std::vector<TaskMetrics> listTaskMetrics() const;
    [[nodiscard]] SystemMetrics getSystemMetrics() const;
    void saveRuntimeSnapshot() const;
    [[nodiscard]] static std::optional<SystemMetrics> loadRuntimeSnapshot(const FileSystemService& fileSystem);

private:
    FileSystemService fileSystem_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<TaskId, TaskMetrics> taskMetrics_;
    SystemMetrics systemMetrics_;
    std::unordered_map<TaskId, std::uint64_t> lastDownloadedBytes_;
    std::unordered_map<TaskId, std::chrono::steady_clock::time_point> lastSampleTime_;
    std::unordered_map<TaskId, std::chrono::steady_clock::time_point> taskStartTime_;
};

}  // namespace cget
