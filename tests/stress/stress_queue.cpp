#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "cget/config/config_manager.h"
#include "cget/crypto/sha256.h"
#include "cget/engine/download_engine.h"
#include "cget/filesystem/filesystem_service.h"
#include "cget/network/protocol_handler.h"
#include "cget/persistence/persistence_store.h"

namespace {

class FakeProtocol final : public cget::ProtocolHandler {
public:
    explicit FakeProtocol(std::string data) : data_(std::move(data)) {}

    cget::RemoteFileInfo fetchMetadata(const std::string& url) override {
        cget::RemoteFileInfo info;
        info.fileSize = data_.size();
        info.supportsRange = true;
        info.finalUrl = url;
        return info;
    }

    void downloadRange(const std::string&,
                       std::uint64_t start,
                       std::uint64_t end,
                       const cget::WriteCallback& write,
                       const cget::ProgressCallback& progress) override {
        const auto offset = static_cast<std::size_t>(start);
        const auto size = static_cast<std::size_t>(end - start + 1);
        write(data_.data() + offset, size);
        progress(size);
    }

    void downloadSingle(const std::string&,
                        const cget::WriteCallback& write,
                        const cget::ProgressCallback& progress) override {
        write(data_.data(), data_.size());
        progress(data_.size());
    }

private:
    std::string data_;
};

std::string readBinary(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    const auto home = std::filesystem::temp_directory_path() / "cget_stress_queue";
    std::error_code ec;
    std::filesystem::remove_all(home, ec);
    cget::FileSystemService fs(home);
    cget::PersistenceStore store(fs);
    const std::string payload(128 * 1024, 'x');
    const auto expectedSha = cget::sha256String(payload);

    constexpr int taskCount = 40;
    constexpr int workerCount = 8;
    std::atomic<int> next{0};
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (int worker = 0; worker < workerCount; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const int index = next.fetch_add(1);
                if (index >= taskCount) {
                    return;
                }
                cget::DownloadTask task;
                task.id = "stress_" + std::to_string(index);
                task.url = "https://example.com/stress.bin";
                task.fileName = "stress_" + std::to_string(index) + ".bin";
                task.targetPath = fs.downloadsDir() / task.fileName;
                task.tempDir = fs.taskTempDir(task.id);
                task.status = cget::TaskStatus::Pending;
                task.requestedThreads = 4;
                task.expectedSha256 = expectedSha;
                fs.ensureTaskTempDir(task.id);
                store.saveTask(task);

                cget::Config config;
                config.maxThreads = 4;
                cget::DownloadEngine engine(std::make_unique<FakeProtocol>(payload), fs, store, config);
                engine.download(task);
                assert(task.status == cget::TaskStatus::Completed);
                assert(readBinary(task.targetPath) == payload);
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    const auto tasks = store.loadAllTasks();
    assert(tasks.size() == taskCount);
    std::cout << "stress queue completed " << tasks.size() << " tasks\n";
    return 0;
}
