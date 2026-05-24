#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace cget {

[[nodiscard]] std::optional<std::uint64_t> parseBandwidthLimit(const std::string& value);
[[nodiscard]] std::string formatRateLimit(std::optional<std::uint64_t> bytesPerSec);

}  // namespace cget
