#include "cget/core/types.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cget {
namespace {

std::time_t timegmPortable(std::tm* tm) {
#if defined(_WIN32)
    return _mkgmtime(tm);
#else
    return timegm(tm);
#endif
}

std::tm gmtimePortable(std::time_t value) {
    std::tm out{};
#if defined(_WIN32)
    gmtime_s(&out, &value);
#else
    gmtime_r(&value, &out);
#endif
    return out;
}

}  // namespace

Chunk::Chunk(std::uint64_t chunkIndex,
             std::uint64_t chunkStart,
             std::uint64_t chunkEnd,
             std::uint64_t chunkDownloaded,
             ChunkStatus chunkStatus,
             std::uint32_t retries,
             std::filesystem::path chunkTempPath)
    : index(chunkIndex),
      start(chunkStart),
      end(chunkEnd),
      downloaded(chunkDownloaded),
      status(chunkStatus),
      retryCount(retries),
      tempPath(std::move(chunkTempPath)) {}

Chunk::Chunk(const Chunk& other)
    : index(other.index),
      start(other.start),
      end(other.end),
      downloaded(other.downloaded.load()),
      status(other.status),
      retryCount(other.retryCount),
      tempPath(other.tempPath) {}

Chunk& Chunk::operator=(const Chunk& other) {
    if (this == &other) {
        return *this;
    }
    index = other.index;
    start = other.start;
    end = other.end;
    downloaded.store(other.downloaded.load());
    status = other.status;
    retryCount = other.retryCount;
    tempPath = other.tempPath;
    return *this;
}

Chunk::Chunk(Chunk&& other) noexcept
    : index(other.index),
      start(other.start),
      end(other.end),
      downloaded(other.downloaded.load()),
      status(other.status),
      retryCount(other.retryCount),
      tempPath(std::move(other.tempPath)) {}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    index = other.index;
    start = other.start;
    end = other.end;
    downloaded.store(other.downloaded.load());
    status = other.status;
    retryCount = other.retryCount;
    tempPath = std::move(other.tempPath);
    return *this;
}

std::uint64_t Chunk::size() const {
    if (end < start) {
        return 0;
    }
    return end - start + 1;
}

DownloadTask::DownloadTask()
    : createdAt(std::chrono::system_clock::now()),
      updatedAt(createdAt) {}

DownloadTask::DownloadTask(const DownloadTask& other)
    : id(other.id),
      url(other.url),
      fileName(other.fileName),
      targetPath(other.targetPath),
      tempDir(other.tempDir),
      status(other.status),
      fileSize(other.fileSize),
      downloaded(other.downloaded.load()),
      chunks(other.chunks),
      createdAt(other.createdAt),
      updatedAt(other.updatedAt),
      startedAt(other.startedAt),
      lastError(other.lastError),
      expectedSha256(other.expectedSha256),
      remoteEtag(other.remoteEtag),
      remoteLastModified(other.remoteLastModified),
      finalUrl(other.finalUrl),
      taskRateLimitBytesPerSec(other.taskRateLimitBytesPerSec),
      schedulerMaxChunks(other.schedulerMaxChunks),
      schedulerPriority(other.schedulerPriority),
      peakSpeedBytesPerSec(other.peakSpeedBytesPerSec),
      failedChunks(other.failedChunks),
      persistedRetryCount(other.persistedRetryCount),
      requestedThreads(other.requestedThreads) {}

DownloadTask& DownloadTask::operator=(const DownloadTask& other) {
    if (this == &other) {
        return *this;
    }
    id = other.id;
    url = other.url;
    fileName = other.fileName;
    targetPath = other.targetPath;
    tempDir = other.tempDir;
    status = other.status;
    fileSize = other.fileSize;
    downloaded.store(other.downloaded.load());
    chunks = other.chunks;
    createdAt = other.createdAt;
    updatedAt = other.updatedAt;
    startedAt = other.startedAt;
    lastError = other.lastError;
    expectedSha256 = other.expectedSha256;
    remoteEtag = other.remoteEtag;
    remoteLastModified = other.remoteLastModified;
    finalUrl = other.finalUrl;
    taskRateLimitBytesPerSec = other.taskRateLimitBytesPerSec;
    schedulerMaxChunks = other.schedulerMaxChunks;
    schedulerPriority = other.schedulerPriority;
    peakSpeedBytesPerSec = other.peakSpeedBytesPerSec;
    failedChunks = other.failedChunks;
    persistedRetryCount = other.persistedRetryCount;
    requestedThreads = other.requestedThreads;
    return *this;
}

