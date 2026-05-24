#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "cget/cli/cli_parser.h"
#include "cget/config/config_manager.h"
#include "cget/core/download_manager.h"
#include "cget/core/errors.h"
#include "cget/filesystem/filesystem_service.h"
#include "cget/network/protocol_registry.h"

namespace {

void printTaskSummary(const cget::DownloadTask& task) {
    const auto snapshot = cget::snapshotTask(task);
    std::cout << "ID: " << snapshot.id << '\n'
              << "File: " << snapshot.fileName << '\n'
              << "Status: " << cget::toString(snapshot.status) << '\n'
              << "Downloaded: " << cget::formatBytes(snapshot.downloaded);
    if (snapshot.fileSize != 0) {
        std::cout << " / " << cget::formatBytes(snapshot.fileSize);
    }
    std::cout << '\n'
              << "Target: " << snapshot.targetPath.string() << '\n';
    if (snapshot.lastError) {
        std::cout << "Error: " << *snapshot.lastError << '\n';
    }
    if (snapshot.expectedSha256) {
        std::cout << "SHA256: " << *snapshot.expectedSha256 << '\n';
    }
}

void printSnapshot(const cget::TaskSnapshot& snapshot) {
    std::cout << "Task: " << snapshot.id << '\n'
              << "URL: " << snapshot.url << '\n'
              << "File: " << snapshot.fileName << '\n'
              << "Target: " << snapshot.targetPath.string() << '\n'
              << "Status: " << cget::toString(snapshot.status) << '\n'
              << "Progress: " << std::fixed << std::setprecision(1) << snapshot.progress << "%\n"
              << "Downloaded: " << cget::formatBytes(snapshot.downloaded);
    if (snapshot.fileSize != 0) {
        std::cout << " / " << cget::formatBytes(snapshot.fileSize);
    }
    std::cout << '\n'
              << "Average speed: " << cget::formatBytes(static_cast<std::uint64_t>(snapshot.averageSpeedBytesPerSec))
              << "/s\n";
    if (snapshot.eta) {
        std::cout << "ETA: " << snapshot.eta->count() << "s\n";
    }
    std::cout << "Chunks: " << snapshot.completedChunks << " / " << snapshot.totalChunks << " completed\n"
              << "Retries: " << snapshot.retryCount << '\n';
    if (snapshot.lastError) {
        std::cout << "Error: " << *snapshot.lastError << '\n';
    }
    if (snapshot.expectedSha256) {
        std::cout << "Expected SHA256: " << *snapshot.expectedSha256 << '\n';
    }
}

void printList(const std::vector<cget::TaskSnapshot>& tasks) {
    if (tasks.empty()) {
        std::cout << "No tasks.\n";
        return;
    }
    std::cout << std::left << std::setw(20) << "ID"
              << std::setw(24) << "File"
              << std::setw(12) << "Progress"
              << std::setw(16) << "Downloaded"
              << std::setw(14) << "Avg Speed"
              << "Status\n";
    for (const auto& task : tasks) {
        std::ostringstream progress;
        progress << std::fixed << std::setprecision(1) << task.progress << "%";
        std::cout << std::left << std::setw(20) << task.id.substr(0, 19)
                  << std::setw(24) << task.fileName.substr(0, 23)
                  << std::setw(12) << progress.str()
                  << std::setw(16) << cget::formatBytes(task.downloaded)
                  << std::setw(14)
                  << (cget::formatBytes(static_cast<std::uint64_t>(task.averageSpeedBytesPerSec)) + "/s")
                  << cget::toString(task.status) << '\n';
    }
}

void printProtocols() {
    std::cout << std::left << std::setw(10) << "Scheme"
              << std::setw(12) << "Available"
              << std::setw(12) << "Range"
              << "Note\n";
    for (const auto& protocol : cget::ProtocolRegistry::protocols()) {
        std::cout << std::left << std::setw(10) << protocol.scheme
                  << std::setw(12) << (protocol.available ? "yes" : "no")
                  << std::setw(12) << (protocol.rangeCapable ? "yes" : "no")
                  << protocol.note << '\n';
    }
}

std::uint32_t parseThreads(const cget::Command& command) {
    const auto it = command.options.find("threads");
    if (it == command.options.end()) {
        return 0;
    }
    return static_cast<std::uint32_t>(std::stoul(it->second));
}

}  // namespace

