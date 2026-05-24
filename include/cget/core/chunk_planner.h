#pragma once

#include <cstdint>
#include <vector>

#include "cget/core/types.h"

namespace cget {

class ChunkPlanner {
public:
    [[nodiscard]] static std::vector<Chunk> plan(std::uint64_t fileSize, std::uint32_t requestedThreads);
};

}  // namespace cget
