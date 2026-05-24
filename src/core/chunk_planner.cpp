#include "cget/core/chunk_planner.h"

#include <algorithm>
#include <string>
#include <thread>

namespace cget {

std::vector<Chunk> ChunkPlanner::plan(std::uint64_t fileSize, std::uint32_t requestedThreads) {
    if (fileSize == 0) {
        return {};
    }

    std::uint32_t threads = requestedThreads;
    if (threads == 0) {
        const auto hardware = std::thread::hardware_concurrency();
        threads = hardware == 0 ? 4 : std::min<std::uint32_t>(hardware * 2, 32);
    }
    threads = std::max<std::uint32_t>(1, std::min<std::uint32_t>(threads, 32));
    threads = static_cast<std::uint32_t>(std::min<std::uint64_t>(threads, fileSize));

    std::vector<Chunk> chunks;
    chunks.reserve(threads);

    const std::uint64_t chunkSize = fileSize / threads;
    std::uint64_t start = 0;
    for (std::uint32_t index = 0; index < threads; ++index) {
        const std::uint64_t end = index == threads - 1 ? fileSize - 1 : start + chunkSize - 1;
        chunks.emplace_back(index,
                            start,
                            end,
                            0,
                            ChunkStatus::Pending,
                            0,
                            "chunk_" + std::to_string(index) + ".part");
        start = end + 1;
    }

    return chunks;
}

}  // namespace cget
