#include "cget/ratelimit/bandwidth.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "cget/core/errors.h"
#include "cget/core/types.h"

namespace cget {
namespace {

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

}  // namespace

std::optional<std::uint64_t> parseBandwidthLimit(const std::string& value) {
    const auto normalized = lower(trim(value));
    if (normalized == "unlimited" || normalized == "none" || normalized == "off" || normalized == "0") {
        return std::nullopt;
    }

    std::size_t pos = 0;
    while (pos < normalized.size() && (std::isdigit(static_cast<unsigned char>(normalized[pos])) || normalized[pos] == '.')) {
        ++pos;
    }
    if (pos == 0) {
        throw CgetError(ErrorCode::InvalidCommandError, "invalid bandwidth limit: " + value);
    }

    double number = 0.0;
    try {
        number = std::stod(normalized.substr(0, pos));
    } catch (const std::exception&) {
        throw CgetError(ErrorCode::InvalidCommandError, "invalid bandwidth limit: " + value);
    }
    if (number <= 0.0) {
        return std::nullopt;
    }

    auto unit = trim(normalized.substr(pos));
    if (unit.size() > 2 && unit.substr(unit.size() - 2) == "/s") {
        unit = trim(unit.substr(0, unit.size() - 2));
    }
    double multiplier = 1.0;
    if (unit.empty() || unit == "b") {
        multiplier = 1.0;
    } else if (unit == "kb") {
        multiplier = 1000.0;
    } else if (unit == "mb") {
        multiplier = 1000.0 * 1000.0;
    } else if (unit == "gb") {
        multiplier = 1000.0 * 1000.0 * 1000.0;
    } else if (unit == "kib") {
        multiplier = 1024.0;
    } else if (unit == "mib") {
        multiplier = 1024.0 * 1024.0;
    } else if (unit == "gib") {
        multiplier = 1024.0 * 1024.0 * 1024.0;
    } else {
        throw CgetError(ErrorCode::InvalidCommandError, "unknown bandwidth unit: " + unit);
    }

    const auto bytes = number * multiplier;
    if (!std::isfinite(bytes) || bytes > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
        throw CgetError(ErrorCode::InvalidCommandError, "bandwidth limit is out of range");
    }
    return static_cast<std::uint64_t>(bytes);
}

std::string formatRateLimit(std::optional<std::uint64_t> bytesPerSec) {
    if (!bytesPerSec || *bytesPerSec == 0) {
        return "unlimited";
    }
    return formatBytes(*bytesPerSec) + "/s";
}

}  // namespace cget
