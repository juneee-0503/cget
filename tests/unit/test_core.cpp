#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "cget/cli/cli_parser.h"
#include "cget/config/config_manager.h"
#include "cget/core/chunk_planner.h"
#include "cget/core/download_manager.h"
#include "cget/core/errors.h"
#include "cget/core/logger.h"
#include "cget/core/task_state.h"
#include "cget/crypto/sha256.h"
#include "cget/engine/download_engine.h"
#include "cget/filesystem/filesystem_service.h"
#include "cget/network/protocol_handler.h"
#include "cget/network/protocol_registry.h"
#include "cget/persistence/persistence_store.h"

namespace {

std::filesystem::path makeTempHome(const std::string& name) {
    auto path = std::filesystem::temp_directory_path() / ("cget_test_" + name);
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path);
    return path;
}

char* mutableArg(const char* value) {
    return const_cast<char*>(value);
}

void setEnv(const char* key, const char* value) {
#if defined(_WIN32)
    _putenv_s(key, value);
#else
    setenv(key, value, 1);
#endif
}

void unsetEnv(const char* key) {
#if defined(_WIN32)
    _putenv_s(key, "");
#else
    unsetenv(key);
#endif
}

void testCliParser() {
    cget::CliParser parser;
    {
        char* argv[] = {mutableArg("cget"), mutableArg("add"), mutableArg("https://example.com/file.bin"),
                        mutableArg("-o"), mutableArg("out.bin"), mutableArg("--threads"), mutableArg("4"),
                        mutableArg("--queue")};
        auto command = parser.parse(8, argv);
        assert(command.type == cget::CommandType::Add);
        assert(command.args.at(0) == "https://example.com/file.bin");
        assert(command.options.at("output") == "out.bin");
        assert(command.options.at("threads") == "4");
        assert(command.options.at("queue") == "true");
    }
    {
        char* argv[] = {mutableArg("cget"), mutableArg("add")};
        bool threw = false;
        try {
            (void)parser.parse(2, argv);
        } catch (const cget::CgetError&) {
            threw = true;
        }
        assert(threw);
    }
    {
        char* argv[] = {mutableArg("cget"), mutableArg("add"), mutableArg("https://example.com/file.bin"),
                        mutableArg("--threads"), mutableArg("nope")};
        bool threw = false;
        try {
            (void)parser.parse(5, argv);
        } catch (const cget::CgetError&) {
            threw = true;
        }
        assert(threw);
    }
    {
        char* argv[] = {mutableArg("cget"), mutableArg("pause"), mutableArg("abc")};
        auto command = parser.parse(3, argv);
        assert(command.type == cget::CommandType::Pause);
        assert(command.args.at(0) == "abc");
    }
    {
        char* argv[] = {mutableArg("cget"), mutableArg("run")};
        auto command = parser.parse(2, argv);
        assert(command.type == cget::CommandType::Run);
    }
    {
        char* argv[] = {mutableArg("cget"), mutableArg("config"), mutableArg("set"), mutableArg("max_threads"),
                        mutableArg("4")};
        auto command = parser.parse(5, argv);
        assert(command.type == cget::CommandType::ConfigSet);
        assert(command.args.at(0) == "max_threads");
        assert(command.args.at(1) == "4");
    }
    {
        char* argv[] = {mutableArg("cget"), mutableArg("protocols")};
        auto command = parser.parse(2, argv);
        assert(command.type == cget::CommandType::Protocols);
    }
    {
        char* argv[] = {mutableArg("cget"), mutableArg("add"), mutableArg("https://example.com/file.bin"),
                        mutableArg("--sha256"),
                        mutableArg("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD")};
        auto command = parser.parse(5, argv);
        assert(command.type == cget::CommandType::Add);
        assert(command.options.at("sha256").size() == 64);
    }
}

void testChunkPlanner() {
    auto chunks = cget::ChunkPlanner::plan(10, 3);
    assert(chunks.size() == 3);
    assert(chunks[0].start == 0);
    assert(chunks[0].end == 2);
    assert(chunks[1].start == 3);
    assert(chunks[1].end == 5);
    assert(chunks[2].start == 6);
    assert(chunks[2].end == 9);

    auto tiny = cget::ChunkPlanner::plan(2, 8);
    assert(tiny.size() == 2);
    assert(tiny[0].start == 0);
    assert(tiny[0].end == 0);
    assert(tiny[1].start == 1);
    assert(tiny[1].end == 1);
}

