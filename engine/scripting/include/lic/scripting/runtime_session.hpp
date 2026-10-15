#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lic::scripting {

struct RuntimeSessionReply final {
    bool ok{};
    std::string output;
    std::string error;
};

// Owns one long-running native NEXUS program. Requests are integer values,
// one per line; each response ends with LICFRAME_END. Timeouts/failures are reported.
class RuntimeSession final {
public:
    RuntimeSession();
    ~RuntimeSession();
    RuntimeSession(const RuntimeSession&) = delete;
    RuntimeSession& operator=(const RuntimeSession&) = delete;

    [[nodiscard]] RuntimeSessionReply start(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& working_directory,
        const std::filesystem::path& log_file,
        std::chrono::milliseconds startup_timeout = std::chrono::seconds(3));

    [[nodiscard]] RuntimeSessionReply exchange(
        const std::vector<std::int64_t>& values,
        std::chrono::milliseconds timeout = std::chrono::milliseconds(8));

    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace lic::scripting
