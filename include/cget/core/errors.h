#pragma once

#include <stdexcept>
#include <string>
#include <utility>

namespace cget {

enum class ErrorCode {
    NetworkError,
    TimeoutError,
    DnsError,
    HttpStatusError,
    DiskFullError,
    JsonCorruptedError,
    RangeNotSupportedError,
    MetadataMismatchError,
    PermissionDeniedError,
    PathTraversalError,
    FileAlreadyExistsError,
    TaskNotFoundError,
    InvalidStateTransitionError,
    InvalidCommandError,
    InvalidUrlError,
    FileSystemError
};

class CgetError : public std::runtime_error {
public:
    CgetError(ErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code) {}

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

}  // namespace cget