void testStateMachine() {
    assert(cget::isValidTransition(cget::TaskStatus::Created, cget::TaskStatus::Pending));
    assert(cget::isValidTransition(cget::TaskStatus::Downloading, cget::TaskStatus::Completed));
    assert(cget::isValidTransition(cget::TaskStatus::Downloading, cget::TaskStatus::PendingRecovery));
    assert(cget::isValidTransition(cget::TaskStatus::PendingRecovery, cget::TaskStatus::Paused));
    assert(!cget::isValidTransition(cget::TaskStatus::Completed, cget::TaskStatus::Downloading));
}

void testFileSystemPaths() {
    cget::FileSystemService fs(makeTempHome("paths"));
    fs.ensureDirectories();
    const auto target = fs.resolveOutputPath(std::nullopt, "file.bin", false);
    assert(target.parent_path() == fs.downloadsDir());

    {
        std::ofstream existing(target);
        existing << "x";
    }
    const auto unique = fs.resolveOutputPath(target, "file.bin", false);
    assert(unique != target);

    bool threw = false;
    try {
        (void)fs.resolveOutputPath(std::filesystem::path("../escape.bin"), "escape.bin", false);
    } catch (const cget::CgetError&) {
        threw = true;
    }
    assert(threw);
}

void testConfigManager() {
    cget::FileSystemService fs(makeTempHome("config"));
    cget::ConfigManager manager(fs);
    auto config = manager.load();
    assert(config.download.maxThreads == 8);
    manager.set("max_threads", "4");
    manager.set("download.max_active_tasks", "3");
    assert(manager.get("max_threads") == "4");
    assert(manager.get("download.max_active_tasks") == "3");
    manager.set("download.max_download_rate_bytes_per_sec", "0");
    manager.set("network.proxy", "http://127.0.0.1:9999");
    manager.set("logging.level", "debug");
    manager.set("logging.console", "false");
    assert(manager.get("max_download_rate_bytes_per_sec") == "0");
    assert(manager.get("proxy") == "http://127.0.0.1:9999");
    assert(manager.get("logging.level") == "debug");
    manager.set("proxy", "none");
    assert(manager.get("network.proxy") == "none");
    assert(manager.entries().size() == 11);

    bool threw = false;
    try {
        manager.set("max_threads", "1000");
    } catch (const cget::CgetError&) {
        threw = true;
    }
    assert(threw);

    {
        cget::FileSystemService legacyFs(makeTempHome("config_legacy"));
        legacyFs.ensureDirectories();
        {
            std::ofstream legacy(legacyFs.configPath());
            legacy << "{\n"
                   << "  \"max_threads\": 5,\n"
                   << "  \"max_active_tasks\": 2,\n"
                   << "  \"max_retries\": 4,\n"
                   << "  \"retry_base_delay_ms\": 500,\n"
                   << "  \"max_download_rate_bytes_per_sec\": 1024,\n"
                   << "  \"proxy_url\": \"http://127.0.0.1:8080\"\n"
                   << "}\n";
        }
        cget::ConfigManager legacyManager(legacyFs);
        const auto loaded = legacyManager.load();
        assert(loaded.download.maxThreads == 5);
        assert(loaded.network.maxRetries == 4);
        assert(loaded.network.proxyUrl == "http://127.0.0.1:8080");
    }

    {
        setEnv("CGET_MAX_THREADS", "6");
        setEnv("CGET_LOG_LEVEL", "warn");
        setEnv("CGET_PROXY", "none");
        cget::FileSystemService envFs(makeTempHome("config_env"));
        cget::ConfigManager envManager(envFs);
        const auto loaded = envManager.load();
        assert(loaded.download.maxThreads == 6);
        assert(loaded.logging.level == "warn");
        assert(!loaded.network.proxyUrl.has_value());
        unsetEnv("CGET_MAX_THREADS");
        unsetEnv("CGET_LOG_LEVEL");
        unsetEnv("CGET_PROXY");
    }
}