DownloadTask::DownloadTask(DownloadTask&& other) noexcept
    : id(std::move(other.id)),
      url(std::move(other.url)),
      fileName(std::move(other.fileName)),
      targetPath(std::move(other.targetPath)),
      tempDir(std::move(other.tempDir)),
      status(other.status),
      fileSize(other.fileSize),
      downloaded(other.downloaded.load()),
      chunks(std::move(other.chunks)),
      createdAt(other.createdAt),
      updatedAt(other.updatedAt),
      startedAt(other.startedAt),
      lastError(std::move(other.lastError)),
      expectedSha256(std::move(other.expectedSha256)),
      remoteEtag(std::move(other.remoteEtag)),
      remoteLastModified(std::move(other.remoteLastModified)),
      finalUrl(std::move(other.finalUrl)),
      taskRateLimitBytesPerSec(std::move(other.taskRateLimitBytesPerSec)),
      schedulerMaxChunks(other.schedulerMaxChunks),
      schedulerPriority(other.schedulerPriority),
      peakSpeedBytesPerSec(other.peakSpeedBytesPerSec),
      failedChunks(other.failedChunks),
      persistedRetryCount(other.persistedRetryCount),
      requestedThreads(other.requestedThreads) {}

DownloadTask& DownloadTask::operator=(DownloadTask&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    id = std::move(other.id);
    url = std::move(other.url);
    fileName = std::move(other.fileName);
    targetPath = std::move(other.targetPath);
    tempDir = std::move(other.tempDir);
    status = other.status;
    fileSize = other.fileSize;
    downloaded.store(other.downloaded.load());
    chunks = std::move(other.chunks);
    createdAt = other.createdAt;
    updatedAt = other.updatedAt;
    startedAt = other.startedAt;
    lastError = std::move(other.lastError);
    expectedSha256 = std::move(other.expectedSha256);
    remoteEtag = std::move(other.remoteEtag);
    remoteLastModified = std::move(other.remoteLastModified);
    finalUrl = std::move(other.finalUrl);
    taskRateLimitBytesPerSec = std::move(other.taskRateLimitBytesPerSec);
    schedulerMaxChunks = other.schedulerMaxChunks;
    schedulerPriority = other.schedulerPriority;
    peakSpeedBytesPerSec = other.peakSpeedBytesPerSec;
    failedChunks = other.failedChunks;
    persistedRetryCount = other.persistedRetryCount;
    requestedThreads = other.requestedThreads;
    return *this;
}

std::string toString(TaskStatus status) {
    switch (status) {
        case TaskStatus::Created: return "Created";
        case TaskStatus::Pending: return "Pending";
        case TaskStatus::Queued: return "Queued";
        case TaskStatus::Downloading: return "Downloading";
        case TaskStatus::Paused: return "Paused";
        case TaskStatus::Retrying: return "Retrying";
        case TaskStatus::Failed: return "Failed";
        case TaskStatus::Completed: return "Completed";
        case TaskStatus::Cancelled: return "Cancelled";
        case TaskStatus::Removed: return "Removed";
        case TaskStatus::Corrupted: return "Corrupted";
        case TaskStatus::PendingRecovery: return "PendingRecovery";
        case TaskStatus::MetadataMismatch: return "MetadataMismatch";
    }
    return "Unknown";
}

std::string toString(ChunkStatus status) {
    switch (status) {
        case ChunkStatus::Pending: return "Pending";
        case ChunkStatus::Downloading: return "Downloading";
        case ChunkStatus::Paused: return "Paused";
        case ChunkStatus::Completed: return "Completed";
        case ChunkStatus::Failed: return "Failed";
    }
    return "Unknown";
}

TaskStatus taskStatusFromString(const std::string& value) {
    if (value == "Created") return TaskStatus::Created;
    if (value == "Pending") return TaskStatus::Pending;
    if (value == "Queued") return TaskStatus::Queued;
    if (value == "Downloading") return TaskStatus::Downloading;
    if (value == "Paused") return TaskStatus::Paused;
    if (value == "Retrying") return TaskStatus::Retrying;
    if (value == "Failed") return TaskStatus::Failed;
    if (value == "Completed") return TaskStatus::Completed;
    if (value == "Cancelled") return TaskStatus::Cancelled;
    if (value == "Removed") return TaskStatus::Removed;
    if (value == "Corrupted") return TaskStatus::Corrupted;
    if (value == "PendingRecovery") return TaskStatus::PendingRecovery;
    if (value == "MetadataMismatch") return TaskStatus::MetadataMismatch;
    throw std::invalid_argument("unknown task status: " + value);
}

