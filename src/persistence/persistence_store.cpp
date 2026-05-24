#include "cget/persistence/persistence_store.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "cget/core/errors.h"
#include "cget/persistence/json.h"

namespace cget {
namespace {

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw CgetError(ErrorCode::TaskNotFoundError, "failed to open task file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

bool isTaskJsonFile(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    return name.rfind("task_", 0) == 0 && path.extension() == ".json";
}

std::uint32_t asUint32(const json::Value& value) {
    return static_cast<std::uint32_t>(value.asUint64());
}

json::Value::Object chunkToJson(const Chunk& chunk) {
    json::Value::Object out;
    out["index"] = json::Value(chunk.index);
    out["start"] = json::Value(chunk.start);
    out["end"] = json::Value(chunk.end);
    out["downloaded"] = json::Value(chunk.downloaded.load());
    out["status"] = json::Value(toString(chunk.status));
    out["retry_count"] = json::Value(static_cast<std::uint64_t>(chunk.retryCount));
    out["temp_path"] = json::Value(chunk.tempPath.generic_string());
    return out;
}

}  // namespace

PersistenceStore::PersistenceStore(FileSystemService fileSystem) : fileSystem_(std::move(fileSystem)) {}

void PersistenceStore::saveTask(const DownloadTask& task) const {
    fileSystem_.ensureDirectories();
    const auto path = taskFile(task.id);
    const auto tmp = path.string() + ".tmp";
    const auto bak = path.string() + ".bak";

    {
        std::ofstream output(tmp, std::ios::trunc);
        if (!output) {
            throw CgetError(ErrorCode::PermissionDeniedError, "failed to write task file: " + tmp);
        }
        output << taskToJson(task);
        output.flush();
        if (!output) {
            throw CgetError(ErrorCode::FileSystemError, "failed to flush task file: " + tmp);
        }
    }

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::filesystem::remove(bak, ec);
        ec.clear();
        std::filesystem::copy_file(path, bak, std::filesystem::copy_options::none, ec);
        if (ec) {
            throw CgetError(ErrorCode::FileSystemError, "failed to create task backup: " + ec.message());
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        throw CgetError(ErrorCode::FileSystemError, "failed to commit task file: " + ec.message());
    }
}

DownloadTask PersistenceStore::loadTask(const TaskId& id) const {
    const auto path = taskFile(id);
    if (auto task = tryLoadTaskFile(path)) {
        return *task;
    }
    if (auto task = tryLoadTaskFile(path.string() + ".bak")) {
        return *task;
    }
    throw CgetError(ErrorCode::JsonCorruptedError, "task JSON is missing or corrupted: " + id);
}

std::vector<DownloadTask> PersistenceStore::loadAllTasks() const {
    fileSystem_.ensureDirectories();
    std::vector<DownloadTask> tasks;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(fileSystem_.tasksDir(), ec)) {
        if (ec) {
            throw CgetError(ErrorCode::FileSystemError, "failed to scan tasks directory: " + ec.message());
        }
        if (!entry.is_regular_file() || !isTaskJsonFile(entry.path())) {
            continue;
        }
        const auto id = entry.path().stem().string().substr(std::string("task_").size());
        try {
            tasks.push_back(loadTask(id));
        } catch (const std::exception&) {
            DownloadTask corrupted;
            corrupted.id = id;
            corrupted.fileName = entry.path().filename().string();
            corrupted.targetPath = entry.path();
            corrupted.status = TaskStatus::Corrupted;
            corrupted.lastError = "task JSON is corrupted";
            tasks.push_back(std::move(corrupted));
        }
    }
    std::sort(tasks.begin(), tasks.end(), [](const DownloadTask& lhs, const DownloadTask& rhs) {
        return lhs.createdAt < rhs.createdAt;
    });
    return tasks;
}

std::vector<DownloadTask> PersistenceStore::recoverTasks() const {
    auto tasks = loadAllTasks();
    for (auto& task : tasks) {
        if (task.status == TaskStatus::Corrupted) {
            continue;
        }
        repairFromDisk(task);
        saveTask(task);
    }
    return tasks;
}

std::filesystem::path PersistenceStore::taskFile(const TaskId& id) const {
    return fileSystem_.tasksDir() / ("task_" + id + ".json");
}

void PersistenceStore::removeTask(const TaskId& id) const {
    std::error_code ec;
    std::filesystem::remove(taskFile(id), ec);
    ec.clear();
    std::filesystem::remove(taskFile(id).string() + ".bak", ec);
    ec.clear();
    std::filesystem::remove(taskFile(id).string() + ".tmp", ec);
}

DownloadTask PersistenceStore::taskFromJson(const std::string& text) const {
    try {
        const auto root = json::parse(text);
        DownloadTask task;
        task.id = root.at("id").asString();
        task.url = root.at("url").asString();
        task.fileName = root.at("file_name").asString();
        task.targetPath = root.at("target_path").asString();
        task.tempDir = root.at("temp_dir").asString();
        task.status = taskStatusFromString(root.at("status").asString());
        task.fileSize = root.at("file_size").asUint64();
        task.downloaded.store(root.at("downloaded").asUint64());
        task.createdAt = parseTimestamp(root.at("created_at").asString());
        task.updatedAt = parseTimestamp(root.at("updated_at").asString());
        if (root.contains("started_at") && !root.at("started_at").isNull()) {
            task.startedAt = parseTimestamp(root.at("started_at").asString());
        }
        if (root.contains("last_error") && !root.at("last_error").isNull()) {
            task.lastError = root.at("last_error").asString();
        }
        if (root.contains("expected_sha256") && !root.at("expected_sha256").isNull()) {
            task.expectedSha256 = root.at("expected_sha256").asString();
        }
        if (root.contains("requested_threads")) {
            task.requestedThreads = asUint32(root.at("requested_threads"));
        }
        for (const auto& item : root.at("chunks").asArray()) {
            Chunk chunk;
            chunk.index = item.at("index").asUint64();
            chunk.start = item.at("start").asUint64();
            chunk.end = item.at("end").asUint64();
            chunk.downloaded.store(item.at("downloaded").asUint64());
            chunk.status = chunkStatusFromString(item.at("status").asString());
            chunk.retryCount = asUint32(item.at("retry_count"));
            chunk.tempPath = item.at("temp_path").asString();
            if (chunk.end < chunk.start || chunk.downloaded.load() > chunk.size()) {
                throw CgetError(ErrorCode::JsonCorruptedError, "invalid chunk range in task JSON");
            }
            task.chunks.push_back(std::move(chunk));
        }
        return task;
    } catch (const CgetError&) {
        throw;
    } catch (const std::exception& error) {
        throw CgetError(ErrorCode::JsonCorruptedError, error.what());
    }
}

std::string PersistenceStore::taskToJson(const DownloadTask& task) const {
    json::Value::Array chunks;
    chunks.reserve(task.chunks.size());
    for (const auto& chunk : task.chunks) {
        chunks.emplace_back(chunkToJson(chunk));
    }

    json::Value::Object root;
    root["id"] = json::Value(task.id);
    root["url"] = json::Value(task.url);
    root["file_name"] = json::Value(task.fileName);
    root["target_path"] = json::Value(task.targetPath.generic_string());
    root["temp_dir"] = json::Value(task.tempDir.generic_string());
    root["status"] = json::Value(toString(task.status));
    root["file_size"] = json::Value(task.fileSize);
    root["downloaded"] = json::Value(task.downloaded.load());
    root["created_at"] = json::Value(formatTimestamp(task.createdAt));
    root["updated_at"] = json::Value(formatTimestamp(task.updatedAt));
    root["started_at"] = task.startedAt ? json::Value(formatTimestamp(*task.startedAt)) : json::Value(nullptr);
    root["requested_threads"] = json::Value(static_cast<std::uint64_t>(task.requestedThreads));
    root["last_error"] = task.lastError ? json::Value(*task.lastError) : json::Value(nullptr);
    root["expected_sha256"] = task.expectedSha256 ? json::Value(*task.expectedSha256) : json::Value(nullptr);
    root["chunks"] = json::Value(std::move(chunks));
    return json::stringify(json::Value(std::move(root)));
}

std::optional<DownloadTask> PersistenceStore::tryLoadTaskFile(const std::filesystem::path& path) const {
    try {
        if (!std::filesystem::exists(path)) {
            return std::nullopt;
        }
        return taskFromJson(readTextFile(path));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void PersistenceStore::repairFromDisk(DownloadTask& task) const {
    if (task.chunks.empty()) {
        if (task.status == TaskStatus::Downloading || task.status == TaskStatus::Retrying) {
            task.status = TaskStatus::Paused;
        }
        return;
    }

    std::uint64_t total = 0;
    for (auto& chunk : task.chunks) {
        const auto partPath = fileSystem_.chunkPath(task, chunk);
        std::uint64_t actual = fileSystem_.fileSize(partPath);
        if (actual > chunk.size()) {
            actual = chunk.size();
            fileSystem_.truncateFile(partPath, actual);
        }
        chunk.downloaded.store(actual);
        total += actual;
        if (actual == chunk.size()) {
            chunk.status = ChunkStatus::Completed;
        } else if (actual == 0) {
            chunk.status = ChunkStatus::Pending;
        } else {
            chunk.status = ChunkStatus::Paused;
        }
    }
    task.downloaded.store(total);
    if (task.status == TaskStatus::Downloading || task.status == TaskStatus::Queued ||
        task.status == TaskStatus::Retrying || task.status == TaskStatus::Pending) {
        task.status = total == task.fileSize && task.fileSize != 0 ? TaskStatus::Completed : TaskStatus::Paused;
    }
    task.updatedAt = std::chrono::system_clock::now();
}

}  // namespace cget
