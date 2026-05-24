#include "cget/core/errors.h"

namespace cget {

std::string toString(ErrorCode code) {
    switch (code) {
        case ErrorCode::NetworkError: return "NetworkError";
        case ErrorCode::TimeoutError: return "TimeoutError";
        case ErrorCode::DnsError: return "DnsError";
        case ErrorCode::HttpStatusError: return "HttpStatusError";
        case ErrorCode::DiskFullError: return "DiskFullError";
        case ErrorCode::JsonCorruptedError: return "JsonCorruptedError";
        case ErrorCode::RangeNotSupportedError: return "RangeNotSupportedError";
        case ErrorCode::MetadataMismatchError: return "MetadataMismatchError";
        case ErrorCode::PermissionDeniedError: return "PermissionDeniedError";
        case ErrorCode::PathTraversalError: return "PathTraversalError";
        case ErrorCode::FileAlreadyExistsError: return "FileAlreadyExistsError";
        case ErrorCode::TaskNotFoundError: return "TaskNotFoundError";
        case ErrorCode::InvalidStateTransitionError: return "InvalidStateTransitionError";
        case ErrorCode::InvalidCommandError: return "InvalidCommandError";
        case ErrorCode::InvalidUrlError: return "InvalidUrlError";
        case ErrorCode::FileSystemError: return "FileSystemError";
        case ErrorCode::ConfigError: return "ConfigError";
        case ErrorCode::ChecksumMismatchError: return "ChecksumMismatchError";
        case ErrorCode::InternalError: return "InternalError";
    }
    return "UnknownError";
}

bool isRetryable(ErrorCode code) {
    switch (code) {
        case ErrorCode::NetworkError:
        case ErrorCode::TimeoutError:
        case ErrorCode::DnsError:
        case ErrorCode::HttpStatusError:
            return true;
        case ErrorCode::DiskFullError:
        case ErrorCode::JsonCorruptedError:
        case ErrorCode::RangeNotSupportedError:
        case ErrorCode::MetadataMismatchError:
        case ErrorCode::PermissionDeniedError:
        case ErrorCode::PathTraversalError:
        case ErrorCode::FileAlreadyExistsError:
        case ErrorCode::TaskNotFoundError:
        case ErrorCode::InvalidStateTransitionError:
        case ErrorCode::InvalidCommandError:
        case ErrorCode::InvalidUrlError:
        case ErrorCode::FileSystemError:
        case ErrorCode::ConfigError:
        case ErrorCode::ChecksumMismatchError:
        case ErrorCode::InternalError:
            return false;
    }
    return false;
}

int suggestedExitCode(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidCommandError:
        case ErrorCode::InvalidUrlError:
        case ErrorCode::PathTraversalError:
        case ErrorCode::ConfigError:
            return 2;
        case ErrorCode::TaskNotFoundError:
            return 3;
        case ErrorCode::NetworkError:
        case ErrorCode::TimeoutError:
        case ErrorCode::DnsError:
        case ErrorCode::HttpStatusError:
            return 4;
        case ErrorCode::PermissionDeniedError:
        case ErrorCode::DiskFullError:
        case ErrorCode::FileSystemError:
        case ErrorCode::FileAlreadyExistsError:
            return 5;
        case ErrorCode::JsonCorruptedError:
        case ErrorCode::MetadataMismatchError:
        case ErrorCode::ChecksumMismatchError:
            return 6;
        case ErrorCode::RangeNotSupportedError:
        case ErrorCode::InvalidStateTransitionError:
        case ErrorCode::InternalError:
            return 1;
    }
    return 1;
}

}  // namespace cget
