#include "cget/scheduler/scheduler.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <numeric>
#include <thread>

namespace cget {
namespace {

bool isSchedulableStatus(TaskStatus status) {
    return status == TaskStatus::Pending || status == TaskStatus::Queued || status == TaskStatus::Downloading ||
           status == TaskStatus::Retrying || status == TaskStatus::Failed;
}

}  // namespace

Scheduler::Scheduler(SchedulerConfig config) : config_(config) {
    config_.maxGlobalWorkers = std::max<std::uint32_t>(1, config_.maxGlobalWorkers);
    config_.maxConcurrentTasks = std::max<std::uint32_t>(1, config_.maxConcurrentTasks);
    config_.maxChunksPerTask = std::max<std::uint32_t>(1, config_.maxChunksPerTask);
    snapshot_.totalWorkers = config_.maxGlobalWorkers;
    snapshot_.maxChunksPerTask = config_.maxChunksPerTask;
    snapshot_.policy = toString(config_.policy);
}

SchedulerSnapshot Scheduler::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

const std::vector<ChunkJob>& Scheduler::scheduledJobs() const {
    return scheduledJobs_;
}

bool Scheduler::hasSchedulableChunk(const DownloadTask& task) const {
    std::lock_guard taskLock(task.mutex);
    if (!isSchedulableStatus(task.status)) {
        return false;
    }
    return std::any_of(task.chunks.begin(), task.chunks.end(), [](const Chunk& chunk) {
        return (chunk.status == ChunkStatus::Pending || chunk.status == ChunkStatus::Paused ||
                chunk.status == ChunkStatus::Failed) &&
               chunk.downloaded.load() < chunk.size();
    });
}

std::optional<std::size_t> Scheduler::selectNextChunk(DownloadTask& task) const {
    std::lock_guard taskLock(task.mutex);
    for (std::size_t index = 0; index < task.chunks.size(); ++index) {
        auto& chunk = task.chunks[index];
        if ((chunk.status == ChunkStatus::Pending || chunk.status == ChunkStatus::Paused ||
             chunk.status == ChunkStatus::Failed) &&
            chunk.downloaded.load() < chunk.size()) {
            return index;
        }
    }
    return std::nullopt;
}

void Scheduler::run(std::vector<DownloadTask>& tasks, const ChunkExecutor& executor) {
    std::mutex stateMutex;
    std::condition_variable cv;
    std::unordered_map<TaskId, TaskQuota> quotas;
    std::vector<std::thread> workers;
    std::uint32_t busyWorkers = 0;
    std::uint32_t activeTasks = 0;
    std::atomic<bool> workerFailed{false};
    {
        std::lock_guard lock(mutex_);
        scheduledJobs_.clear();
        snapshot_.totalWorkers = config_.maxGlobalWorkers;
        snapshot_.maxChunksPerTask = config_.maxChunksPerTask;
        snapshot_.policy = toString(config_.policy);
    }

    auto unfinished = [&] {
        return std::any_of(tasks.begin(), tasks.end(), [&](const DownloadTask& task) {
            return hasSchedulableChunk(task) || quotas[task.id].runningChunks > 0;
        });
    };

    auto countQueuedTasks = [&] {
        return static_cast<std::uint32_t>(std::count_if(tasks.begin(), tasks.end(), [&](const DownloadTask& task) {
            return hasSchedulableChunk(task) && quotas[task.id].runningChunks == 0;
        }));
    };

    while (true) {
        bool scheduled = false;
        {
            std::unique_lock lock(stateMutex);
            if (workerFailed.load()) {
                break;
            }
            if (!unfinished()) {
                break;
            }

            std::vector<std::size_t> taskOrder(tasks.size());
            std::iota(taskOrder.begin(), taskOrder.end(), 0);
            if (config_.policy == SchedulingPolicyType::SmallTaskFirst) {
                std::stable_sort(taskOrder.begin(), taskOrder.end(), [&](std::size_t lhs, std::size_t rhs) {
                    return tasks[lhs].fileSize < tasks[rhs].fileSize;
                });
            }

            for (const auto taskIndex : taskOrder) {
                auto& task = tasks[taskIndex];
                if (busyWorkers >= config_.maxGlobalWorkers) {
                    break;
                }
                auto& quota = quotas[task.id];
                if (quota.taskId.empty()) {
                    quota.taskId = task.id;
                    quota.maxConcurrentChunks = task.schedulerMaxChunks != 0 ? task.schedulerMaxChunks
                                                                             : config_.maxChunksPerTask;
                }
                if (!hasSchedulableChunk(task) || quota.runningChunks >= quota.maxConcurrentChunks) {
                    continue;
                }
                if (quota.runningChunks == 0 && activeTasks >= config_.maxConcurrentTasks) {
                    continue;
                }
                const auto chunkIndex = selectNextChunk(task);
                if (!chunkIndex) {
                    continue;
                }

                auto& chunk = task.chunks[*chunkIndex];
                {
                    std::lock_guard taskLock(task.mutex);
                    chunk.status = ChunkStatus::Downloading;
                    task.status = TaskStatus::Downloading;
                }
                if (quota.runningChunks == 0) {
                    ++activeTasks;
                }
                ++quota.runningChunks;
                ++busyWorkers;
                scheduled = true;
                {
                    std::lock_guard snapshotLock(mutex_);
                    snapshot_.busyWorkers = busyWorkers;
                    snapshot_.activeTasks = activeTasks;
                    snapshot_.queuedTasks = countQueuedTasks();
                    snapshot_.taskQueueSize = snapshot_.queuedTasks;
                    snapshot_.chunkQueueSize = 0;
                    snapshot_.workerUtilization = snapshot_.totalWorkers == 0
                                                      ? 0.0
                                                      : (static_cast<double>(busyWorkers) / snapshot_.totalWorkers) * 100.0;
                    scheduledJobs_.push_back(ChunkJob{task.id, chunk.index, task.schedulerPriority,
                                                      std::chrono::steady_clock::now()});
                }

                workers.emplace_back([&, taskId = task.id, chunkVectorIndex = *chunkIndex] {
                    try {
                        auto taskIt = std::find_if(tasks.begin(), tasks.end(), [&](const DownloadTask& candidate) {
                            return candidate.id == taskId;
                        });
                        if (taskIt != tasks.end()) {
                            executor(*taskIt, taskIt->chunks.at(chunkVectorIndex));
                        }
                    } catch (...) {
                        workerFailed.store(true);
                        auto taskIt = std::find_if(tasks.begin(), tasks.end(), [&](const DownloadTask& candidate) {
                            return candidate.id == taskId;
                        });
                        if (taskIt != tasks.end()) {
                            std::lock_guard taskLock(taskIt->mutex);
                            taskIt->chunks.at(chunkVectorIndex).status = ChunkStatus::Failed;
                            taskIt->status = TaskStatus::Failed;
                            taskIt->lastError = "scheduled worker failed";
                        }
                    }
                    {
                        std::lock_guard doneLock(stateMutex);
                        auto& doneQuota = quotas[taskId];
                        if (doneQuota.runningChunks > 0) {
                            --doneQuota.runningChunks;
                            if (doneQuota.runningChunks == 0 && activeTasks > 0) {
                                --activeTasks;
                            }
                        }
                        if (busyWorkers > 0) {
                            --busyWorkers;
                        }
                        {
                            std::lock_guard snapshotLock(mutex_);
                            snapshot_.busyWorkers = busyWorkers;
                            snapshot_.activeTasks = activeTasks;
                            snapshot_.queuedTasks = countQueuedTasks();
                            snapshot_.taskQueueSize = snapshot_.queuedTasks;
                            snapshot_.workerUtilization = snapshot_.totalWorkers == 0
                                                              ? 0.0
                                                              : (static_cast<double>(busyWorkers) / snapshot_.totalWorkers) * 100.0;
                        }
                    }
                    cv.notify_all();
                });
            }
            if (!scheduled) {
                cv.wait_for(lock, std::chrono::milliseconds(10));
            }
        }
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    if (workerFailed.load()) {
        for (auto& task : tasks) {
            if (task.status == TaskStatus::Downloading) {
                task.status = TaskStatus::Failed;
                task.lastError = "scheduled worker failed";
            }
        }
    }
    std::lock_guard snapshotLock(mutex_);
    snapshot_.busyWorkers = 0;
    snapshot_.activeTasks = 0;
    snapshot_.queuedTasks = 0;
    snapshot_.taskQueueSize = 0;
    snapshot_.chunkQueueSize = 0;
    snapshot_.workerUtilization = 0.0;
}

}  // namespace cget
