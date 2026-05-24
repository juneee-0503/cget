#include "cget/network/http_protocol_handler.h"

#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <exception>
#include <mutex>
#include <sstream>
#include <utility>

#include "cget/core/errors.h"
#include "cget/network/protocol_registry.h"

namespace cget {
namespace {

std::once_flag curlInitFlag;

void ensureCurlInitialized() {
    std::call_once(curlInitFlag, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

struct HeaderContext {
    bool acceptRanges = false;
    std::optional<std::string> etag;
    std::optional<std::string> lastModified;
    long statusCode = 0;
};

struct WriteContext {
    WriteCallback write;
    ProgressCallback progress;
    bool rejectRangeFallback = false;
    long statusCode = 0;
    std::exception_ptr exception;
};

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string curlError(CURLcode code, const char* buffer) {
    if (buffer != nullptr && *buffer != '\0') {
        return buffer;
    }
    return curl_easy_strerror(code);
}

void configureCommon(CURL* curl, const std::string& url, const std::string& proxyUrl, char* errorBuffer) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "cget/1.0");
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    if (!proxyUrl.empty()) {
        curl_easy_setopt(curl, CURLOPT_PROXY, proxyUrl.c_str());
    }
}

std::size_t headerCallback(char* buffer, std::size_t size, std::size_t nitems, void* userdata) {
    const std::size_t total = size * nitems;
    auto* context = static_cast<HeaderContext*>(userdata);
    std::string line(buffer, total);
    line = trim(line);
    if (line.rfind("HTTP/", 0) == 0) {
        std::istringstream input(line);
        std::string httpVersion;
        input >> httpVersion >> context->statusCode;
        return total;
    }
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
        return total;
    }
    const auto key = lower(trim(line.substr(0, colon)));
    const auto value = trim(line.substr(colon + 1));
    if (key == "accept-ranges" && lower(value) == "bytes") {
        context->acceptRanges = true;
    } else if (key == "etag") {
        context->etag = value;
    } else if (key == "last-modified") {
        context->lastModified = value;
    }
    return total;
}

std::size_t writeCallback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    const std::size_t total = size * nmemb;
    auto* context = static_cast<WriteContext*>(userdata);
    if (context->rejectRangeFallback && context->statusCode == 200) {
        return 0;
    }
    try {
        context->write(ptr, total);
        if (context->progress) {
            context->progress(total);
        }
    } catch (...) {
        context->exception = std::current_exception();
        return 0;
    }
    return total;
}

std::size_t writeHeaderForDownload(char* buffer, std::size_t size, std::size_t nitems, void* userdata) {
    const std::size_t total = size * nitems;
    auto* context = static_cast<WriteContext*>(userdata);
    std::string line(buffer, total);
    line = trim(line);
    if (line.rfind("HTTP/", 0) == 0) {
        std::istringstream input(line);
        std::string httpVersion;
        input >> httpVersion >> context->statusCode;
    }
    return total;
}

void throwForHttpStatus(long statusCode) {
    if (statusCode == 416) {
        throw CgetError(ErrorCode::MetadataMismatchError, "server returned HTTP 416 Range Not Satisfiable");
    }
    if (statusCode == 403 || statusCode == 404) {
        throw CgetError(ErrorCode::HttpStatusError, "server returned HTTP " + std::to_string(statusCode));
    }
    if (statusCode >= 400) {
        throw CgetError(ErrorCode::HttpStatusError, "server returned HTTP " + std::to_string(statusCode));
    }
}

bool isHttpUrl(const std::string& url) {
    const auto scheme = ProtocolRegistry::schemeOf(url);
    return scheme == "http" || scheme == "https";
}

bool rangeCanUseSuccessWithoutHttp206(const std::string& url) {
    const auto scheme = ProtocolRegistry::schemeOf(url);
    return scheme == "ftp" || scheme == "ftps" || scheme == "sftp" || scheme == "scp";
}

}  // namespace

