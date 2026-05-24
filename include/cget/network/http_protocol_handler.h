#pragma once

#include "cget/network/protocol_handler.h"

namespace cget {

class HttpProtocolHandler final : public ProtocolHandler {
public:
    HttpProtocolHandler();
    explicit HttpProtocolHandler(std::string proxyUrl);
    ~HttpProtocolHandler() override;

    [[nodiscard]] RemoteFileInfo fetchMetadata(const std::string& url) override;
    void downloadRange(const std::string& url,
                       std::uint64_t start,
                       std::uint64_t end,
                       const WriteCallback& write,
                       const ProgressCallback& progress) override;
    void downloadSingle(const std::string& url,
                        const WriteCallback& write,
                        const ProgressCallback& progress) override;

private:
    std::string proxyUrl_;
};

}  // namespace cget
