#include "cget/filesystem/filesystem_service.h"

#include <cstdlib>
#include <sstream>

#include "cget/core/errors.h"

namespace cget {
namespace {

std::filesystem::path defaultHome() {
    if (const char* cgetHome = std::getenv("CGET_HOME")) {
        if (*cgetHome != '\0') {
            return std::filesystem::path(cgetHome);
        }
    }
#if defined(_WIN32)
    if (const char* userProfile = std::getenv("USERPROFILE")) {
        return std::filesystem::path(userProfile) / ".cget";
    }
#endif
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".cget";
    }
    return std::filesystem::current_path() / ".cget";
}

std::string sanitizeFileName(std::string name) {
    if (name.empty() || name == "." || name == "..") {
        return "download";
    }
    for (char& c : name) {
        if (c == '/' || c == '\\' || static_cast<unsigned char>(c) < 32) {
            c = '_';
        }
    }
    if (name.empty() || name == "." || name == "..") {
        return "download";
    }
    return name;
}

}  // namespace

FileSystemService::FileSystemService() : appHome_(defaultHome()) {}

FileSystemService::FileSystemService(std::filesystem::path appHome) : appHome_(std::move(appHome)) {}

const std::filesystem::path& FileSystemService::appHome() const {
    return appHome_;
}

std::filesystem::path FileSystemService::tasksDir() const {
    return appHome_ / "tasks";
}

std::filesystem::path FileSystemService::tempRootDir() const {
    return appHome_ / "temp";
}

std::filesystem::path FileSystemService::downloadsDir() const {
    return appHome_ / "downloads";
}

std::filesystem::path FileSystemService::logsDir() const {
    return appHome_ / "logs";
}

std::filesystem::path FileSystemService::configPath() const {
    return appHome_ / "config.json";
}

std::filesystem::path FileSystemService::taskTempDir(const TaskId& id) const {
    return tempRootDir() / id;
}

std::filesystem::path FileSystemService::chunkPath(const DownloadTask& task, const Chunk& chunk) const {
    if (isPathTraversal(chunk.tempPath) || chunk.tempPath.is_absolute()) {
        throw CgetError(ErrorCode::PathTraversalError, "unsafe chunk temp path: " + chunk.tempPath.string());
    }
    return task.tempDir / chunk.tempPath;
}

void FileSystemService::ensureDirectories() const {
    std::error_code ec;
    std::filesystem::create_directories(tasksDir(), ec);
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to create tasks directory: " + ec.message());
    }
    std::filesystem::create_directories(tempRootDir(), ec);
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to create temp directory: " + ec.message());
    }
    std::filesystem::create_directories(downloadsDir(), ec);
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to create downloads directory: " + ec.message());
    }
    std::filesystem::create_directories(logsDir(), ec);
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to create logs directory: " + ec.message());
    }
}

void FileSystemService::ensureTaskTempDir(const TaskId& id) const {
    ensureDirectories();
    std::error_code ec;
    std::filesystem::create_directories(taskTempDir(id), ec);
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to create task temp directory: " + ec.message());
    }
}

std::filesystem::path FileSystemService::resolveOutputPath(const std::optional<std::filesystem::path>& outputPath,
                                                           const std::string& fileName,
                                                           bool forceOverwrite) const {
    ensureDirectories();
    std::filesystem::path target = outputPath.value_or(downloadsDir() / sanitizeFileName(fileName));
    if (isPathTraversal(target)) {
        throw CgetError(ErrorCode::PathTraversalError, "output path contains '..': " + target.string());
    }

    if (target.has_filename() && std::filesystem::exists(target) && std::filesystem::is_directory(target)) {
        target /= sanitizeFileName(fileName);
    } else if (!target.has_filename()) {
        target /= sanitizeFileName(fileName);
    }

    if (target.is_relative()) {
        target = std::filesystem::absolute(target);
    }

    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to create output parent directory: " + ec.message());
    }

    if (!forceOverwrite && std::filesystem::exists(target)) {
        return uniquePath(target);
    }
    return target;
}

std::string FileSystemService::fileNameFromUrl(const std::string& url) const {
    std::string clean = url;
    const auto fragment = clean.find('#');
    if (fragment != std::string::npos) {
        clean.erase(fragment);
    }
    const auto query = clean.find('?');
    if (query != std::string::npos) {
        clean.erase(query);
    }
    const auto slash = clean.find_last_of('/');
    const std::string name = slash == std::string::npos ? clean : clean.substr(slash + 1);
    return sanitizeFileName(name.empty() ? "download" : name);
}

bool FileSystemService::isPathTraversal(const std::filesystem::path& path) const {
    for (const auto& part : path) {
        if (part == "..") {
            return true;
        }
    }
    return false;
}

std::uint64_t FileSystemService::fileSize(const std::filesystem::path& path) const {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return 0;
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return 0;
    }
    return size;
}

void FileSystemService::truncateFile(const std::filesystem::path& path, std::uint64_t size) const {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return;
    }
    std::filesystem::resize_file(path, size, ec);
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to truncate file: " + ec.message());
    }
}

void FileSystemService::mergeChunks(const std::vector<Chunk>& chunks,
                                    const std::filesystem::path& tempDir,
                                    const std::filesystem::path& targetPath,
                                    std::uint64_t expectedSize) const {
    std::error_code ec;
    std::filesystem::create_directories(targetPath.parent_path(), ec);
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to create output parent directory: " + ec.message());
    }

    const auto tempTarget = targetPath.string() + ".cget.tmp";
    {
        std::ofstream out(tempTarget, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw CgetError(ErrorCode::PermissionDeniedError, "failed to open output temp file: " + tempTarget);
        }
        std::vector<char> buffer(1024 * 1024);
        for (const auto& chunk : chunks) {
            if (isPathTraversal(chunk.tempPath) || chunk.tempPath.is_absolute()) {
                throw CgetError(ErrorCode::PathTraversalError, "unsafe chunk temp path: " + chunk.tempPath.string());
            }
            const auto partPath = tempDir / chunk.tempPath;
            std::ifstream in(partPath, std::ios::binary);
            if (!in) {
                throw CgetError(ErrorCode::FileSystemError, "missing chunk file: " + partPath.string());
            }
            while (in) {
                in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const auto read = in.gcount();
                if (read > 0) {
                    out.write(buffer.data(), read);
                }
            }
        }
    }

    const auto actualSize = fileSize(tempTarget);
    if (actualSize != expectedSize) {
        throw CgetError(ErrorCode::FileSystemError,
                       "merged file size mismatch: expected " + std::to_string(expectedSize) +
                           ", got " + std::to_string(actualSize));
    }

    std::filesystem::remove(targetPath, ec);
    ec.clear();
    std::filesystem::rename(tempTarget, targetPath, ec);
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to move merged file into place: " + ec.message());
    }
}

void FileSystemService::removeTaskTempDir(const TaskId& id) const {
    const auto dir = taskTempDir(id);
    if (isPathTraversal(id)) {
        throw CgetError(ErrorCode::PathTraversalError, "unsafe task id: " + id);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to remove task temp directory: " + ec.message());
    }
}

std::filesystem::path FileSystemService::uniquePath(const std::filesystem::path& path) const {
    const auto parent = path.parent_path();
    const auto stem = path.stem().string();
    const auto extension = path.extension().string();
    for (int index = 1; index < 10000; ++index) {
        std::ostringstream name;
        name << stem << " (" << index << ")" << extension;
        auto candidate = parent / name.str();
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    throw CgetError(ErrorCode::FileAlreadyExistsError, "could not find non-conflicting output path");
}

}  // namespace cget