HttpProtocolHandler::HttpProtocolHandler() {
    ensureCurlInitialized();
}

HttpProtocolHandler::HttpProtocolHandler(std::string proxyUrl) : proxyUrl_(std::move(proxyUrl)) {
    ensureCurlInitialized();
}

HttpProtocolHandler::~HttpProtocolHandler() = default;

RemoteFileInfo HttpProtocolHandler::fetchMetadata(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw CgetError(ErrorCode::NetworkError, "failed to initialize curl");
    }

    char errorBuffer[CURL_ERROR_SIZE]{};
    HeaderContext headers;
    configureCommon(curl, url, proxyUrl_, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headers);

    const CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        const auto message = curlError(result, errorBuffer);
        curl_easy_cleanup(curl);
        throw CgetError(result == CURLE_OPERATION_TIMEDOUT ? ErrorCode::TimeoutError : ErrorCode::NetworkError,
                       message);
    }

    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    throwForHttpStatus(responseCode);

    curl_off_t contentLength = -1;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &contentLength);

    char* finalUrl = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &finalUrl);
    const std::string effectiveUrl = finalUrl == nullptr ? url : std::string(finalUrl);

    curl_easy_cleanup(curl);

    RemoteFileInfo info;
    info.fileSize = contentLength > 0 ? static_cast<std::uint64_t>(contentLength) : 0;
    info.supportsRange = info.fileSize > 0 && (headers.acceptRanges || !isHttpUrl(url));
    info.etag = headers.etag;
    info.lastModified = headers.lastModified;
    info.finalUrl = effectiveUrl;
    return info;
}

void HttpProtocolHandler::downloadRange(const std::string& url,
                                        std::uint64_t start,
                                        std::uint64_t end,
                                        const WriteCallback& write,
                                        const ProgressCallback& progress) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw CgetError(ErrorCode::NetworkError, "failed to initialize curl");
    }

    char errorBuffer[CURL_ERROR_SIZE]{};
    WriteContext context{write, progress, true, 0, nullptr};
    configureCommon(curl, url, proxyUrl_, errorBuffer);
    const std::string range = std::to_string(start) + "-" + std::to_string(end);
    curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeaderForDownload);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &context);

    const CURLcode result = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);

    if (context.exception) {
        std::rethrow_exception(context.exception);
    }
    if (isHttpUrl(url) && responseCode == 200) {
        throw CgetError(ErrorCode::RangeNotSupportedError, "server ignored Range request");
    }
    if (result != CURLE_OK) {
        throw CgetError(result == CURLE_OPERATION_TIMEDOUT ? ErrorCode::TimeoutError : ErrorCode::NetworkError,
                       curlError(result, errorBuffer));
    }
    if (rangeCanUseSuccessWithoutHttp206(url)) {
        return;
    }
    if (responseCode != 206) {
        throwForHttpStatus(responseCode);
        throw CgetError(ErrorCode::HttpStatusError, "expected HTTP 206, got HTTP " + std::to_string(responseCode));
    }
}

void HttpProtocolHandler::downloadSingle(const std::string& url,
                                         const WriteCallback& write,
                                         const ProgressCallback& progress) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        throw CgetError(ErrorCode::NetworkError, "failed to initialize curl");
    }

    char errorBuffer[CURL_ERROR_SIZE]{};
    WriteContext context{write, progress, false, 0, nullptr};
    configureCommon(curl, url, proxyUrl_, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeaderForDownload);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &context);

    const CURLcode result = curl_easy_perform(curl);
    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);

    if (context.exception) {
        std::rethrow_exception(context.exception);
    }
    if (result != CURLE_OK) {
        throw CgetError(result == CURLE_OPERATION_TIMEDOUT ? ErrorCode::TimeoutError : ErrorCode::NetworkError,
                       curlError(result, errorBuffer));
    }
    throwForHttpStatus(responseCode);
}

}  // namespace cget
