#pragma once

#include <memory>
#include <mutex>

#include "cget/config/config_manager.h"
#include "cget/filesystem/filesystem_service.h"
#include "cget/core/logger.h"
#include "cget/network/protocol_handler.h"
#include "cget/persistence/persistence_store.h"

namespace cget {

class DownloadEngine {
public:
    DownloadEngine(std::unique_ptr<ProtocolHandler> protocol,
                   FileSystemService fileSystem,
                   PersistenceStore persistence,
                   Config config);

    void download(DownloadTask& task);

private:
    struct RateLimiter;

    void downloadWithRange(DownloadTask& task, const RemoteFileInfo& info);
    void downloadSingleStream(DownloadTask& task, const RemoteFileInfo& info);
    void downloadChunkWithRetry(DownloadTask& task, Chunk& chunk, RateLimiter& limiter);
    void persistTask(DownloadTask& task);
    void setFailure(DownloadTask& task, TaskStatus status, const std::string& message);
    [[nodiscard]] bool verifyChecksum(DownloadTask& task);
    [[nodiscard]] std::uint32_t normalizedThreadCount(const DownloadTask& task) const;

    std::unique_ptr<ProtocolHandler> protocol_;
    FileSystemService fileSystem_;
    PersistenceStore persistence_;
    Config config_;
    Logger logger_;
    std::mutex persistMutex_;
};

}  // namespace cget
