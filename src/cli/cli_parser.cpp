#include "cget/cli/cli_parser.h"

#include <charconv>
#include <sstream>

#include "cget/core/errors.h"

namespace cget {
namespace {

std::vector<std::string> makeTokens(int argc, char** argv) {
    std::vector<std::string> tokens;
    tokens.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
        tokens.emplace_back(argv[i]);
    }
    return tokens;
}

bool isUnsignedInteger(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    unsigned parsed = 0;
    const auto* begin = value.data();
    const auto* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed);
    return result.ec == std::errc{} && result.ptr == end && parsed > 0;
}

}  // namespace

Command CliParser::parse(int argc, char** argv) const {
    const auto tokens = makeTokens(argc, argv);
    if (tokens.empty() || tokens[0] == "help" || tokens[0] == "-h" || tokens[0] == "--help") {
        return Command{CommandType::Help, {}, {}};
    }
    if (tokens[0] == "version" || tokens[0] == "--version" || tokens[0] == "-V") {
        return Command{CommandType::Version, {}, {}};
    }
    if (tokens[0] == "add") {
        return parseAdd(tokens);
    }
    if (tokens[0] == "pause") {
        return parseOneId(CommandType::Pause, tokens, "pause");
    }
    if (tokens[0] == "resume") {
        return parseResume(tokens);
    }
    if (tokens[0] == "remove") {
        return parseOneId(CommandType::Remove, tokens, "remove");
    }
    if (tokens[0] == "run") {
        if (tokens.size() != 1) {
            throw CgetError(ErrorCode::InvalidCommandError, "run does not accept positional arguments");
        }
        return Command{CommandType::Run, {}, {}};
    }
    if (tokens[0] == "list") {
        if (tokens.size() != 1) {
            throw CgetError(ErrorCode::InvalidCommandError, "list does not accept positional arguments");
        }
        return Command{CommandType::List, {}, {}};
    }
    if (tokens[0] == "status") {
        return parseStatus(tokens);
    }
    if (tokens[0] == "recover") {
        if (tokens.size() != 1) {
            throw CgetError(ErrorCode::InvalidCommandError, "recover does not accept positional arguments");
        }
        return Command{CommandType::Recover, {}, {}};
    }
    if (tokens[0] == "config") {
        return parseConfig(tokens);
    }
    if (tokens[0] == "protocols") {
        if (tokens.size() != 1) {
            throw CgetError(ErrorCode::InvalidCommandError, "protocols does not accept positional arguments");
        }
        return Command{CommandType::Protocols, {}, {}};
    }

    throw CgetError(ErrorCode::InvalidCommandError, "unknown command: " + tokens[0]);
}

std::string CliParser::helpText() const {
    return "cget 1.0\n"
           "\n"
           "Usage:\n"
           "  cget add <url> [-o <path>] [--threads N]\n"
           "  cget add <url> [--sha256 HEX] [-o <path>] [--threads N]\n"
           "  cget add <url> --queue [--sha256 HEX] [-o <path>] [--threads N]\n"
           "  cget pause <task-id>\n"
           "  cget resume <task-id>\n"
           "  cget remove <task-id>\n"
           "  cget run\n"
           "  cget list\n"
           "  cget status <task-id>\n"
           "  cget config get [key]\n"
           "  cget config set <key> <value>\n"
           "  cget protocols\n"
           "  cget recover\n"
           "  cget version\n"
           "  cget help\n"
           "\n"
           "Environment:\n"
           "  CGET_HOME  Override the default ~/.cget state directory.\n";
}

std::string CliParser::versionText() const {
    return "cget 1.0.0";
}

Command CliParser::parseAdd(const std::vector<std::string>& tokens) const {
    if (tokens.size() < 2) {
        throw CgetError(ErrorCode::InvalidCommandError, "add requires a URL");
    }

    Command command{CommandType::Add, {tokens[1]}, {}};
    for (std::size_t i = 2; i < tokens.size(); ++i) {
        if (tokens[i] == "-o" || tokens[i] == "--output") {
            if (i + 1 >= tokens.size()) {
                throw CgetError(ErrorCode::InvalidCommandError, tokens[i] + " requires a path");
            }
            command.options["output"] = tokens[++i];
            continue;
        }
        if (tokens[i] == "--threads") {
            if (i + 1 >= tokens.size()) {
                throw CgetError(ErrorCode::InvalidCommandError, "--threads requires a positive integer");
            }
            const std::string value = tokens[++i];
            if (!isUnsignedInteger(value)) {
                throw CgetError(ErrorCode::InvalidCommandError, "--threads requires a positive integer");
            }
            command.options["threads"] = value;
            continue;
        }
        if (tokens[i] == "--sha256") {
            if (i + 1 >= tokens.size()) {
                throw CgetError(ErrorCode::InvalidCommandError, "--sha256 requires a 64-character hex digest");
            }
            command.options["sha256"] = tokens[++i];
            continue;
        }
        if (tokens[i] == "--force") {
            command.options["force"] = "true";
            continue;
        }
        if (tokens[i] == "--queue") {
            command.options["queue"] = "true";
            continue;
        }
        throw CgetError(ErrorCode::InvalidCommandError, "unknown add option: " + tokens[i]);
    }
    return command;
}

Command CliParser::parseOneId(CommandType type, const std::vector<std::string>& tokens, const std::string& name) const {
    if (tokens.size() != 2) {
        throw CgetError(ErrorCode::InvalidCommandError, name + " requires a task id");
    }
    return Command{type, {tokens[1]}, {}};
}

Command CliParser::parseResume(const std::vector<std::string>& tokens) const {
    return parseOneId(CommandType::Resume, tokens, "resume");
}

Command CliParser::parseStatus(const std::vector<std::string>& tokens) const {
    return parseOneId(CommandType::Status, tokens, "status");
}

Command CliParser::parseConfig(const std::vector<std::string>& tokens) const {
    if (tokens.size() < 2) {
        throw CgetError(ErrorCode::InvalidCommandError, "config requires get or set");
    }
    if (tokens[1] == "get") {
        if (tokens.size() > 3) {
            throw CgetError(ErrorCode::InvalidCommandError, "config get accepts at most one key");
        }
        Command command{CommandType::ConfigGet, {}, {}};
        if (tokens.size() == 3) {
            command.args.push_back(tokens[2]);
        }
        return command;
    }
    if (tokens[1] == "set") {
        if (tokens.size() != 4) {
            throw CgetError(ErrorCode::InvalidCommandError, "config set requires a key and value");
        }
        return Command{CommandType::ConfigSet, {tokens[2], tokens[3]}, {}};
    }
    throw CgetError(ErrorCode::InvalidCommandError, "unknown config command: " + tokens[1]);
}

}  // namespace cget
