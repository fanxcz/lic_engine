#include <lic/scripting/runtime_session.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lic::scripting {
namespace {
constexpr std::string_view kFrameEnd = "LICFRAME_END";
constexpr std::string_view kInputRequest = "LICINPUT";
constexpr std::size_t kMaxOutputBytes = 1024U * 1024U;

[[nodiscard]] RuntimeSessionReply failure(std::string message, std::string output = {}) {
    return RuntimeSessionReply{false, std::move(output), std::move(message)};
}
#ifdef _WIN32
[[nodiscard]] std::wstring widen_utf8(const std::string& input) {
    if (input.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                    static_cast<int>(input.size()), nullptr, 0);
    UINT page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count == 0) {
        page = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(page, flags, input.data(), static_cast<int>(input.size()),
                                    nullptr, 0);
    }
    if (count <= 0) return std::wstring(input.begin(), input.end());
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    (void)MultiByteToWideChar(page, flags, input.data(), static_cast<int>(input.size()),
                              output.data(), count);
    return output;
}
[[nodiscard]] std::wstring quote_windows_argument(const std::wstring& argument) {
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t ch : argument) {
        if (ch == L'\\') ++backslashes;
        else if (ch == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result += L'\"';
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            result += ch;
            backslashes = 0;
        }
    }
    result.append(backslashes * 2U, L'\\');
    result += L'\"';
    return result;
}
#endif
} // namespace

struct RuntimeSession::Impl final {
    bool running{};
    std::string pending;
#ifdef _WIN32
    HANDLE child_input{INVALID_HANDLE_VALUE};
    HANDLE child_output{INVALID_HANDLE_VALUE};
    HANDLE process{INVALID_HANDLE_VALUE};
    HANDLE thread{INVALID_HANDLE_VALUE};
#else
    pid_t process{-1};
    int child_input{-1};
    int child_output{-1};
#endif

    [[nodiscard]] RuntimeSessionReply wait_for_marker(std::string_view expected,
                                                         std::chrono::milliseconds timeout,
                                                         std::string& output) {
        if (!running) return failure("NEXUS runtime is not running");
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto newline = pending.find('\n');
            if (newline != std::string::npos) {
                std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1U);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line == expected) return RuntimeSessionReply{true, {}, {}};
                if (line == kFrameEnd || line == kInputRequest) {
                    return failure("Unexpected NEXUS runtime protocol marker: " + line);
                }
                output += line;
                output.push_back('\n');
                if (output.size() > kMaxOutputBytes || pending.size() > kMaxOutputBytes) {
                    return failure("NEXUS runtime response exceeded the 1 MiB limit");
                }
                continue;
            }
#ifdef _WIN32
            DWORD available = 0;
            if (!PeekNamedPipe(child_output, nullptr, 0, nullptr, &available, nullptr)) {
                running = false;
                return failure("NEXUS runtime output pipe closed (Windows error " +
                               std::to_string(GetLastError()) + ")");
            }
            if (available > 0) {
                std::array<char, 4096> chunk{};
                const DWORD count = std::min<DWORD>(available, static_cast<DWORD>(chunk.size()));
                DWORD readCount = 0;
                if (!ReadFile(child_output, chunk.data(), count, &readCount, nullptr) || readCount == 0) {
                    running = false;
                    return failure("Could not read NEXUS runtime output");
                }
                pending.append(chunk.data(), static_cast<std::size_t>(readCount));
                if (pending.size() > kMaxOutputBytes) {
                    running = false;
                    return failure("NEXUS runtime line exceeded the 1 MiB limit");
                }
                continue;
            }
            if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
                running = false;
                return failure("NEXUS runtime exited without the expected protocol marker");
            }
            Sleep(1);
