#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace cget {

enum class CommandType {
    Add,
    Pause,
    Resume,
    Remove,
    Run,
    List,
    Status,
    ConfigGet,
    ConfigSet,
    Protocols,
    Recover,
    Version,
    Help
};

struct Command {
    CommandType type = CommandType::Help;
    std::vector<std::string> args;
    std::unordered_map<std::string, std::string> options;
};

class CliParser {
public:
    [[nodiscard]] Command parse(int argc, char** argv) const;
    [[nodiscard]] std::string helpText() const;
    [[nodiscard]] std::string versionText() const;

private:
    [[nodiscard]] Command parseAdd(const std::vector<std::string>& tokens) const;
    [[nodiscard]] Command parseOneId(CommandType type, const std::vector<std::string>& tokens, const std::string& name) const;
    [[nodiscard]] Command parseResume(const std::vector<std::string>& tokens) const;
    [[nodiscard]] Command parseStatus(const std::vector<std::string>& tokens) const;
    [[nodiscard]] Command parseConfig(const std::vector<std::string>& tokens) const;
};

}  // namespace cget