ChunkStatus chunkStatusFromString(const std::string& value) {
    if (value == "Pending") return ChunkStatus::Pending;
    if (value == "Downloading") return ChunkStatus::Downloading;
    if (value == "Paused") return ChunkStatus::Paused;
    if (value == "Completed") return ChunkStatus::Completed;
    if (value == "Failed") return ChunkStatus::Failed;
    throw std::invalid_argument("unknown chunk status: " + value);
}

TaskSnapshot snapshotTask(const DownloadTask& task) {
    TaskSnapshot snapshot;
    snapshot.id = task.id;
    snapshot.url = task.url;
    snapshot.fileName = task.fileName;
    snapshot.targetPath = task.targetPath;
    snapshot.status = task.status;
    snapshot.fileSize = task.fileSize;
    snapshot.downloaded = task.downloaded.load();
    snapshot.progress = task.fileSize == 0 ? 0.0 : (static_cast<double>(snapshot.downloaded) / task.fileSize) * 100.0;
    snapshot.progress = std::clamp(snapshot.progress, 0.0, 100.0);
    snapshot.lastError = task.lastError;
    snapshot.expectedSha256 = task.expectedSha256;
    snapshot.remoteEtag = task.remoteEtag;
    snapshot.remoteLastModified = task.remoteLastModified;
    snapshot.finalUrl = task.finalUrl;
    snapshot.taskRateLimitBytesPerSec = task.taskRateLimitBytesPerSec;
    snapshot.schedulerMaxChunks = task.schedulerMaxChunks;
    snapshot.schedulerPriority = task.schedulerPriority;
    snapshot.peakSpeedBytesPerSec = task.peakSpeedBytesPerSec;
    snapshot.failedChunks = task.failedChunks;
    snapshot.retryCount = task.persistedRetryCount;
    snapshot.totalChunks = static_cast<std::uint32_t>(task.chunks.size());
    for (const auto& chunk : task.chunks) {
        if (chunk.status == ChunkStatus::Completed) {
            ++snapshot.completedChunks;
        }
        if (chunk.status == ChunkStatus::Downloading) {
            ++snapshot.runningChunks;
        }
        if (chunk.status == ChunkStatus::Failed) {
            ++snapshot.failedChunks;
        }
        snapshot.retryCount += chunk.retryCount;
    }

    const auto start = task.startedAt.value_or(task.createdAt);
    double elapsedSeconds = std::chrono::duration<double>(task.updatedAt - start).count();
    if (snapshot.downloaded > 0) {
        elapsedSeconds = std::max(elapsedSeconds, 0.001);
        snapshot.averageSpeedBytesPerSec = static_cast<double>(snapshot.downloaded) / elapsedSeconds;
    }
    if (snapshot.averageSpeedBytesPerSec > 0.0 && snapshot.fileSize > snapshot.downloaded) {
        const auto remaining = snapshot.fileSize - snapshot.downloaded;
        snapshot.eta = std::chrono::seconds(static_cast<std::int64_t>(
            std::ceil(static_cast<double>(remaining) / snapshot.averageSpeedBytesPerSec)));
    }
    return snapshot;
}

std::string formatBytes(std::uint64_t bytes) {
    static constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    if (unit == 0) {
        out << bytes << units[unit];
    } else {
        out << std::fixed << std::setprecision(2) << value << units[unit];
    }
    return out.str();
}

std::string formatTimestamp(std::chrono::system_clock::time_point value) {
    const std::time_t time = std::chrono::system_clock::to_time_t(value);
    const std::tm tm = gmtimePortable(time);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::chrono::system_clock::time_point parseTimestamp(const std::string& value) {
    std::tm tm{};
    std::istringstream input(value);
    input >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (input.fail()) {
        return std::chrono::system_clock::now();
    }
    return std::chrono::system_clock::from_time_t(timegmPortable(&tm));
}

}  // namespace cget
