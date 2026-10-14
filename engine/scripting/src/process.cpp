#include <lic/scripting/process.hpp>

#include <cerrno>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

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
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lic::scripting {
namespace {

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

#ifdef _WIN32
std::wstring widen_utf8(const std::string& input) {
    if (input.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                    static_cast<int>(input.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count == 0) {
        codePage = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(codePage, flags, input.data(),
                                    static_cast<int>(input.size()), nullptr, 0);
    }
    if (count <= 0) return std::wstring(input.begin(), input.end());
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    (void)MultiByteToWideChar(codePage, flags, input.data(), static_cast<int>(input.size()),
                              output.data(), count);
    return output;
}

std::wstring quote_windows_argument(const std::wstring& argument) {
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'\"') {
            result.append(backslashes * 2U + 1U, L'\\');
            result += L'\"';
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            result += character;
            backslashes = 0;
        }
    }
    result.append(backslashes * 2U, L'\\');
    result += L'\"';
    return result;
}

std::wstring path_argument(const std::filesystem::path& path) {
    return path.wstring();
}
#else
std::string path_argument(const std::filesystem::path& path) {
    return path.string();
}
#endif

} // namespace

ProcessResult run_process_capture(const std::filesystem::path& executable,
                                  const std::vector<std::string>& arguments,
                                  const std::filesystem::path& working_directory,
                                  const std::filesystem::path& log_file) {
    ProcessResult result;
    std::error_code filesystemError;
    if (!log_file.parent_path().empty()) {
        std::filesystem::create_directories(log_file.parent_path(), filesystemError);
        if (filesystemError) {
            result.output = "Cannot create log folder: " + filesystemError.message();
            return result;
        }
    }
    if (!std::filesystem::is_regular_file(executable, filesystemError)) {
        result.output = "Executable was not found: " + executable.string();
        return result;
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE logHandle = CreateFileW(log_file.c_str(), GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logHandle == INVALID_HANDLE_VALUE) {
        result.output = "Cannot create process log file.";
        return result;
    }
    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullInput == INVALID_HANDLE_VALUE ? GetStdHandle(STD_INPUT_HANDLE) : nullInput;
    startup.hStdOutput = logHandle;
    startup.hStdError = logHandle;

    std::wstring commandLine = quote_windows_argument(executable.wstring());
    for (const auto& argument : arguments) {
        commandLine += L' ';
        commandLine += quote_windows_argument(widen_utf8(argument));
    }
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    const std::wstring workDirectory = working_directory.empty() ? std::wstring() : working_directory.wstring();
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable.c_str(), mutableCommand.data(), nullptr, nullptr,
                                        TRUE, CREATE_NO_WINDOW, nullptr,
                                        workDirectory.empty() ? nullptr : workDirectory.c_str(),
                                        &startup, &process);
    CloseHandle(logHandle);
    if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);
    if (!created) {
        result.output = "Could not launch process (Windows error " +
                        std::to_string(GetLastError()) + ").";
        return result;
    }
    result.started = true;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    if (GetExitCodeProcess(process.hProcess, &exitCode)) {
        result.exit_code = static_cast<int>(exitCode);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
#else
    const pid_t child = fork();
    if (child < 0) {
        result.output = "fork() failed: " + std::error_code(errno, std::generic_category()).message();
        return result;
    }
    if (child == 0) {
        const std::string outputPath = log_file.string();
        const int logDescriptor = open(outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logDescriptor < 0) _exit(126);
        (void)dup2(logDescriptor, STDOUT_FILENO);
        (void)dup2(logDescriptor, STDERR_FILENO);
        if (logDescriptor > STDERR_FILENO) close(logDescriptor);
        if (!working_directory.empty() && chdir(working_directory.c_str()) != 0) _exit(126);

        const std::string executableString = std::filesystem::absolute(executable).string();
        std::vector<std::string> ownedArguments;
        ownedArguments.reserve(arguments.size() + 1);
        ownedArguments.push_back(executableString);
        ownedArguments.insert(ownedArguments.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        argv.reserve(ownedArguments.size() + 1);
        for (auto& argument : ownedArguments) argv.push_back(argument.data());
        argv.push_back(nullptr);
        execv(executableString.c_str(), argv.data());
        _exit(127);
    }

    result.started = true;
    int waitStatus = 0;
    while (waitpid(child, &waitStatus, 0) < 0) {
        if (errno == EINTR) continue;
        result.started = false;
        result.output = "waitpid() failed: " + std::error_code(errno, std::generic_category()).message();
        return result;
    }
    if (WIFEXITED(waitStatus)) result.exit_code = WEXITSTATUS(waitStatus);
    else if (WIFSIGNALED(waitStatus)) result.exit_code = 128 + WTERMSIG(waitStatus);
#endif
    result.output = read_text_file(log_file);
    return result;
}

} // namespace lic::scripting
