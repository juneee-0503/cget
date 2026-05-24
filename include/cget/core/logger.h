#pragma once

#include <string>

#include "cget/filesystem/filesystem_service.h"

namespace cget {

class Logger {
public:
    explicit Logger(FileSystemService fileSystem);

    void info(const std::string& message) const;
    void warn(const std::string& message) const;
    void error(const std::string& message) const;

private:
    void write(const std::string& level, const std::string& message) const;

    FileSystemService fileSystem_;
};

}  // namespace cget
