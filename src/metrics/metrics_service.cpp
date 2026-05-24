#include "cget/metrics/metrics_service.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>

#include "cget/core/errors.h"
#include "cget/persistence/json.h"

namespace cget {
namespace {

std::filesystem::path metricsPath(const FileSystemService& fileSystem) {
    return fileSystem.appHome() / "metrics.json";
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

json::Value::Object systemToJson(const SystemMetrics& metrics) {
    json::Value::Object out;
    out["total_tasks"] = json::Value(static_cast<std::uint64_t>(metrics.totalTasks));
    out["active_tasks"] = json::Value(static_cast<std::uint64_t>(metrics.activeTasks));
    out["queued_tasks"] = json::Value(static_cast<std::uint64_t>(metrics.queuedTasks));
    out["paused_tasks"] = json::Value(static_cast<std::uint64_t>(metrics.pausedTasks));
    out["failed_tasks"] = json::Value(static_cast<std::uint64_t>(metrics.failedTasks));
    out["completed_tasks"] = json::Value(static_cast<std::uint64_t>(metrics.completedTasks));
    out["total_workers"] = json::Value(static_cast<std::uint64_t>(metrics.totalWorkers));
    out["busy_workers"] = json::Value(static_cast<std::uint64_t>(metrics.busyWorkers));
    out["worker_utilization"] = json::Value(metrics.workerUtilization);
    out["global_current_speed_bytes_per_sec"] = json::Value(metrics.globalCurrentSpeedBytesPerSec);
    out["global_average_speed_bytes_per_sec"] = json::Value(metrics.globalAverageSpeedBytesPerSec);
    out["persistence_flush_count"] = json::Value(metrics.persistenceFlushCount);
    out["scheduler_queue_size"] = json::Value(metrics.schedulerQueueSize);
    out["scheduler_policy"] = json::Value(metrics.schedulerPolicy);
    out["updated_at"] = json::Value(formatTimestamp(metrics.updatedAt));
    return out;
}

}  // namespace

MetricsService::MetricsService(FileSystemService fileSystem) : fileSystem_(std::move(fileSystem)) {}

void MetricsService::sample(const std::vector<TaskSnapshot>& tasks, const SchedulerSnapshot& scheduler) {
    const auto now = std::chrono::steady_clock::now();
    std::unique_lock lock(mutex_);

    SystemMetrics system;
    system.totalTasks = static_cast<std::uint32_t>(tasks.size());
    system.totalWorkers = scheduler.totalWorkers;
    system.busyWorkers = scheduler.busyWorkers;
    system.workerUtilization = scheduler.workerUtilization;
    system.schedulerQueueSize = scheduler.taskQueueSize + scheduler.chunkQueueSize;
    system.schedulerPolicy = scheduler.policy;
    system.updatedAt = std::chrono::system_clock::now();

    double globalCurrent = 0.0;
    double globalAverage = 0.0;
    for (const auto& task : tasks) {
        auto& start = taskStartTime_[task.id];
        if (start == std::chrono::steady_clock::time_point{}) {
            start = now;
        }
        const auto lastTimeIt = lastSampleTime_.find(task.id);
        const auto lastBytesIt = lastDownloadedBytes_.find(task.id);
        const double interval = lastTimeIt == lastSampleTime_.end()
                                    ? 0.0
                                    : std::chrono::duration<double>(now - lastTimeIt->second).count();
        const std::uint64_t lastBytes = lastBytesIt == lastDownloadedBytes_.end() ? task.downloaded : lastBytesIt->second;
        const std::uint64_t deltaBytes = task.downloaded >= lastBytes ? task.downloaded - lastBytes : 0;
        const double currentSpeed = interval > 0.0 ? static_cast<double>(deltaBytes) / interval : 0.0;
        const double elapsed = std::max(0.001, std::chrono::duration<double>(now - start).count());
        const double averageSpeed = static_cast<double>(task.downloaded) / elapsed;
        const double previousPeak = taskMetrics_.count(task.id) ? taskMetrics_.at(task.id).peakSpeedBytesPerSec
                                                                : task.peakSpeedBytesPerSec;
        TaskMetrics metrics;
        metrics.taskId = task.id;
        metrics.fileName = task.fileName;
        metrics.totalBytes = task.fileSize;
        metrics.downloadedBytes = task.downloaded;
        metrics.progressPercent = task.progress;
        metrics.currentSpeedBytesPerSec = currentSpeed;
        metrics.averageSpeedBytesPerSec = averageSpeed;
        metrics.peakSpeedBytesPerSec = std::max(previousPeak, currentSpeed);
        if (currentSpeed > 0.0 && task.fileSize > task.downloaded && task.status != TaskStatus::Completed) {
            metrics.eta = std::chrono::seconds(static_cast<std::int64_t>(
                std::ceil(static_cast<double>(task.fileSize - task.downloaded) / currentSpeed)));
        }
        metrics.totalChunks = task.totalChunks;
        metrics.completedChunks = task.completedChunks;
        metrics.runningChunks = task.runningChunks;
        metrics.failedChunks = task.failedChunks;
        metrics.retryCount = task.retryCount;
        metrics.status = task.status;
        metrics.lastError = task.lastError;
        taskMetrics_[task.id] = metrics;
        lastDownloadedBytes_[task.id] = task.downloaded;
        lastSampleTime_[task.id] = now;

        globalCurrent += currentSpeed;
        globalAverage += averageSpeed;
        if (task.status == TaskStatus::Downloading || task.status == TaskStatus::Retrying) ++system.activeTasks;
        if (task.status == TaskStatus::Queued || task.status == TaskStatus::Pending) ++system.queuedTasks;
        if (task.status == TaskStatus::Paused || task.status == TaskStatus::PendingRecovery) ++system.pausedTasks;
        if (task.status == TaskStatus::Failed || task.status == TaskStatus::MetadataMismatch ||
            task.status == TaskStatus::Corrupted) ++system.failedTasks;
        if (task.status == TaskStatus::Completed) ++system.completedTasks;
    }
    system.globalCurrentSpeedBytesPerSec = globalCurrent;
    system.globalAverageSpeedBytesPerSec = globalAverage;
    systemMetrics_ = system;
}

std::optional<TaskMetrics> MetricsService::getTaskMetrics(const TaskId& id) const {
    std::shared_lock lock(mutex_);
    const auto it = taskMetrics_.find(id);
    if (it == taskMetrics_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<TaskMetrics> MetricsService::listTaskMetrics() const {
    std::shared_lock lock(mutex_);
    std::vector<TaskMetrics> out;
    for (const auto& [_, metrics] : taskMetrics_) {
        out.push_back(metrics);
    }
    return out;
}

SystemMetrics MetricsService::getSystemMetrics() const {
    std::shared_lock lock(mutex_);
    return systemMetrics_;
}

void MetricsService::saveRuntimeSnapshot() const {
    try {
        fileSystem_.ensureDirectories();
        std::ofstream output(metricsPath(fileSystem_), std::ios::trunc);
        output << json::stringify(json::Value(systemToJson(getSystemMetrics())));
    } catch (...) {
    }
}

std::optional<SystemMetrics> MetricsService::loadRuntimeSnapshot(const FileSystemService& fileSystem) {
    const auto path = metricsPath(fileSystem);
    if (!std::filesystem::exists(path)) {
        return std::nullopt;
    }
    try {
        const auto root = json::parse(readTextFile(path));
        SystemMetrics metrics;
        metrics.totalTasks = static_cast<std::uint32_t>(root.at("total_tasks").asUint64());
        metrics.activeTasks = static_cast<std::uint32_t>(root.at("active_tasks").asUint64());
        metrics.queuedTasks = static_cast<std::uint32_t>(root.at("queued_tasks").asUint64());
        metrics.pausedTasks = static_cast<std::uint32_t>(root.at("paused_tasks").asUint64());
        metrics.failedTasks = static_cast<std::uint32_t>(root.at("failed_tasks").asUint64());
        metrics.completedTasks = static_cast<std::uint32_t>(root.at("completed_tasks").asUint64());
        metrics.totalWorkers = static_cast<std::uint32_t>(root.at("total_workers").asUint64());
        metrics.busyWorkers = static_cast<std::uint32_t>(root.at("busy_workers").asUint64());
        metrics.workerUtilization = root.at("worker_utilization").asNumber();
        metrics.globalCurrentSpeedBytesPerSec = root.at("global_current_speed_bytes_per_sec").asNumber();
        metrics.globalAverageSpeedBytesPerSec = root.at("global_average_speed_bytes_per_sec").asNumber();
        metrics.persistenceFlushCount = root.at("persistence_flush_count").asUint64();
        metrics.schedulerQueueSize = root.at("scheduler_queue_size").asUint64();
        metrics.schedulerPolicy = root.at("scheduler_policy").asString();
        metrics.updatedAt = parseTimestamp(root.at("updated_at").asString());
        metrics.stale = std::chrono::system_clock::now() - metrics.updatedAt > std::chrono::seconds(5);
        return metrics;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace cget
