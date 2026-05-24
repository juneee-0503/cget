#pragma once

#include <vector>

#include "cget/config/config_manager.h"
#include "cget/core/types.h"
#include "cget/filesystem/filesystem_service.h"
#include "cget/core/logger.h"
#include "cget/persistence/persistence_store.h"

namespace cget {

class DownloadManager {
public:
    DownloadManager();
    explicit DownloadManager(FileSystemService fileSystem);

    [[nodiscard]] DownloadTask createTask(const CreateTaskRequest& request);
    void downloadTask(DownloadTask& task);
    void pauseTask(const TaskId& id);
    void removeTask(const TaskId& id);
    [[nodiscard]] std::vector<TaskSnapshot> runQueuedTasks();
    [[nodiscard]] DownloadTask loadTask(const TaskId& id) const;
    [[nodiscard]] std::vector<TaskSnapshot> listTasks() const;
    [[nodiscard]] TaskSnapshot getTaskSnapshot(const TaskId& id) const;
    [[nodiscard]] std::vector<DownloadTask> recoverTasks() const;

private:
    [[nodiscard]] TaskId generateTaskId() const;
    static void validateUrl(const std::string& url);

    FileSystemService fileSystem_;
    PersistenceStore persistence_;
    ConfigManager config_;
    Logger logger_;
};

}  // namespace cget
