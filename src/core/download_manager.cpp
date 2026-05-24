#include "cget/core/download_manager.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

#include "cget/core/errors.h"
#include "cget/crypto/sha256.h"
#include "cget/engine/download_engine.h"
#include "cget/network/protocol_registry.h"

namespace cget {

DownloadManager::DownloadManager() : DownloadManager(FileSystemService()) {}

DownloadManager::DownloadManager(FileSystemService fileSystem)
    : fileSystem_(std::move(fileSystem)),
      persistence_(fileSystem_),
      config_(fileSystem_),
      logger_(fileSystem_) {}

DownloadTask DownloadManager::createTask(const CreateTaskRequest& request) {
    validateUrl(request.url);
    fileSystem_.ensureDirectories();

    DownloadTask task;
    task.id = generateTaskId();
    task.url = request.url;
    task.fileName = request.fileName.value_or(fileSystem_.fileNameFromUrl(request.url));
    if (request.expectedSha256) {
        if (!isSha256Hex(*request.expectedSha256)) {
            throw CgetError(ErrorCode::InvalidCommandError, "SHA256 must be exactly 64 hexadecimal characters");
        }
        task.expectedSha256 = lowercaseHex(*request.expectedSha256);
    }
    task.targetPath = fileSystem_.resolveOutputPath(request.outputPath, task.fileName, request.forceOverwrite);
    task.tempDir = fileSystem_.taskTempDir(task.id);
    task.status = request.queueOnly ? TaskStatus::Queued : TaskStatus::Pending;
    task.requestedThreads = request.requestedThreads;
    task.createdAt = std::chrono::system_clock::now();
    task.updatedAt = task.createdAt;

    fileSystem_.ensureTaskTempDir(task.id);
    persistence_.saveTask(task);
    logger_.info("created task " + task.id + " for " + task.url);
    return task;
}

void DownloadManager::downloadTask(DownloadTask& task) {
    if (task.status == TaskStatus::Completed) {
        return;
    }
    task.status = TaskStatus::Queued;
    persistence_.saveTask(task);
    const auto config = config_.load();
    DownloadEngine engine(ProtocolRegistry::create(task.url, config.proxyUrl.value_or("")), fileSystem_, persistence_,
                          config);
    engine.download(task);
}

void DownloadManager::pauseTask(const TaskId& id) {
    auto task = persistence_.loadTask(id);
    if (task.status == TaskStatus::Completed || task.status == TaskStatus::Removed ||
        task.status == TaskStatus::Corrupted) {
        throw CgetError(ErrorCode::InvalidStateTransitionError, "cannot pause task in state " + toString(task.status));
    }
    task.status = TaskStatus::Paused;
    task.lastError.reset();
    for (auto& chunk : task.chunks) {
        if (chunk.status == ChunkStatus::Downloading || chunk.status == ChunkStatus::Failed) {
            chunk.status = ChunkStatus::Paused;
        }
    }
    task.updatedAt = std::chrono::system_clock::now();
    persistence_.saveTask(task);
    logger_.info("paused task " + id);
}

void DownloadManager::removeTask(const TaskId& id) {
    auto task = persistence_.loadTask(id);
    task.status = TaskStatus::Removed;
    task.updatedAt = std::chrono::system_clock::now();
    persistence_.saveTask(task);
    persistence_.removeTask(id);
    fileSystem_.removeTaskTempDir(id);
    logger_.info("removed task " + id);
}

std::vector<TaskSnapshot> DownloadManager::runQueuedTasks() {
    (void)persistence_.recoverTasks();
    auto tasks = persistence_.loadAllTasks();
    std::vector<DownloadTask> runnable;
    for (auto& task : tasks) {
        if (task.status == TaskStatus::Pending || task.status == TaskStatus::Queued ||
            task.status == TaskStatus::Retrying || task.status == TaskStatus::Failed) {
            runnable.push_back(std::move(task));
        }
    }

    const auto config = config_.load();
    const auto workerCount = std::min<std::size_t>(std::max<std::uint32_t>(1, config.maxActiveTasks), runnable.size());
    std::atomic<std::size_t> next{0};
    std::mutex resultsMutex;
    std::vector<TaskSnapshot> results;
    std::vector<std::thread> workers;

    for (std::size_t i = 0; i < workerCount; ++i) {
        workers.emplace_back([this, &runnable, &next, &results, &resultsMutex] {
            while (true) {
                const auto index = next.fetch_add(1);
                if (index >= runnable.size()) {
                    return;
                }
                auto& task = runnable[index];
                downloadTask(task);
                std::lock_guard lock(resultsMutex);
                results.push_back(snapshotTask(task));
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    logger_.info("run completed " + std::to_string(results.size()) + " queued task(s)");
    return results;
}

DownloadTask DownloadManager::loadTask(const TaskId& id) const {
    auto task = persistence_.loadTask(id);
    return task;
}

std::vector<TaskSnapshot> DownloadManager::listTasks() const {
    std::vector<TaskSnapshot> snapshots;
    for (const auto& task : persistence_.loadAllTasks()) {
        snapshots.push_back(snapshotTask(task));
    }
    return snapshots;
}

TaskSnapshot DownloadManager::getTaskSnapshot(const TaskId& id) const {
    return snapshotTask(persistence_.loadTask(id));
}

std::vector<DownloadTask> DownloadManager::recoverTasks() const {
    return persistence_.recoverTasks();
}

TaskId DownloadManager::generateTaskId() const {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::random_device rd;
    std::mt19937 generator(rd());
    std::uniform_int_distribution<int> distribution(1000, 9999);
    std::ostringstream id;
    id << millis << distribution(generator);
    return id.str();
}

void DownloadManager::validateUrl(const std::string& url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        throw CgetError(ErrorCode::InvalidUrlError, "URL must include a supported scheme");
    }
    const auto scheme = url.substr(0, schemeEnd);
    if (!ProtocolRegistry::isSupportedScheme(scheme)) {
        throw CgetError(ErrorCode::InvalidUrlError,
                       "unsupported or unavailable URL scheme: " + scheme +
                           "; supported here: " + ProtocolRegistry::supportedSchemesText());
    }
    const auto hostStart = schemeEnd + 3;
    const auto hostEnd = url.find_first_of("/?#", hostStart);
    const auto host = url.substr(hostStart, hostEnd == std::string::npos ? std::string::npos : hostEnd - hostStart);
    if (host.empty()) {
        throw CgetError(ErrorCode::InvalidUrlError, "URL host is required");
    }
}

}  // namespace cget
