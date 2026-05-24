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

}  // namespace

struct DownloadEngine::RateLimiter {
    explicit RateLimiter(std::uint64_t bytesPerSecond)
        : bytesPerSecond(bytesPerSecond), started(std::chrono::steady_clock::now()) {}

    void throttle(std::uint64_t bytes) {
        if (bytesPerSecond == 0 || bytes == 0) {
            return;
        }
        std::unique_lock lock(mutex);
        bytesSeen += bytes;
        const auto expected = std::chrono::duration<double>(static_cast<double>(bytesSeen) / bytesPerSecond);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        if (expected > elapsed) {
            std::this_thread::sleep_for(expected - elapsed);
        }
    }

    std::uint64_t bytesPerSecond = 0;
    std::uint64_t bytesSeen = 0;
    std::chrono::steady_clock::time_point started;
    std::mutex mutex;
};

DownloadEngine::DownloadEngine(std::unique_ptr<ProtocolHandler> protocol,
                               FileSystemService fileSystem,
                               PersistenceStore persistence,
                               Config config)
    : protocol_(std::move(protocol)),
      fileSystem_(std::move(fileSystem)),
      persistence_(std::move(persistence)),
      config_(config),
      logger_(fileSystem_) {}

void DownloadEngine::download(DownloadTask& task) {
    try {
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
        if (task.fileSize != 0 && info.fileSize != 0 && task.fileSize != info.fileSize) {
            setFailure(task, TaskStatus::MetadataMismatch, "remote file size changed");
            return;
        }

        task.fileSize = info.fileSize;
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
    RateLimiter limiter(config_.maxDownloadRateBytesPerSec);

    for (auto& chunk : task.chunks) {
        if (chunk.status == ChunkStatus::Completed || chunk.downloaded.load() >= chunk.size()) {
            chunk.status = ChunkStatus::Completed;
            continue;
        }
        workers.emplace_back([this, &task, &chunk, &errors, &errorsMutex, &limiter] {
            try {
                downloadChunkWithRetry(task, chunk, limiter);
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
    RateLimiter limiter(config_.maxDownloadRateBytesPerSec);
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
            limiter.throttle(bytes);
            const auto now = std::chrono::steady_clock::now();
            if (now - lastPersist >= std::chrono::seconds(1)) {
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
    for (std::uint32_t attempt = 0; attempt <= config_.maxRetries; ++attempt) {
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
                    limiter.throttle(bytes);
                    const auto now = std::chrono::steady_clock::now();
                    if (now - lastPersist >= std::chrono::seconds(1)) {
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
                error.code() == ErrorCode::HttpStatusError || attempt == config_.maxRetries) {
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
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.retryBaseDelayMs * multiplier));
        }
    }
}

void DownloadEngine::persistTask(DownloadTask& task) {
    std::lock_guard persistLock(persistMutex_);
    std::lock_guard taskLock(task.mutex);
    task.updatedAt = std::chrono::system_clock::now();
    persistence_.saveTask(task);
}

void DownloadEngine::setFailure(DownloadTask& task, TaskStatus status, const std::string& message) {
    std::lock_guard lock(task.mutex);
    task.status = status;
    task.lastError = message;
    task.updatedAt = std::chrono::system_clock::now();
    persistence_.saveTask(task);
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
    return std::clamp<std::uint32_t>(config_.maxThreads, 1, 32);
}

}  // namespace cget
