#pragma once

#include <chrono>
#include <memory>
#include <mutex>

#include "cget/config/config_manager.h"
#include "cget/filesystem/filesystem_service.h"
#include "cget/core/logger.h"
#include "cget/metrics/metrics_service.h"
#include "cget/network/protocol_handler.h"
#include "cget/persistence/persistence_store.h"
#include "cget/ratelimit/rate_limiter.h"

namespace cget {

class DownloadEngine {
public:
    DownloadEngine(std::unique_ptr<ProtocolHandler> protocol,
                   FileSystemService fileSystem,
                   PersistenceStore persistence,
                   Config config);
    DownloadEngine(std::unique_ptr<ProtocolHandler> protocol,
                   FileSystemService fileSystem,
                   PersistenceStore persistence,
                   Config config,
                   std::shared_ptr<RateLimiter> rateLimiter);

    void download(DownloadTask& task);
    void prepareTask(DownloadTask& task);
    void downloadPreparedChunk(DownloadTask& task, Chunk& chunk);
    void finalizePreparedTask(DownloadTask& task);

private:
    void downloadWithRange(DownloadTask& task, const RemoteFileInfo& info);
    void downloadSingleStream(DownloadTask& task, const RemoteFileInfo& info);
    void downloadChunkWithRetry(DownloadTask& task, Chunk& chunk, RateLimiter& limiter);
    void applyTaskRateLimit(const DownloadTask& task);
    void persistTask(DownloadTask& task);
    void refreshMetrics(const DownloadTask& task);
    void setFailure(DownloadTask& task, TaskStatus status, const std::string& message);
    [[nodiscard]] bool verifyChecksum(DownloadTask& task);
    [[nodiscard]] std::uint32_t normalizedThreadCount(const DownloadTask& task) const;

    std::unique_ptr<ProtocolHandler> protocol_;
    FileSystemService fileSystem_;
    PersistenceStore persistence_;
    Config config_;
    Logger logger_;
    MetricsService metrics_;
    std::shared_ptr<RateLimiter> rateLimiter_;
    std::chrono::steady_clock::time_point lastMetricsFlush_;
    std::mutex persistMutex_;
};

}  // namespace cget