void testErrorModel() {
    assert(cget::toString(cget::ErrorCode::NetworkError) == "NetworkError");
    assert(cget::isRetryable(cget::ErrorCode::TimeoutError));
    assert(!cget::isRetryable(cget::ErrorCode::PermissionDeniedError));
    assert(cget::suggestedExitCode(cget::ErrorCode::InvalidCommandError) == 2);
}

void testLogger() {
    cget::FileSystemService fs(makeTempHome("logger"));
    cget::LoggerOptions options;
    options.level = cget::LogLevel::Warn;
    options.console = false;
    options.maxFileBytes = 64;
    options.maxRotatedFiles = 2;
    cget::Logger logger(fs, options);
    logger.debug("hidden debug message");
    logger.error("visible error message");
    assert(std::filesystem::exists(fs.logsDir() / "cget.log"));

    for (int index = 0; index < 5; ++index) {
        logger.warn("rotation message " + std::to_string(index) + " with enough bytes to rotate");
    }
    assert(std::filesystem::exists(fs.logsDir() / "cget.log.1"));
}

void testSha256() {
    assert(cget::sha256String("abc") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(cget::isSha256Hex("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
    assert(!cget::isSha256Hex("not-a-sha"));
}

void testProtocolRegistry() {
    assert(cget::ProtocolRegistry::schemeOf("HTTPS://example.com/file") == "https");
    assert(cget::ProtocolRegistry::schemeOf("ftp://example.com/file") == "ftp");
    const auto protocols = cget::ProtocolRegistry::protocols();
    assert(!protocols.empty());
    assert(cget::ProtocolRegistry::isSupportedScheme("http"));
    assert(cget::ProtocolRegistry::isSupportedScheme("https"));
    auto handler = cget::ProtocolRegistry::create("http://example.com/file", "");
    assert(handler != nullptr);
    if (cget::ProtocolRegistry::isSupportedScheme("ftp")) {
        auto ftpHandler = cget::ProtocolRegistry::create("ftp://example.com/file", "");
        assert(ftpHandler != nullptr);
    }
}

cget::DownloadTask makeTask(const cget::FileSystemService& fs) {
    cget::DownloadTask task;
    task.id = "task1";
    task.url = "https://example.com/file.bin";
    task.fileName = "file.bin";
    task.targetPath = fs.downloadsDir() / "file.bin";
    task.tempDir = fs.taskTempDir(task.id);
    task.status = cget::TaskStatus::Downloading;
    task.fileSize = 10;
    task.downloaded.store(8);
    task.requestedThreads = 2;
    task.chunks.emplace_back(0, 0, 4, 5, cget::ChunkStatus::Completed, 0, "chunk_0.part");
    task.chunks.emplace_back(1, 5, 9, 3, cget::ChunkStatus::Downloading, 0, "chunk_1.part");
    return task;
}

class FakeProtocol final : public cget::ProtocolHandler {
public:
    FakeProtocol(std::string data,
                 bool supportsRange,
                 bool ignoreRange = false,
                 std::optional<std::string> etag = std::nullopt,
                 std::optional<std::string> lastModified = std::nullopt)
        : data_(std::move(data)),
          supportsRange_(supportsRange),
          ignoreRange_(ignoreRange),
          etag_(std::move(etag)),
          lastModified_(std::move(lastModified)) {}

    cget::RemoteFileInfo fetchMetadata(const std::string& url) override {
        cget::RemoteFileInfo info;
        info.fileSize = data_.size();
        info.supportsRange = supportsRange_;
        info.finalUrl = url;
        info.etag = etag_;
        info.lastModified = lastModified_;
        return info;
    }

    void downloadRange(const std::string&,
                       std::uint64_t start,
                       std::uint64_t end,
                       const cget::WriteCallback& write,
                       const cget::ProgressCallback& progress) override {
        if (ignoreRange_) {
            throw cget::CgetError(cget::ErrorCode::RangeNotSupportedError, "range ignored");
        }
        assert(start <= end);
        assert(end < data_.size());
        const auto count = static_cast<std::size_t>(end - start + 1);
        const auto offset = static_cast<std::size_t>(start);
        write(data_.data() + offset, count);
        progress(count);
    }

    void downloadSingle(const std::string&,
                        const cget::WriteCallback& write,
                        const cget::ProgressCallback& progress) override {
        write(data_.data(), data_.size());
        progress(data_.size());
    }

private:
    std::string data_;
    bool supportsRange_;
    bool ignoreRange_;
    std::optional<std::string> etag_;
    std::optional<std::string> lastModified_;
};

std::string readBinary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void testDownloadEngineRangeAndFallback() {
    const std::string data = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    {
        cget::FileSystemService fs(makeTempHome("engine_range"));
        cget::PersistenceStore store(fs);
        fs.ensureTaskTempDir("range");
        cget::DownloadTask task;
        task.id = "range";
        task.url = "https://example.com/payload.bin";
        task.fileName = "payload.bin";
        task.targetPath = fs.downloadsDir() / "payload.bin";
        task.tempDir = fs.taskTempDir(task.id);
        task.status = cget::TaskStatus::Pending;
        task.requestedThreads = 4;
        store.saveTask(task);

        cget::DownloadEngine engine(std::make_unique<FakeProtocol>(data, true), fs, store, cget::Config{});
        engine.download(task);
        assert(task.status == cget::TaskStatus::Completed);
        assert(task.chunks.size() == 4);
        assert(readBinary(task.targetPath) == data);
    }

    {
        cget::FileSystemService fs(makeTempHome("engine_single"));
        cget::PersistenceStore store(fs);
        fs.ensureTaskTempDir("single");
        cget::DownloadTask task;
        task.id = "single";
        task.url = "https://example.com/payload.bin";
        task.fileName = "payload.bin";
        task.targetPath = fs.downloadsDir() / "payload.bin";
        task.tempDir = fs.taskTempDir(task.id);
        task.status = cget::TaskStatus::Pending;
        task.requestedThreads = 4;
        store.saveTask(task);

        cget::DownloadEngine engine(std::make_unique<FakeProtocol>(data, false), fs, store, cget::Config{});
        engine.download(task);
        assert(task.status == cget::TaskStatus::Completed);
        assert(task.chunks.size() == 1);
        assert(readBinary(task.targetPath) == data);
    }

    {
        cget::FileSystemService fs(makeTempHome("engine_fallback"));
        cget::PersistenceStore store(fs);
        fs.ensureTaskTempDir("fallback");
        cget::DownloadTask task;
        task.id = "fallback";
        task.url = "https://example.com/payload.bin";
        task.fileName = "payload.bin";
        task.targetPath = fs.downloadsDir() / "payload.bin";
        task.tempDir = fs.taskTempDir(task.id);
        task.status = cget::TaskStatus::Pending;
        task.requestedThreads = 4;
        store.saveTask(task);

        cget::DownloadEngine engine(std::make_unique<FakeProtocol>(data, true, true), fs, store, cget::Config{});
        engine.download(task);
        assert(task.status == cget::TaskStatus::Completed);
        assert(task.chunks.size() == 1);
        assert(readBinary(task.targetPath) == data);
    }
}

void testDownloadEngineSha256Mismatch() {
    const std::string data = "checksum payload";
    cget::FileSystemService fs(makeTempHome("engine_sha"));
    cget::PersistenceStore store(fs);
    fs.ensureTaskTempDir("sha");
    cget::DownloadTask task;
    task.id = "sha";
    task.url = "https://example.com/payload.bin";
    task.fileName = "payload.bin";
    task.targetPath = fs.downloadsDir() / "payload.bin";
    task.tempDir = fs.taskTempDir(task.id);
    task.status = cget::TaskStatus::Pending;
    task.expectedSha256 = cget::sha256String("different payload");
    store.saveTask(task);

    cget::DownloadEngine engine(std::make_unique<FakeProtocol>(data, false), fs, store, cget::Config{});
    engine.download(task);
    assert(task.status == cget::TaskStatus::Failed);
    assert(task.lastError.has_value());
    assert(task.lastError->find("SHA256 mismatch") != std::string::npos);
}

void testDownloadEngineMetadataMismatch() {
    const std::string data = "metadata payload";
    cget::FileSystemService fs(makeTempHome("engine_metadata"));
    cget::PersistenceStore store(fs);
    fs.ensureTaskTempDir("metadata");
    cget::DownloadTask task;
    task.id = "metadata";
    task.url = "https://example.com/payload.bin";
    task.fileName = "payload.bin";
    task.targetPath = fs.downloadsDir() / "payload.bin";
    task.tempDir = fs.taskTempDir(task.id);
    task.status = cget::TaskStatus::Paused;
    task.fileSize = data.size();
    task.remoteEtag = "\"old\"";
    store.saveTask(task);

    cget::DownloadEngine engine(std::make_unique<FakeProtocol>(data, false, false, "\"new\""), fs, store,
                                cget::Config{});
    engine.download(task);
    assert(task.status == cget::TaskStatus::MetadataMismatch);
    assert(task.lastError.has_value());
}

void testPersistenceAndRecovery() {
    cget::FileSystemService fs(makeTempHome("persistence"));
    cget::PersistenceStore store(fs);
    fs.ensureTaskTempDir("task1");

    auto task = makeTask(fs);
    store.saveTask(task);
    auto loaded = store.loadTask("task1");
    assert(loaded.id == task.id);
    assert(loaded.chunks.size() == 2);
    assert(loaded.chunks[1].downloaded.load() == 3);
    task.remoteEtag = "\"etag\"";
    task.remoteLastModified = "Sun, 24 May 2026 00:00:00 GMT";
    task.finalUrl = "https://example.com/file.bin";
    store.saveTask(task);
    loaded = store.loadTask("task1");
    assert(loaded.remoteEtag == "\"etag\"");
    assert(loaded.remoteLastModified == "Sun, 24 May 2026 00:00:00 GMT");
    assert(loaded.finalUrl == "https://example.com/file.bin");

    task.lastError = "create backup";
    store.saveTask(task);
    {
        std::ofstream corrupt(store.taskFile("task1"), std::ios::trunc);
        corrupt << "{ broken";
    }
    loaded = store.loadTask("task1");
    assert(loaded.id == task.id);

    {
        std::ofstream part(fs.taskTempDir("task1") / "chunk_0.part", std::ios::binary);
        part << "12345";
    }
    {
        std::ofstream part(fs.taskTempDir("task1") / "chunk_1.part", std::ios::binary);
        part << "12";
    }
    store.saveTask(task);
    const auto recovered = store.recoverTasks();
    assert(recovered.size() == 1);
    assert(recovered[0].status == cget::TaskStatus::Paused);
    assert(recovered[0].downloaded.load() == 7);
    assert(recovered[0].chunks[0].status == cget::ChunkStatus::Completed);
    assert(recovered[0].chunks[1].status == cget::ChunkStatus::Paused);
    assert(recovered[0].chunks[1].downloaded.load() == 2);
}

void testManagerPauseAndRemove() {
    cget::FileSystemService fs(makeTempHome("manager"));
    cget::DownloadManager manager(fs);
    cget::CreateTaskRequest request;
    request.url = "https://example.com/file.bin";
    request.queueOnly = true;
    auto task = manager.createTask(request);
    assert(task.status == cget::TaskStatus::Queued);

    manager.pauseTask(task.id);
    auto paused = manager.loadTask(task.id);
    assert(paused.status == cget::TaskStatus::Paused);

    manager.removeTask(task.id);
    bool threw = false;
    try {
        (void)manager.loadTask(task.id);
    } catch (const cget::CgetError&) {
        threw = true;
    }
    assert(threw);
}

}  // namespace

int main() {
    testCliParser();
    testChunkPlanner();
    testStateMachine();
    testErrorModel();
    testLogger();
    testSha256();
    testProtocolRegistry();
    testFileSystemPaths();
    testConfigManager();
    testPersistenceAndRecovery();
    testManagerPauseAndRemove();
    testDownloadEngineRangeAndFallback();
    testDownloadEngineSha256Mismatch();
    testDownloadEngineMetadataMismatch();
    std::cout << "all unit tests passed\n";
    return 0;
}
