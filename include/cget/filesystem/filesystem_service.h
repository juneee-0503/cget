#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "cget/core/types.h"

namespace cget {

class FileSystemService {
public:
    FileSystemService();
    explicit FileSystemService(std::filesystem::path appHome);

    [[nodiscard]] const std::filesystem::path& appHome() const;
    [[nodiscard]] std::filesystem::path tasksDir() const;
    [[nodiscard]] std::filesystem::path tempRootDir() const;
    [[nodiscard]] std::filesystem::path downloadsDir() const;
    [[nodiscard]] std::filesystem::path logsDir() const;
    [[nodiscard]] std::filesystem::path configPath() const;
    [[nodiscard]] std::filesystem::path taskTempDir(const TaskId& id) const;
    [[nodiscard]] std::filesystem::path chunkPath(const DownloadTask& task, const Chunk& chunk) const;

    void ensureDirectories() const;
    void ensureTaskTempDir(const TaskId& id) const;
    [[nodiscard]] std::filesystem::path resolveOutputPath(const std::optional<std::filesystem::path>& outputPath,
                                                          const std::string& fileName,
                                                          bool forceOverwrite) const;
    [[nodiscard]] std::string fileNameFromUrl(const std::string& url) const;
    [[nodiscard]] bool isPathTraversal(const std::filesystem::path& path) const;
    [[nodiscard]] std::uint64_t fileSize(const std::filesystem::path& path) const;
    void truncateFile(const std::filesystem::path& path, std::uint64_t size) const;
    void mergeChunks(const std::vector<Chunk>& chunks,
                     const std::filesystem::path& tempDir,
                     const std::filesystem::path& targetPath,
                     std::uint64_t expectedSize) const;
    void removeTaskTempDir(const TaskId& id) const;

private:
    [[nodiscard]] std::filesystem::path uniquePath(const std::filesystem::path& path) const;
    std::filesystem::path appHome_;
};

}  // namespace cget
