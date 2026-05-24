#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cget {

using TaskId = std::string;

enum class TaskStatus {
    Created,
    Pending,
    Queued,
    Downloading,
    Paused,
    Retrying,
    Failed,
    Completed,
    Cancelled,
    Removed,
    Corrupted,
    PendingRecovery,
    MetadataMismatch
};

enum class ChunkStatus {
    Pending,
    Downloading,
    Paused,
    Completed,
    Failed
};

struct RemoteFileInfo {
    std::uint64_t fileSize = 0;
    bool supportsRange = false;
    std::optional<std::string> etag;
    std::optional<std::string> lastModified;
    std::string finalUrl;
};

struct CreateTaskRequest {
    std::string url;
    std::optional<std::filesystem::path> outputPath;
    std::optional<std::string> fileName;
    std::optional<std::string> expectedSha256;
    std::optional<std::uint64_t> taskRateLimitBytesPerSec;
    bool taskRateLimitOverride = false;
    std::uint32_t requestedThreads = 0;
    bool forceOverwrite = false;
    bool queueOnly = false;
};

struct Chunk {
    std::uint64_t index = 0;
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    std::atomic<std::uint64_t> downloaded{0};
    ChunkStatus status = ChunkStatus::Pending;
    std::uint32_t retryCount = 0;
    std::filesystem::path tempPath;

    Chunk() = default;
    Chunk(std::uint64_t chunkIndex,
          std::uint64_t chunkStart,
          std::uint64_t chunkEnd,
          std::uint64_t chunkDownloaded,
          ChunkStatus chunkStatus,
          std::uint32_t retries,
          std::filesystem::path chunkTempPath);
    Chunk(const Chunk& other);
    Chunk& operator=(const Chunk& other);
    Chunk(Chunk&& other) noexcept;
    Chunk& operator=(Chunk&& other) noexcept;

    [[nodiscard]] std::uint64_t size() const;
};

struct DownloadTask {
    TaskId id;
    std::string url;
    std::string fileName;
    std::filesystem::path targetPath;
    std::filesystem::path tempDir;
    TaskStatus status = TaskStatus::Created;
    std::uint64_t fileSize = 0;
    std::atomic<std::uint64_t> downloaded{0};
    std::vector<Chunk> chunks;
    std::chrono::system_clock::time_point createdAt;
    std::chrono::system_clock::time_point updatedAt;
    std::optional<std::chrono::system_clock::time_point> startedAt;
    std::optional<std::string> lastError;
    std::optional<std::string> expectedSha256;
    std::optional<std::string> remoteEtag;
    std::optional<std::string> remoteLastModified;
    std::optional<std::string> finalUrl;
    std::optional<std::uint64_t> taskRateLimitBytesPerSec;
    std::uint32_t schedulerMaxChunks = 0;
    std::uint32_t schedulerPriority = 0;
    double peakSpeedBytesPerSec = 0.0;
    std::uint32_t failedChunks = 0;
    std::uint32_t persistedRetryCount = 0;
    std::uint32_t requestedThreads = 0;
    mutable std::mutex mutex;

    DownloadTask();
    DownloadTask(const DownloadTask& other);
    DownloadTask& operator=(const DownloadTask& other);
    DownloadTask(DownloadTask&& other) noexcept;
    DownloadTask& operator=(DownloadTask&& other) noexcept;
};

struct TaskSnapshot {
    TaskId id;
    std::string url;
    std::string fileName;
    std::filesystem::path targetPath;
    TaskStatus status = TaskStatus::Created;
    std::uint64_t fileSize = 0;
    std::uint64_t downloaded = 0;
    double progress = 0.0;
    std::optional<std::string> lastError;
    std::optional<std::string> expectedSha256;
    std::optional<std::string> remoteEtag;
    std::optional<std::string> remoteLastModified;
    std::optional<std::string> finalUrl;
    std::optional<std::uint64_t> taskRateLimitBytesPerSec;
    std::uint32_t completedChunks = 0;
    std::uint32_t totalChunks = 0;
    std::uint32_t runningChunks = 0;
    std::uint32_t failedChunks = 0;
    std::uint32_t retryCount = 0;
    std::uint32_t schedulerMaxChunks = 0;
    std::uint32_t schedulerPriority = 0;
    double currentSpeedBytesPerSec = 0.0;
    double averageSpeedBytesPerSec = 0.0;
    double peakSpeedBytesPerSec = 0.0;
    std::optional<std::chrono::seconds> eta;
};

[[nodiscard]] std::string toString(TaskStatus status);
[[nodiscard]] std::string toString(ChunkStatus status);
[[nodiscard]] TaskStatus taskStatusFromString(const std::string& value);
[[nodiscard]] ChunkStatus chunkStatusFromString(const std::string& value);
[[nodiscard]] TaskSnapshot snapshotTask(const DownloadTask& task);
[[nodiscard]] std::string formatBytes(std::uint64_t bytes);
[[nodiscard]] std::string formatTimestamp(std::chrono::system_clock::time_point value);
[[nodiscard]] std::chrono::system_clock::time_point parseTimestamp(const std::string& value);

}  // namespace cget
