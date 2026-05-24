#include "cget/network/protocol_registry.h"

#include <algorithm>
#include <curl/curl.h>
#include <mutex>
#include <set>
#include <sstream>

#include "cget/core/errors.h"
#include "cget/network/http_protocol_handler.h"

namespace cget {
namespace {

std::once_flag curlInitFlag;

void ensureCurlInitialized() {
    std::call_once(curlInitFlag, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::set<std::string> curlProtocols() {
    ensureCurlInitialized();
    std::set<std::string> out;
    const auto* version = curl_version_info(CURLVERSION_NOW);
    if (version == nullptr || version->protocols == nullptr) {
        return out;
    }
    for (const char* const* protocol = version->protocols; *protocol != nullptr; ++protocol) {
        out.insert(lower(*protocol));
    }
    return out;
}

bool curlHasHttp3() {
    ensureCurlInitialized();
    const auto* version = curl_version_info(CURLVERSION_NOW);
    if (version == nullptr) {
        return false;
    }
#if defined(CURL_VERSION_HTTP3)
    return (version->features & CURL_VERSION_HTTP3) != 0;
#else
    return false;
#endif
}

bool isKnownDownloadScheme(const std::string& scheme) {
    return scheme == "http" || scheme == "https" || scheme == "ftp" || scheme == "ftps" ||
           scheme == "sftp" || scheme == "scp";
}

bool isRangeCapable(const std::string& scheme, bool available) {
    if (!available) {
        return false;
    }
    return scheme == "http" || scheme == "https" || scheme == "ftp" || scheme == "ftps" ||
           scheme == "sftp" || scheme == "scp";
}

}  // namespace

std::string ProtocolRegistry::schemeOf(const std::string& url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        return {};
    }
    return lower(url.substr(0, schemeEnd));
}

std::vector<ProtocolInfo> ProtocolRegistry::protocols() {
    const auto available = curlProtocols();
    std::vector<ProtocolInfo> infos;
    for (const std::string scheme : {"http", "https", "ftp", "ftps", "sftp", "scp"}) {
        ProtocolInfo info;
        info.scheme = scheme;
        info.available = available.find(scheme) != available.end();
        info.rangeCapable = isRangeCapable(scheme, info.available);
        if (!info.available) {
            info.note = "not available in this libcurl build";
        } else if (scheme == "https" && curlHasHttp3()) {
            info.note = "HTTP/3 available through libcurl when negotiated/configured by curl";
        } else {
            info.note = "handled by libcurl protocol adapter";
        }
        infos.push_back(std::move(info));
    }
    return infos;
}

bool ProtocolRegistry::isSupportedScheme(const std::string& scheme) {
    const auto normalized = lower(scheme);
    if (!isKnownDownloadScheme(normalized)) {
        return false;
    }
    const auto available = curlProtocols();
    return available.find(normalized) != available.end();
}

std::unique_ptr<ProtocolHandler> ProtocolRegistry::create(const std::string& url, const std::string& proxyUrl) {
    const auto scheme = schemeOf(url);
    if (scheme.empty()) {
        throw CgetError(ErrorCode::InvalidUrlError, "URL must include a supported scheme");
    }
    if (!isKnownDownloadScheme(scheme)) {
        throw CgetError(ErrorCode::InvalidUrlError, "unsupported URL scheme: " + scheme);
    }
    if (!isSupportedScheme(scheme)) {
        throw CgetError(ErrorCode::InvalidUrlError,
                       "scheme '" + scheme + "' is not available in this libcurl build; supported here: " +
                           supportedSchemesText());
    }
    return std::make_unique<HttpProtocolHandler>(proxyUrl);
}

std::string ProtocolRegistry::supportedSchemesText() {
    std::ostringstream out;
    bool first = true;
    for (const auto& info : protocols()) {
        if (!info.available) {
            continue;
        }
        if (!first) {
            out << ", ";
        }
        first = false;
        out << info.scheme;
    }
    if (first) {
        return "none";
    }
    return out.str();
}

}  // namespace cget
