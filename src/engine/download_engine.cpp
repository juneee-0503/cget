#include "cget/engine/download_engine.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>

#include "cget/core/chunk_planner.h"
#include "cget/core/errors.h"
#include "cget/crypto/sha256.h"

namespace cget {
namespace {

std::uint64_t recomputeDownloaded(const DownloadTask& task) {
    std::uint64_t total = 0;
    for (const auto& chunk : task.chunks) {
        total += chunk.downloaded.load();
    }
    return total;
}

LoggerOptions loggerOptionsFromConfig(const Config& config) {
    LoggerOptions options;
    options.level = logLevelFromString(config.logging.level);
    options.console = config.logging.console;
    options.maxFileBytes = config.logging.maxFileBytes;
    options.maxRotatedFiles = config.logging.maxRotatedFiles;
    return options;
}

bool metadataChanged(const DownloadTask& task, const RemoteFileInfo& info) {
    if (task.fileSize != 0 && info.fileSize != 0 && task.fileSize != info.fileSize) {
        return true;
    }
    if (task.remoteEtag && info.etag && *task.remoteEtag != *info.etag) {
        return true;
    }
    if (task.remoteLastModified && info.lastModified && *task.remoteLastModified != *info.lastModified) {
        return true;
    }
    return false;
}

void rememberMetadata(DownloadTask& task, const RemoteFileInfo& info) {
    if (info.fileSize != 0) {
        task.fileSize = info.fileSize;
    }
    if (info.etag) {
        task.remoteEtag = info.etag;
    }
    if (info.lastModified) {
        task.remoteLastModified = info.lastModified;
    }
    if (!info.finalUrl.empty()) {
        task.finalUrl = info.finalUrl;
    }
}

std::optional<std::uint64_t> globalRateLimitFromConfig(const Config& config) {
    if (config.rateLimit.globalBytesPerSec) {
        return config.rateLimit.globalBytesPerSec;
    }
    if (config.download.maxDownloadRateBytesPerSec != 0) {
        return config.download.maxDownloadRateBytesPerSec;
    }
    return std::nullopt;
}

SchedulerSnapshot singleTaskSchedulerSnapshot(const Config& config) {
    SchedulerSnapshot snapshot;
    snapshot.totalWorkers = std::max<std::uint32_t>(1, config.scheduler.maxGlobalWorkers);
    snapshot.maxChunksPerTask = std::max<std::uint32_t>(1, config.scheduler.maxChunksPerTask);
    snapshot.policy = toString(config.scheduler.policy);
    return snapshot;
}

}  // namespace

DownloadEngine::DownloadEngine(std::unique_ptr<ProtocolHandler> protocol,
                               FileSystemService fileSystem,
                               PersistenceStore persistence,
                               Config config)
    : DownloadEngine(std::move(protocol),
                     std::move(fileSystem),
                     std::move(persistence),
                     config,
                     std::make_shared<RateLimiter>(globalRateLimitFromConfig(config))) {}

DownloadEngine::DownloadEngine(std::unique_ptr<ProtocolHandler> protocol,
                               FileSystemService fileSystem,
                               PersistenceStore persistence,
                               Config config,
                               std::shared_ptr<RateLimiter> rateLimiter)
    : protocol_(std::move(protocol)),
      fileSystem_(std::move(fileSystem)),
      persistence_(std::move(persistence)),
      config_(config),
      logger_(fileSystem_, loggerOptionsFromConfig(config)),
      metrics_(fileSystem_),
      rateLimiter_(std::move(rateLimiter)),
      lastMetricsFlush_() {}

void DownloadEngine::download(DownloadTask& task) {
    try {
        applyTaskRateLimit(task);
        {
            std::lock_guard lock(task.mutex);
            task.status = TaskStatus::Downloading;
            if (!task.startedAt) {
                task.startedAt = std::chrono::system_clock::now();
            }
            task.lastError.reset();
        }
        persistTask(task);
        logger_.info("started task " + task.id + " for " + task.url);

        const auto info = protocol_->fetchMetadata(task.url);
        if (metadataChanged(task, info)) {
            setFailure(task, TaskStatus::MetadataMismatch, "remote file metadata changed");
            return;
        }

        rememberMetadata(task, info);
        persistTask(task);
        if (task.fileSize == 0) {
            downloadSingleStream(task, info);
        } else if (info.supportsRange && normalizedThreadCount(task) > 1) {
            downloadWithRange(task, info);
        } else {
            downloadSingleStream(task, info);
        }
    } catch (const CgetError& error) {
        setFailure(task, error.code() == ErrorCode::MetadataMismatchError ? TaskStatus::MetadataMismatch
                                                                          : TaskStatus::Failed,
                   error.what());
    } catch (const std::exception& error) {
        setFailure(task, TaskStatus::Failed, error.what());
    }
}

void DownloadEngine::prepareTask(DownloadTask& task) {
    try {
        applyTaskRateLimit(task);
        {
            std::lock_guard lock(task.mutex);
            task.status = TaskStatus::Downloading;
            if (!task.startedAt) {
                task.startedAt = std::chrono::system_clock::now();
            }
            task.lastError.reset();
        }
        persistTask(task);

        const auto info = protocol_->fetchMetadata(task.url);
        if (metadataChanged(task, info)) {
            setFailure(task, TaskStatus::MetadataMismatch, "remote file metadata changed");
            return;
        }

        rememberMetadata(task, info);
        if (task.fileSize > 0 && info.supportsRange && normalizedThreadCount(task) > 1) {
            fileSystem_.ensureTaskTempDir(task.id);
            if (task.chunks.empty()) {
                task.chunks = ChunkPlanner::plan(task.fileSize, normalizedThreadCount(task));
            }
            task.downloaded.store(recomputeDownloaded(task));
        }
        persistTask(task);
    } catch (const CgetError& error) {
        setFailure(task, error.code() == ErrorCode::MetadataMismatchError ? TaskStatus::MetadataMismatch
                                                                          : TaskStatus::Failed,
                   error.what());
    } catch (const std::exception& error) {
        setFailure(task, TaskStatus::Failed, error.what());
    }
}

void DownloadEngine::downloadPreparedChunk(DownloadTask& task, Chunk& chunk) {
    applyTaskRateLimit(task);
    downloadChunkWithRetry(task, chunk, *rateLimiter_);
}

void DownloadEngine::finalizePreparedTask(DownloadTask& task) {
    if (task.status == TaskStatus::Failed || task.status == TaskStatus::MetadataMismatch ||
        task.status == TaskStatus::Corrupted || task.status == TaskStatus::PendingRecovery) {
        persistTask(task);
        return;
    }
    const bool complete = std::all_of(task.chunks.begin(), task.chunks.end(), [](const Chunk& chunk) {
        return chunk.status == ChunkStatus::Completed && chunk.downloaded.load() == chunk.size();
    });
    if (!complete) {
        setFailure(task, TaskStatus::Failed, "not all scheduled chunks completed");
        return;
    }
    fileSystem_.mergeChunks(task.chunks, task.tempDir, task.targetPath, task.fileSize);
    if (!verifyChecksum(task)) {
        return;
    }
    {
        std::lock_guard lock(task.mutex);
        task.downloaded.store(task.fileSize);
        task.status = TaskStatus::Completed;
        task.lastError.reset();
    }
    persistTask(task);
    logger_.info("completed scheduled task " + task.id + " -> " + task.targetPath.string());
}

void DownloadEngine::downloadWithRange(DownloadTask& task, const RemoteFileInfo& info) {
    fileSystem_.ensureTaskTempDir(task.id);
    if (task.chunks.empty()) {
        task.chunks = ChunkPlanner::plan(info.fileSize, normalizedThreadCount(task));
    }

    task.downloaded.store(recomputeDownloaded(task));
    persistTask(task);

    struct WorkerError {
        ErrorCode code;
        std::string message;
    };

    std::mutex errorsMutex;
    std::vector<WorkerError> errors;
    std::vector<std::thread> workers;

    for (auto& chunk : task.chunks) {
        if (chunk.status == ChunkStatus::Completed || chunk.downloaded.load() >= chunk.size()) {
            chunk.status = ChunkStatus::Completed;
            continue;
        }
        workers.emplace_back([this, &task, &chunk, &errors, &errorsMutex] {
            try {
                downloadChunkWithRetry(task, chunk, *rateLimiter_);
            } catch (const CgetError& error) {
                std::lock_guard lock(errorsMutex);
                errors.push_back(WorkerError{error.code(), error.what()});
            } catch (const std::exception& error) {
                std::lock_guard lock(errorsMutex);
                errors.push_back(WorkerError{ErrorCode::NetworkError, error.what()});
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    if (!errors.empty()) {
        const auto rangeFallback = std::any_of(errors.begin(), errors.end(), [](const WorkerError& error) {
            return error.code == ErrorCode::RangeNotSupportedError;
        });
        if (rangeFallback) {
            {
                std::lock_guard lock(task.mutex);
                task.chunks.clear();
                task.downloaded.store(0);
                task.lastError = "server ignored Range request; falling back to single-stream download";
            }
            persistTask(task);
            downloadSingleStream(task, info);
            return;
        }

        setFailure(task,
                   errors.front().code == ErrorCode::MetadataMismatchError ? TaskStatus::MetadataMismatch
                                                                           : TaskStatus::Failed,
                   errors.front().message);
        return;
    }

    const bool complete = std::all_of(task.chunks.begin(), task.chunks.end(), [](const Chunk& chunk) {
        return chunk.status == ChunkStatus::Completed && chunk.downloaded.load() == chunk.size();
    });
    if (!complete) {
        setFailure(task, TaskStatus::Failed, "not all chunks completed");
        return;
    }

    fileSystem_.mergeChunks(task.chunks, task.tempDir, task.targetPath, task.fileSize);
    if (!verifyChecksum(task)) {
        return;
    }
    {
        std::lock_guard lock(task.mutex);
        task.downloaded.store(task.fileSize);
        task.status = TaskStatus::Completed;
        task.lastError.reset();
    }
    persistTask(task);
    logger_.info("completed ranged task " + task.id + " -> " + task.targetPath.string());
}

void DownloadEngine::downloadSingleStream(DownloadTask& task, const RemoteFileInfo& info) {
    fileSystem_.ensureTaskTempDir(task.id);
    const auto tempPath = task.tempDir / "single.part";
    fileSystem_.truncateFile(tempPath, 0);
    {
        std::lock_guard lock(task.mutex);
        task.chunks.clear();
        if (info.fileSize > 0) {
            task.chunks.emplace_back(0, 0, info.fileSize - 1, 0, ChunkStatus::Downloading, 0, "single.part");
        }
        task.downloaded.store(0);
    }
    persistTask(task);

    std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw CgetError(ErrorCode::PermissionDeniedError, "failed to open temp file: " + tempPath.string());
    }

    auto lastPersist = std::chrono::steady_clock::now();
    protocol_->downloadSingle(
        task.url,
        [&](const char* data, std::size_t size) {
            output.write(data, static_cast<std::streamsize>(size));
            if (!output) {
                throw CgetError(ErrorCode::FileSystemError, "failed to write temp file");
            }
        },
        [&](std::uint64_t bytes) {
            if (!task.chunks.empty()) {
                task.chunks[0].downloaded.fetch_add(bytes);
            }
            task.downloaded.fetch_add(bytes);
            rateLimiter_->acquire(task.id, bytes);
            const auto now = std::chrono::steady_clock::now();
            if (now - lastPersist >= std::chrono::milliseconds(config_.persistence.flushIntervalMs)) {
                lastPersist = now;
                persistTask(task);
            }
        });
    output.close();

    const auto downloaded = fileSystem_.fileSize(tempPath);
    if (info.fileSize != 0 && downloaded != info.fileSize) {
        throw CgetError(ErrorCode::FileSystemError,
                       "downloaded file size mismatch: expected " + std::to_string(info.fileSize) +
                           ", got " + std::to_string(downloaded));
    }

    if (task.chunks.empty() && downloaded > 0) {
        task.chunks.emplace_back(0, 0, downloaded - 1, downloaded, ChunkStatus::Completed, 0, "single.part");
    } else if (!task.chunks.empty()) {
        task.chunks[0].downloaded.store(downloaded);
        task.chunks[0].status = ChunkStatus::Completed;
    }
    task.fileSize = info.fileSize == 0 ? downloaded : info.fileSize;
    task.downloaded.store(task.fileSize);
    fileSystem_.mergeChunks(task.chunks, task.tempDir, task.targetPath, task.fileSize);
    if (!verifyChecksum(task)) {
        return;
    }
    {
        std::lock_guard lock(task.mutex);
        task.status = TaskStatus::Completed;
        task.lastError.reset();
    }
    persistTask(task);
    logger_.info("completed single-stream task " + task.id + " -> " + task.targetPath.string());
}

void DownloadEngine::downloadChunkWithRetry(DownloadTask& task, Chunk& chunk, RateLimiter& limiter) {
    for (std::uint32_t attempt = 0; attempt <= config_.network.maxRetries; ++attempt) {
        try {
            const auto existing = fileSystem_.fileSize(fileSystem_.chunkPath(task, chunk));
            if (existing > chunk.size()) {
                fileSystem_.truncateFile(fileSystem_.chunkPath(task, chunk), chunk.size());
                chunk.downloaded.store(chunk.size());
            } else {
                chunk.downloaded.store(existing);
            }

            const auto already = chunk.downloaded.load();
            if (already >= chunk.size()) {
                std::lock_guard lock(task.mutex);
                chunk.status = ChunkStatus::Completed;
                task.downloaded.store(recomputeDownloaded(task));
                return;
            }

            const std::uint64_t actualStart = chunk.start + already;
            const auto partPath = fileSystem_.chunkPath(task, chunk);
            std::ofstream output(partPath, std::ios::binary | std::ios::app);
            if (!output) {
                throw CgetError(ErrorCode::PermissionDeniedError, "failed to open chunk file: " + partPath.string());
            }

            {
                std::lock_guard lock(task.mutex);
                chunk.status = ChunkStatus::Downloading;
                task.status = TaskStatus::Downloading;
            }
            persistTask(task);

            auto lastPersist = std::chrono::steady_clock::now();
            protocol_->downloadRange(
                task.url,
                actualStart,
                chunk.end,
                [&](const char* data, std::size_t size) {
                    output.write(data, static_cast<std::streamsize>(size));
                    if (!output) {
                        throw CgetError(ErrorCode::FileSystemError, "failed to write chunk file");
                    }
                },
                [&](std::uint64_t bytes) {
                    chunk.downloaded.fetch_add(bytes);
                    task.downloaded.fetch_add(bytes);
                    limiter.acquire(task.id, bytes);
                    const auto now = std::chrono::steady_clock::now();
                    if (now - lastPersist >= std::chrono::milliseconds(config_.persistence.flushIntervalMs)) {
                        lastPersist = now;
                        persistTask(task);
                    }
                });
            output.close();

            const auto actual = fileSystem_.fileSize(partPath);
            if (actual != chunk.size()) {
                throw CgetError(ErrorCode::NetworkError,
                               "chunk " + std::to_string(chunk.index) + " size mismatch after download");
            }
            {
                std::lock_guard lock(task.mutex);
                chunk.downloaded.store(chunk.size());
                chunk.status = ChunkStatus::Completed;
                task.downloaded.store(recomputeDownloaded(task));
            }
            persistTask(task);
            return;
        } catch (const CgetError& error) {
            if (error.code() == ErrorCode::RangeNotSupportedError || error.code() == ErrorCode::MetadataMismatchError ||
                error.code() == ErrorCode::HttpStatusError || attempt == config_.network.maxRetries) {
                std::lock_guard lock(task.mutex);
                chunk.status = ChunkStatus::Failed;
                chunk.retryCount = attempt;
                throw;
            }
            {
                std::lock_guard lock(task.mutex);
                chunk.status = ChunkStatus::Failed;
                chunk.retryCount = attempt + 1;
                task.status = TaskStatus::Retrying;
                task.lastError = error.what();
            }
            persistTask(task);
            const auto multiplier = static_cast<std::uint32_t>(1U << std::min<std::uint32_t>(attempt, 10));
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.network.retryBaseDelayMs * multiplier));
        }
    }
}

void DownloadEngine::applyTaskRateLimit(const DownloadTask& task) {
    if (!rateLimiter_) {
        return;
    }
    rateLimiter_->setTaskLimit(task.id, task.taskRateLimitBytesPerSec ? task.taskRateLimitBytesPerSec
                                                                      : config_.rateLimit.defaultPerTaskBytesPerSec);
}

void DownloadEngine::persistTask(DownloadTask& task) {
    {
        std::lock_guard persistLock(persistMutex_);
        std::lock_guard taskLock(task.mutex);
        task.updatedAt = std::chrono::system_clock::now();
        persistence_.saveTask(task);
    }
    refreshMetrics(task);
}

void DownloadEngine::refreshMetrics(const DownloadTask& task) {
    if (!config_.metrics.enabled) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    try {
        TaskSnapshot snapshot;
        {
            std::lock_guard lock(task.mutex);
            snapshot = snapshotTask(task);
        }
        const bool force = snapshot.status == TaskStatus::Completed || snapshot.status == TaskStatus::Failed ||
                           snapshot.status == TaskStatus::MetadataMismatch ||
                           snapshot.status == TaskStatus::PendingRecovery ||
                           snapshot.status == TaskStatus::Corrupted;
        if (!force && now - lastMetricsFlush_ < std::chrono::milliseconds(config_.metrics.sampleIntervalMs)) {
            return;
        }
        lastMetricsFlush_ = now;
        metrics_.sample({snapshot}, singleTaskSchedulerSnapshot(config_));
        metrics_.saveRuntimeSnapshot();
    } catch (...) {
    }
}

void DownloadEngine::setFailure(DownloadTask& task, TaskStatus status, const std::string& message) {
    {
        std::lock_guard lock(task.mutex);
        task.status = status;
        task.lastError = message;
        task.updatedAt = std::chrono::system_clock::now();
        persistence_.saveTask(task);
    }
    refreshMetrics(task);
    logger_.error("task " + task.id + " failed with status " + toString(status) + ": " + message);
}

bool DownloadEngine::verifyChecksum(DownloadTask& task) {
    if (!task.expectedSha256) {
        return true;
    }
    const auto actual = sha256File(task.targetPath);
    const auto expected = lowercaseHex(*task.expectedSha256);
    if (actual != expected) {
        setFailure(task, TaskStatus::Failed,
                   "SHA256 mismatch: expected " + expected + ", got " + actual);
        return false;
    }
    logger_.info("verified SHA256 for task " + task.id + ": " + actual);
    return true;
}

std::uint32_t DownloadEngine::normalizedThreadCount(const DownloadTask& task) const {
    if (task.requestedThreads != 0) {
        return std::clamp<std::uint32_t>(task.requestedThreads, 1, 32);
    }
    return std::clamp<std::uint32_t>(config_.download.maxThreads, 1, 32);
}

}  // namespace cget
