#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace lic::scripting {

struct ProcessResult final {
    bool started{};
    int exit_code{-1};
    std::string output;
};

// Runs a child process without a shell and captures stdout/stderr to a log file.
[[nodiscard]] ProcessResult run_process_capture(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const std::filesystem::path& working_directory,
    const std::filesystem::path& log_file);

} // namespace lic::scripting
