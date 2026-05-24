#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include "cget/core/types.h"
#include "cget/filesystem/filesystem_service.h"

namespace cget {

class PersistenceStore {
public:
    explicit PersistenceStore(FileSystemService fileSystem);

    void saveTask(const DownloadTask& task) const;
    [[nodiscard]] DownloadTask loadTask(const TaskId& id) const;
    [[nodiscard]] std::vector<DownloadTask> loadAllTasks() const;
    [[nodiscard]] std::vector<DownloadTask> recoverTasks() const;
    [[nodiscard]] std::filesystem::path taskFile(const TaskId& id) const;
    void removeTask(const TaskId& id) const;

private:
    [[nodiscard]] DownloadTask taskFromJson(const std::string& text) const;
    [[nodiscard]] std::string taskToJson(const DownloadTask& task) const;
    [[nodiscard]] std::optional<DownloadTask> tryLoadTaskFile(const std::filesystem::path& path) const;
    void repairFromDisk(DownloadTask& task) const;

    FileSystemService fileSystem_;
};

}  // namespace cget
