#include "common/childprocess.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>
#include <vector>
#else
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace childprocess
{

#ifdef _WIN32

namespace
{
/// One argument quoted the way CommandLineToArgvW reads it back
std::wstring Quote(const std::wstring& arg)
{
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
        return arg;
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg)
    {
        if (c == L'\\')
        {
            ++slashes;
            continue;
        }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        out.push_back(c);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring Wide(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}
} // namespace

std::unique_ptr<Child> Child::Start(const Spec& spec, std::string& error)
{
    std::wstring line = Quote(Wide(spec.path));
    for (const std::string& arg : spec.args)
        line += L" " + Quote(Wide(arg));

    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE log = INVALID_HANDLE_VALUE;
    if (!spec.logFile.empty())
        log = CreateFileW(Wide(spec.logFile).c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE)
        log = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0,
                          nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nullptr;
    startup.hStdOutput = log;
    startup.hStdError = log;
    PROCESS_INFORMATION info{};
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');
    const BOOL ok = CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &info);
    const DWORD why = GetLastError();
    if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);
    if (!ok)
    {
        error = "cannot start " + spec.path + " (error " + std::to_string(why) + ")";
        return nullptr;
    }
    CloseHandle(info.hThread);
    std::unique_ptr<Child> child(new Child());
    child->_pid = static_cast<int>(info.dwProcessId);
    child->_process = info.hProcess;
    return child;
}

Child::~Child()
{
    if (_process)
        CloseHandle(static_cast<HANDLE>(_process));
}

bool Child::Exited(int& code, bool& crashed)
{
    if (!_done && _process && WaitForSingleObject(static_cast<HANDLE>(_process), 0) == WAIT_OBJECT_0)
    {
        DWORD exit = 0;
        GetExitCodeProcess(static_cast<HANDLE>(_process), &exit);
        _done = true;
        _code = static_cast<int>(exit);
        _crashed = exit >= 0xC0000000u;  // an NTSTATUS exception: access violation, abort
    }
    code = _code;
    crashed = _crashed;
    return _done;
}

void Child::Terminate()
{
    Kill();
}

void Child::Kill()
{
    if (!_done && _process)
        TerminateProcess(static_cast<HANDLE>(_process), 1);
}

#else

std::unique_ptr<Child> Child::Start(const Spec& spec, std::string& error)
{
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, spec.logFile.empty() ? "/dev/null" : spec.logFile.c_str(),
                                     O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    short flags = POSIX_SPAWN_SETPGROUP;  // its own group: a Ctrl-C at the emulator's terminal is not its
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;  // macOS: only 0, 1, 2 reach it, none of the emulator's sockets
#endif
    posix_spawnattr_setflags(&attr, flags);
    posix_spawnattr_setpgroup(&attr, 0);

    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(spec.path.c_str()));
    for (const std::string& arg : spec.args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, spec.path.c_str(), &actions, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (rc != 0)
    {
        error = "cannot start " + spec.path + ": " + std::strerror(rc);
        return nullptr;
    }
    std::unique_ptr<Child> child(new Child());
    child->_pid = static_cast<int>(pid);
    return child;
}

Child::~Child() = default;

bool Child::Exited(int& code, bool& crashed)
{
    if (!_done && _pid > 0)
    {
        int status = 0;
        const pid_t r = waitpid(static_cast<pid_t>(_pid), &status, WNOHANG);
        if (r == static_cast<pid_t>(_pid))
        {
            _done = true;
            _crashed = WIFSIGNALED(status) && WTERMSIG(status) != SIGTERM;
            _code = WIFEXITED(status) ? WEXITSTATUS(status) : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        }
        else if (r < 0 && errno == ECHILD)
            _done = true;  // reaped elsewhere
    }
    code = _code;
    crashed = _crashed;
    return _done;
}

void Child::Terminate()
{
    if (!_done && _pid > 0)
        kill(static_cast<pid_t>(_pid), SIGTERM);
}

void Child::Kill()
{
    if (!_done && _pid > 0)
        kill(static_cast<pid_t>(_pid), SIGKILL);
}

#endif

} // namespace childprocess
