#pragma once

#include <memory>
#include <string>
#include <vector>

#include "cget/network/protocol_handler.h"

namespace cget {

struct ProtocolInfo {
    std::string scheme;
    bool available = false;
    bool rangeCapable = false;
    std::string note;
};

class ProtocolRegistry {
public:
    [[nodiscard]] static std::string schemeOf(const std::string& url);
    [[nodiscard]] static std::vector<ProtocolInfo> protocols();
    [[nodiscard]] static bool isSupportedScheme(const std::string& scheme);
    [[nodiscard]] static std::unique_ptr<ProtocolHandler> create(const std::string& url,
                                                                 const std::string& proxyUrl);
    [[nodiscard]] static std::string supportedSchemesText();
};

}  // namespace cget