#else
            pollfd descriptor{child_output, POLLIN, 0};
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            const int waitMs = static_cast<int>(std::clamp<long long>(remaining.count(), 0, 4));
            const int ready = poll(&descriptor, 1, waitMs);
            if (ready < 0) {
                if (errno == EINTR) continue;
                running = false;
                return failure("poll() failed while reading NEXUS runtime: " +
                               std::error_code(errno, std::generic_category()).message());
            }
            if (ready == 0) continue;
            if ((descriptor.revents & POLLIN) != 0) {
                std::array<char, 4096> chunk{};
                const ssize_t count = read(child_output, chunk.data(), chunk.size());
                if (count > 0) {
                    pending.append(chunk.data(), static_cast<std::size_t>(count));
                    if (pending.size() > kMaxOutputBytes) {
                        running = false;
                        return failure("NEXUS runtime line exceeded the 1 MiB limit");
                    }
                    continue;
                }
                if (count < 0 && errno == EINTR) continue;
                running = false;
                return failure("NEXUS runtime output pipe closed");
            }
            if ((descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
                running = false;
                return failure("NEXUS runtime exited without the expected protocol marker");
            }
#endif
        }
        return failure("NEXUS runtime protocol timed out waiting for " + std::string(expected));
    }

    [[nodiscard]] bool write_request(std::string_view request, std::string& error) {
        if (!running) { error = "NEXUS runtime is not running"; return false; }
        std::size_t offset = 0;
        while (offset < request.size()) {
#ifdef _WIN32
            DWORD count = 0;
            const DWORD chunkSize = static_cast<DWORD>(std::min<std::size_t>(
                request.size() - offset, static_cast<std::size_t>(0x7fffffff)));
            if (!WriteFile(child_input, request.data() + offset, chunkSize, &count, nullptr) || count == 0) {
                error = "Could not write to NEXUS runtime (Windows error " +
                        std::to_string(GetLastError()) + ")";
                running = false;
                return false;
            }
            offset += static_cast<std::size_t>(count);
#else
            const ssize_t count = write(child_input, request.data() + offset, request.size() - offset);
            if (count < 0) {
                if (errno == EINTR) continue;
                error = "Could not write to NEXUS runtime: " +
                        std::error_code(errno, std::generic_category()).message();
                running = false;
                return false;
            }
            if (count == 0) { error = "NEXUS runtime input pipe closed"; running = false; return false; }
            offset += static_cast<std::size_t>(count);
#endif
        }
        return true;
    }

    void close_input() noexcept {
#ifdef _WIN32
        if (child_input != INVALID_HANDLE_VALUE) { CloseHandle(child_input); child_input = INVALID_HANDLE_VALUE; }
#else
        if (child_input >= 0) { close(child_input); child_input = -1; }
#endif
    }
    void close_output() noexcept {
#ifdef _WIN32
        if (child_output != INVALID_HANDLE_VALUE) { CloseHandle(child_output); child_output = INVALID_HANDLE_VALUE; }
#else
        if (child_output >= 0) { close(child_output); child_output = -1; }
#endif
    }

    void terminate() noexcept {
        if (running) {
            std::string output;
            const auto prompt = wait_for_marker(kInputRequest, std::chrono::milliseconds(100), output);
            if (prompt.ok) {
                std::string ignored;
                if (write_request("0\n", ignored)) {
                    (void)wait_for_marker(kFrameEnd, std::chrono::milliseconds(120), output);
                }
            }
        }
        running = false;
        close_input();
#ifdef _WIN32
        if (process != INVALID_HANDLE_VALUE) {
            if (WaitForSingleObject(process, 250) == WAIT_TIMEOUT) {
                (void)TerminateProcess(process, 1);
                (void)WaitForSingleObject(process, 250);
            }
            CloseHandle(process);
            process = INVALID_HANDLE_VALUE;
        }
        if (thread != INVALID_HANDLE_VALUE) { CloseHandle(thread); thread = INVALID_HANDLE_VALUE; }
#else
        if (process > 0) {
            int status = 0;
            pid_t waited = waitpid(process, &status, WNOHANG);
            if (waited == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                waited = waitpid(process, &status, WNOHANG);
            }
            if (waited == 0) {
                (void)kill(process, SIGTERM);
                for (int attempt = 0; attempt < 20; ++attempt) {
                    waited = waitpid(process, &status, WNOHANG);
                    if (waited == process) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            if (waited == 0) {
                (void)kill(process, SIGKILL);
                while (waitpid(process, &status, 0) < 0 && errno == EINTR) {}
            }
            process = -1;
        }
#endif
        close_output();
        pending.clear();
    }
};

RuntimeSession::RuntimeSession() : impl_(std::make_unique<Impl>()) {}
RuntimeSession::~RuntimeSession() { stop(); }

RuntimeSessionReply RuntimeSession::start(
    const std::filesystem::path& executable, const std::vector<std::string>& arguments,
    const std::filesystem::path& working_directory, const std::filesystem::path& log_file,
    std::chrono::milliseconds startup_timeout) {
    stop();
    if (!impl_) impl_ = std::make_unique<Impl>();
    std::error_code filesystemError;
    if (!log_file.parent_path().empty()) {
        std::filesystem::create_directories(log_file.parent_path(), filesystemError);
        if (filesystemError) return failure("Cannot create runtime log folder: " + filesystemError.message());
    }
    if (!std::filesystem::is_regular_file(executable, filesystemError)) {
        return failure("NEXUS runtime executable not found: " + executable.string());
    }
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE outputRead = INVALID_HANDLE_VALUE, outputWrite = INVALID_HANDLE_VALUE;
    HANDLE inputRead = INVALID_HANDLE_VALUE, inputWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&outputRead, &outputWrite, &security, 0) ||
        !CreatePipe(&inputRead, &inputWrite, &security, 0)) {
        if (outputRead != INVALID_HANDLE_VALUE) CloseHandle(outputRead);
        if (outputWrite != INVALID_HANDLE_VALUE) CloseHandle(outputWrite);
        if (inputRead != INVALID_HANDLE_VALUE) CloseHandle(inputRead);
        if (inputWrite != INVALID_HANDLE_VALUE) CloseHandle(inputWrite);
        return failure("Could not create NEXUS runtime pipes (Windows error " +
                       std::to_string(GetLastError()) + ")");
    }
    (void)SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);
    (void)SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    HANDLE logHandle = CreateFileW(log_file.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logHandle == INVALID_HANDLE_VALUE) {
        CloseHandle(outputRead); CloseHandle(outputWrite); CloseHandle(inputRead); CloseHandle(inputWrite);
        return failure("Cannot open NEXUS runtime log file");
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inputRead;
    startup.hStdOutput = outputWrite;
    startup.hStdError = logHandle;
    std::wstring commandLine = quote_windows_argument(executable.wstring());
    for (const auto& argument : arguments) {
        commandLine += L' ';
        commandLine += quote_windows_argument(widen_utf8(argument));
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    const std::wstring workDirectory = working_directory.empty() ? std::wstring() : working_directory.wstring();
    PROCESS_INFORMATION processInfo{};
    const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr,
        TRUE, CREATE_NO_WINDOW, nullptr, workDirectory.empty() ? nullptr : workDirectory.c_str(),
        &startup, &processInfo);
    CloseHandle(outputWrite); CloseHandle(inputRead); CloseHandle(logHandle);
    if (!created) {
        CloseHandle(outputRead); CloseHandle(inputWrite);
        return failure("Could not launch NEXUS runtime (Windows error " + std::to_string(GetLastError()) + ")");
    }
    impl_->child_output = outputRead;
    impl_->child_input = inputWrite;
    impl_->process = processInfo.hProcess;
    impl_->thread = processInfo.hThread;
    impl_->running = true;
#else
    (void)std::signal(SIGPIPE, SIG_IGN);
    int childInput[2]{-1, -1}, childOutput[2]{-1, -1};
    if (pipe(childInput) != 0 || pipe(childOutput) != 0) {
        const std::string message = std::error_code(errno, std::generic_category()).message();
        if (childInput[0] >= 0) close(childInput[0]);
        if (childInput[1] >= 0) close(childInput[1]);
        if (childOutput[0] >= 0) close(childOutput[0]);
        if (childOutput[1] >= 0) close(childOutput[1]);
        return failure("Could not create NEXUS runtime pipes: " + message);
    }
    const std::string executableString = std::filesystem::absolute(executable).string();
    std::vector<std::string> ownedArguments;
    ownedArguments.reserve(arguments.size() + 1);
    ownedArguments.push_back(executableString);
    ownedArguments.insert(ownedArguments.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(ownedArguments.size() + 1);
    for (auto& argument : ownedArguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    const pid_t child = fork();
    if (child < 0) {
        const std::string message = std::error_code(errno, std::generic_category()).message();
        close(childInput[0]); close(childInput[1]); close(childOutput[0]); close(childOutput[1]);
        return failure("fork() failed: " + message);
    }
    if (child == 0) {
        close(childInput[1]); close(childOutput[0]);
        const int logDescriptor = open(log_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logDescriptor < 0) _exit(126);
        (void)dup2(childInput[0], STDIN_FILENO);
        (void)dup2(childOutput[1], STDOUT_FILENO);
        (void)dup2(logDescriptor, STDERR_FILENO);
        if (childInput[0] > STDERR_FILENO) close(childInput[0]);
        if (childOutput[1] > STDERR_FILENO) close(childOutput[1]);
        if (logDescriptor > STDERR_FILENO) close(logDescriptor);
        if (!working_directory.empty() && chdir(working_directory.c_str()) != 0) _exit(126);
        execv(executableString.c_str(), argv.data());
        _exit(127);
    }
    close(childInput[0]); close(childOutput[1]);
    impl_->child_input = childInput[1];
    impl_->child_output = childOutput[0];
    impl_->process = child;
    impl_->running = true;
#endif
    std::string output;
    const auto startupReply = impl_->wait_for_marker(kFrameEnd, startup_timeout, output);
    if (!startupReply.ok) {
        impl_->terminate();
        return failure(startupReply.error, std::move(output));
    }
    return RuntimeSessionReply{true, std::move(output), {}};
}

RuntimeSessionReply RuntimeSession::exchange(const std::vector<std::int64_t>& values,
                                             std::chrono::milliseconds timeout) {
    if (!impl_ || !impl_->running) return failure("NEXUS runtime is not running");
    if (values.empty()) return failure("NEXUS runtime request cannot be empty");
    std::string output;
    for (const std::int64_t value : values) {
        // The native NEXUS input helper reads one line at a time and does not
        // preserve extra bytes from a multi-line pipe read. Wait for LICINPUT
        // before sending every value, so no subsequent event data can be lost.
        const auto prompt = impl_->wait_for_marker(kInputRequest, timeout, output);
        if (!prompt.ok) {
            impl_->terminate();
            return failure(prompt.error, std::move(output));
        }
        std::string error;
        const std::string request = std::to_string(value) + '\n';
        if (!impl_->write_request(request, error)) {
            impl_->terminate();
            return failure(std::move(error), std::move(output));
        }
    }
    const auto frame = impl_->wait_for_marker(kFrameEnd, timeout, output);
    if (!frame.ok) {
        impl_->terminate();
        return failure(frame.error, std::move(output));
    }
    return RuntimeSessionReply{true, std::move(output), {}};
}

void RuntimeSession::stop() noexcept {
    if (impl_) impl_->terminate();
}
bool RuntimeSession::running() const noexcept {
    return impl_ && impl_->running;
}
} // namespace lic::scripting