int main(int argc, char** argv) {
    cget::CliParser parser;
    try {
        const auto command = parser.parse(argc, argv);
        cget::DownloadManager manager;
        cget::ConfigManager config{cget::FileSystemService()};

        switch (command.type) {
            case cget::CommandType::Help:
                std::cout << parser.helpText();
                return 0;
            case cget::CommandType::Version:
                std::cout << parser.versionText() << '\n';
                return 0;
            case cget::CommandType::List:
                printList(manager.listTasks());
                return 0;
            case cget::CommandType::Status:
                printSnapshot(manager.getTaskSnapshot(command.args.at(0)));
                return 0;
            case cget::CommandType::Pause:
                manager.pauseTask(command.args.at(0));
                std::cout << "Task paused: " << command.args.at(0) << '\n';
                return 0;
            case cget::CommandType::Remove:
                manager.removeTask(command.args.at(0));
                std::cout << "Task removed: " << command.args.at(0) << '\n';
                return 0;
            case cget::CommandType::Run: {
                const auto results = manager.runQueuedTasks();
                if (results.empty()) {
                    std::cout << "No queued tasks.\n";
                } else {
                    printList(results);
                }
                return std::all_of(results.begin(), results.end(), [](const cget::TaskSnapshot& task) {
                    return task.status == cget::TaskStatus::Completed;
                }) ? 0 : 1;
            }
            case cget::CommandType::ConfigGet:
                if (command.args.empty()) {
                    for (const auto& [key, value] : config.entries()) {
                        std::cout << key << " = " << value << '\n';
                    }
                } else {
                    std::cout << command.args.at(0) << " = " << config.get(command.args.at(0)) << '\n';
                }
                return 0;
            case cget::CommandType::ConfigSet:
                config.set(command.args.at(0), command.args.at(1));
                std::cout << command.args.at(0) << " = " << config.get(command.args.at(0)) << '\n';
                return 0;
            case cget::CommandType::Protocols:
                printProtocols();
                return 0;
            case cget::CommandType::Recover: {
                const auto recovered = manager.recoverTasks();
                std::cout << "Recovered " << recovered.size() << " task(s).\n";
                return 0;
            }
            case cget::CommandType::Add: {
                cget::CreateTaskRequest request;
                request.url = command.args.at(0);
                request.requestedThreads = parseThreads(command);
                request.forceOverwrite = command.options.find("force") != command.options.end();
                request.queueOnly = command.options.find("queue") != command.options.end();
                if (const auto it = command.options.find("sha256"); it != command.options.end()) {
                    request.expectedSha256 = it->second;
                }
                if (const auto it = command.options.find("output"); it != command.options.end()) {
                    request.outputPath = it->second;
                }
                auto task = manager.createTask(request);
                std::cout << "Task created:\n";
                printTaskSummary(task);
                if (request.queueOnly) {
                    std::cout << "Queued. Run with: cget run\n";
                    return 0;
                }
                std::cout << "Starting foreground download...\n";
                manager.downloadTask(task);
                printTaskSummary(task);
                return task.status == cget::TaskStatus::Completed ? 0 : 1;
            }
            case cget::CommandType::Resume: {
                (void)manager.recoverTasks();
                auto task = manager.loadTask(command.args.at(0));
                std::cout << "Task resumed:\n";
                printTaskSummary(task);
                manager.downloadTask(task);
                printTaskSummary(task);
                return task.status == cget::TaskStatus::Completed ? 0 : 1;
            }
        }
    } catch (const cget::CgetError& error) {
        std::cerr << "cget: " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "cget: " << error.what() << '\n';
        return 1;
    }
    return 1;
}
