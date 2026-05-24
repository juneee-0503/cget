#include <algorithm>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>

#include "cget/cli/cli_parser.h"
#include "cget/config/config_manager.h"
#include "cget/core/download_manager.h"
#include "cget/core/errors.h"
#include "cget/filesystem/filesystem_service.h"
#include "cget/metrics/metrics_service.h"
#include "cget/network/protocol_registry.h"
#include "cget/ratelimit/bandwidth.h"

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
    if (snapshot.taskRateLimitBytesPerSec) {
        std::cout << "Limit: " << cget::formatRateLimit(snapshot.taskRateLimitBytesPerSec) << '\n';
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
              << "Current speed: " << cget::formatBytes(static_cast<std::uint64_t>(snapshot.currentSpeedBytesPerSec))
              << "/s\n"
              << "Average speed: " << cget::formatBytes(static_cast<std::uint64_t>(snapshot.averageSpeedBytesPerSec))
              << "/s\n";
    if (snapshot.eta) {
        std::cout << "ETA: " << snapshot.eta->count() << "s\n";
    }
    std::cout << "Chunks: " << snapshot.completedChunks << " / " << snapshot.totalChunks << " completed, "
              << snapshot.runningChunks << " running, " << snapshot.failedChunks << " failed\n"
              << "Retries: " << snapshot.retryCount << '\n';
    std::cout << "Scheduler quota: "
              << (snapshot.schedulerMaxChunks == 0 ? std::string("default")
                                                   : std::to_string(snapshot.schedulerMaxChunks))
              << "\n";
    std::cout << "Rate limit: " << cget::formatRateLimit(snapshot.taskRateLimitBytesPerSec) << '\n';
    if (snapshot.lastError) {
        std::cout << "Error: " << *snapshot.lastError << '\n';
    }
    if (snapshot.expectedSha256) {
        std::cout << "Expected SHA256: " << *snapshot.expectedSha256 << '\n';
    }
    if (snapshot.remoteEtag) {
        std::cout << "Remote ETag: " << *snapshot.remoteEtag << '\n';
    }
    if (snapshot.remoteLastModified) {
        std::cout << "Remote Last-Modified: " << *snapshot.remoteLastModified << '\n';
    }
    if (snapshot.finalUrl) {
        std::cout << "Final URL: " << *snapshot.finalUrl << '\n';
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
              << std::setw(16) << "Current Speed"
              << std::setw(10) << "ETA"
              << std::setw(12) << "Chunks"
              << "Status\n";
    for (const auto& task : tasks) {
        std::ostringstream progress;
        progress << std::fixed << std::setprecision(1) << task.progress << "%";
        const auto eta = task.eta ? std::to_string(task.eta->count()) + "s" : "-";
        const auto chunks = std::to_string(task.completedChunks) + "/" + std::to_string(task.totalChunks);
        std::cout << std::left << std::setw(20) << task.id.substr(0, 19)
                  << std::setw(24) << task.fileName.substr(0, 23)
                  << std::setw(12) << progress.str()
                  << std::setw(16)
                  << (cget::formatBytes(static_cast<std::uint64_t>(task.currentSpeedBytesPerSec)) + "/s")
                  << std::setw(10) << eta
                  << std::setw(12) << chunks
                  << cget::toString(task.status) << '\n';
    }
}

void printStats(const cget::SystemMetrics& metrics) {
    std::cout << "Runtime: " << (metrics.stale ? "stale" : "active") << '\n'
              << "Updated: " << cget::formatTimestamp(metrics.updatedAt) << '\n'
              << "Tasks: " << metrics.totalTasks << " total, " << metrics.activeTasks << " active, "
              << metrics.queuedTasks << " queued, " << metrics.pausedTasks << " paused, "
              << metrics.failedTasks << " failed, " << metrics.completedTasks << " completed\n"
              << "Workers: " << metrics.busyWorkers << " / " << metrics.totalWorkers << " busy ("
              << std::fixed << std::setprecision(1) << metrics.workerUtilization << "%)\n"
              << "Global current speed: "
              << cget::formatBytes(static_cast<std::uint64_t>(metrics.globalCurrentSpeedBytesPerSec)) << "/s\n"
              << "Global average speed: "
              << cget::formatBytes(static_cast<std::uint64_t>(metrics.globalAverageSpeedBytesPerSec)) << "/s\n"
              << "Scheduler queue: " << metrics.schedulerQueueSize << '\n'
              << "Scheduler policy: " << metrics.schedulerPolicy << '\n';
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

std::optional<std::uint64_t> parseLimit(const cget::Command& command) {
    const auto it = command.options.find("limit");
    if (it == command.options.end()) {
        return std::nullopt;
    }
    return cget::parseBandwidthLimit(it->second);
}

}  // namespace

int main(int argc, char** argv) {
    cget::CliParser parser;
    try {
        const auto command = parser.parse(argc, argv);

        switch (command.type) {
            case cget::CommandType::Help:
                std::cout << parser.helpText();
                return 0;
            case cget::CommandType::Version:
                std::cout << parser.versionText() << '\n';
                return 0;
            case cget::CommandType::List:
            {
                cget::DownloadManager manager;
                printList(manager.listTasks());
                return 0;
            }
            case cget::CommandType::Stats:
            {
                cget::FileSystemService fs;
                if (const auto metrics = cget::MetricsService::loadRuntimeSnapshot(fs)) {
                    printStats(*metrics);
                } else {
                    std::cout << "No runtime metrics snapshot. Start a download or run queued tasks first.\n";
                }
                return 0;
            }
            case cget::CommandType::Status:
            {
                cget::DownloadManager manager;
                printSnapshot(manager.getTaskSnapshot(command.args.at(0)));
                return 0;
            }
            case cget::CommandType::Pause:
            {
                cget::DownloadManager manager;
                manager.pauseTask(command.args.at(0));
                std::cout << "Task paused: " << command.args.at(0) << '\n';
                return 0;
            }
            case cget::CommandType::Remove:
            {
                cget::DownloadManager manager;
                manager.removeTask(command.args.at(0));
                std::cout << "Task removed: " << command.args.at(0) << '\n';
                return 0;
            }
            case cget::CommandType::Run: {
                cget::DownloadManager manager;
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
            {
                cget::ConfigManager config{cget::FileSystemService()};
                if (command.args.empty()) {
                    for (const auto& [key, value] : config.entries()) {
                        std::cout << key << " = " << value << '\n';
                    }
                } else {
                    const auto value = config.get(command.args.at(0));
                    std::cout << command.args.at(0) << " = " << value << '\n';
                }
                return 0;
            }
            case cget::CommandType::ConfigSet:
            {
                cget::ConfigManager config{cget::FileSystemService()};
                config.set(command.args.at(0), command.args.at(1));
                std::cout << command.args.at(0) << " = " << config.get(command.args.at(0)) << '\n';
                return 0;
            }
            case cget::CommandType::Protocols:
                printProtocols();
                return 0;
            case cget::CommandType::Recover: {
                cget::DownloadManager manager;
                const auto recovered = manager.recoverTasks();
                std::cout << "Recovered " << recovered.size() << " task(s).\n";
                return 0;
            }
            case cget::CommandType::Add: {
                cget::DownloadManager manager;
                cget::CreateTaskRequest request;
                request.url = command.args.at(0);
                request.requestedThreads = parseThreads(command);
                request.forceOverwrite = command.options.find("force") != command.options.end();
                request.queueOnly = command.options.find("queue") != command.options.end();
                if (command.options.find("limit") != command.options.end()) {
                    request.taskRateLimitOverride = true;
                    request.taskRateLimitBytesPerSec = parseLimit(command);
                }
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
                cget::DownloadManager manager;
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
        std::cerr << "cget: " << cget::toString(error.code()) << ": " << error.what() << '\n';
        return cget::suggestedExitCode(error.code());
    } catch (const std::exception& error) {
        std::cerr << "cget: " << error.what() << '\n';
        return 1;
    }
    return 1;
}
