#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "cget/core/types.h"

namespace cget {

using ProgressCallback = std::function<void(std::uint64_t bytes)>;
using WriteCallback = std::function<void(const char* data, std::size_t size)>;

class ProtocolHandler {
public:
    virtual ~ProtocolHandler() = default;
    [[nodiscard]] virtual RemoteFileInfo fetchMetadata(const std::string& url) = 0;
    virtual void downloadRange(const std::string& url,
                               std::uint64_t start,
                               std::uint64_t end,
                               const WriteCallback& write,
                               const ProgressCallback& progress) = 0;
    virtual void downloadSingle(const std::string& url,
                                const WriteCallback& write,
                                const ProgressCallback& progress) = 0;
};

}  // namespace cget
