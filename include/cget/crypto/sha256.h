#pragma once

#include <filesystem>
#include <string>

namespace cget {

[[nodiscard]] std::string sha256String(const std::string& input);
[[nodiscard]] std::string sha256File(const std::filesystem::path& path);
[[nodiscard]] bool isSha256Hex(const std::string& value);
[[nodiscard]] std::string lowercaseHex(std::string value);

}  // namespace cget
