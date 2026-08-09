#include "subprocess.h"

#ifndef _WIN32
// Unix includes
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>
#else
// Windows includes
#include <windows.h>
#include <limits>
#endif

#include <cstring>

// ============================================================================
// Destructor
// ============================================================================

Subprocess::~Subprocess()
{
    if (_spawned)
    {
        terminate();
        waitForFinished(1000); // Brief wait for cleanup
        cleanup();
    }
}

// ============================================================================
// Unix implementation (macOS + Linux)
// ============================================================================

#ifndef _WIN32

bool Subprocess::spawn(const std::string& command, const std::vector<std::string>& args)
{
    // Create pipes for stdin, stdout, stderr
    int stdinPipe[2] = {-1, -1};
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};

    if (pipe(stdinPipe) != 0)
    {
        _lastError = "Failed to create stdin pipe: " + std::string(strerror(errno));
        return false;
    }

    if (pipe(stdoutPipe) != 0)
    {
        _lastError = "Failed to create stdout pipe: " + std::string(strerror(errno));
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        return false;
    }

    if (pipe(stderrPipe) != 0)
    {
        _lastError = "Failed to create stderr pipe: " + std::string(strerror(errno));
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        return false;
    }

    _pid = fork();

    if (_pid < 0)
    {
        _lastError = "fork() failed: " + std::string(strerror(errno));
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[0]);
        close(stderrPipe[1]);
        return false;
    }

    if (_pid == 0)
    {
        // === Child process ===

        // Redirect stdin
        dup2(stdinPipe[0], STDIN_FILENO);

        // Redirect stdout
        dup2(stdoutPipe[1], STDOUT_FILENO);

        // Redirect stderr
        dup2(stderrPipe[1], STDERR_FILENO);

        // Close all pipe FDs (duplicates are in place)
        close(stdinPipe[0]);
        close(stdinPipe[1]);
        close(stdoutPipe[0]);
        close(stdoutPipe[1]);
        close(stderrPipe[0]);
        close(stderrPipe[1]);

        // Build argv array
        std::vector<char*> argv;
        argv.reserve(args.size() + 2);

        // argv[0] = program name (convention)
        std::string programName = command;
        argv.push_back(const_cast<char*>(programName.c_str()));

        for (const auto& arg : args)
        {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        // Execute
        execvp(command.c_str(), argv.data());

        // If execvp returns, it failed
        const char* err = strerror(errno);
        fprintf(stderr, "execvp failed for '%s': %s\n", command.c_str(), err);
        _exit(127);
    }

    // === Parent process ===

    // Close child-side pipe FDs
    close(stdinPipe[0]);
    close(stdoutPipe[1]);
    close(stderrPipe[1]);

    _stdinFd = stdinPipe[1];
    _stdoutFd = stdoutPipe[0];
    _stderrFd = stderrPipe[0];

    // Set stderr to non-blocking for readStderr()
    int flags = fcntl(_stderrFd, F_GETFL, 0);
    fcntl(_stderrFd, F_SETFL, flags | O_NONBLOCK);

    _spawned = true;
    return true;
}

bool Subprocess::writeToStdin(const void* data, size_t size)
{
    if (!_spawned || _stdinFd < 0)
    {
        _lastError = "Process not spawned or stdin closed";
        return false;
    }

    const uint8_t* buf = static_cast<const uint8_t*>(data);
    size_t totalWritten = 0;

    while (totalWritten < size)
    {
        ssize_t written = write(_stdinFd, buf + totalWritten, size - totalWritten);

        if (written < 0)
        {
            if (errno == EINTR)
                continue;

            _lastError = "write() failed: " + std::string(strerror(errno));
            return false;
        }

        if (written == 0)
        {
            _lastError = "write() returned 0 (pipe closed)";
            return false;
        }

        totalWritten += static_cast<size_t>(written);
    }

    return true;
}

void Subprocess::closeStdin()
{
    if (_stdinFd >= 0)
    {
        close(_stdinFd);
        _stdinFd = -1;
    }
}

std::string Subprocess::readStderr()
{
    if (!_spawned || _stderrFd < 0)
        return {};

    std::string result;
    char buffer[4096];

    while (true)
    {
        ssize_t bytesRead = read(_stderrFd, buffer, sizeof(buffer));

        if (bytesRead > 0)
        {
            result.append(buffer, static_cast<size_t>(bytesRead));
            continue; // Try to read more
        }

        if (bytesRead == 0)
        {
            // EOF
            break;
        }

        // EAGAIN/EWOULDBLOCK = no more data available right now
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;

        if (errno == EINTR)
            continue;

        // Some other error
        break;
    }

    return result;
}

static std::string readAllFromFd(int fd)
{
    if (fd < 0)
        return {};

    // Set descriptor back to blocking mode
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    std::string result;
    char buffer[4096];

    while (true)
    {
        ssize_t bytesRead = read(fd, buffer, sizeof(buffer));

        if (bytesRead > 0)
        {
            result.append(buffer, static_cast<size_t>(bytesRead));
            continue;
        }

        if (bytesRead == 0)
            break; // EOF

        if (errno == EINTR)
            continue;

        break; // Error
    }

    return result;
}

std::string Subprocess::readAllStdout()
{
    if (!_spawned)
        return {};

    return readAllFromFd(_stdoutFd);
}

std::string Subprocess::readAllStderr()
{
    if (!_spawned || _stderrFd < 0)
        return {};

    // Set stderr back to blocking mode
    int flags = fcntl(_stderrFd, F_GETFL, 0);
    fcntl(_stderrFd, F_SETFL, flags & ~O_NONBLOCK);

    std::string result;
    char buffer[4096];

    while (true)
    {
        ssize_t bytesRead = read(_stderrFd, buffer, sizeof(buffer));

        if (bytesRead > 0)
        {
            result.append(buffer, static_cast<size_t>(bytesRead));
            continue;
        }

        if (bytesRead == 0)
            break; // EOF

        if (errno == EINTR)
            continue;

        break; // Error
    }

    return result;
}

bool Subprocess::isRunning() const
{
    if (!_spawned || _pid <= 0)
        return false;

    // Check if process exists
    int result = waitpid(_pid, nullptr, WNOHANG);
    return result == 0; // 0 = still running
}

int Subprocess::waitForFinished(int timeoutMs)
{
    if (!_spawned || _pid <= 0)
        return _exitCode;

    if (timeoutMs < 0)
    {
        // Wait indefinitely
        int status = 0;
        while (waitpid(_pid, &status, 0) < 0)
        {
            if (errno == EINTR)
                continue;
            _lastError = "waitpid() failed: " + std::string(strerror(errno));
            return -1;
        }

        if (WIFEXITED(status))
            _exitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            _exitCode = 128 + WTERMSIG(status);
        else
            _exitCode = -1;

        return _exitCode;
    }

    // Poll with timeout
    int elapsed = 0;
    int interval = 50; // ms

    while (elapsed < timeoutMs)
    {
        int status = 0;
        int result = waitpid(_pid, &status, WNOHANG);

        if (result == _pid)
        {
            // Process finished
            if (WIFEXITED(status))
                _exitCode = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                _exitCode = 128 + WTERMSIG(status);
            else
                _exitCode = -1;

            return _exitCode;
        }

        if (result < 0 && errno != EINTR)
        {
            _lastError = "waitpid() failed: " + std::string(strerror(errno));
            return -1;
        }

        // Sleep for interval (use usleep)
        int sleepUs = interval * 1000;
        usleep(sleepUs);
        elapsed += interval;
    }

    return -1; // Timeout
}

void Subprocess::terminate()
{
    if (!_spawned || _pid <= 0)
        return;

    ::kill(_pid, SIGTERM);
}

void Subprocess::kill()
{
    if (!_spawned || _pid <= 0)
        return;

    ::kill(_pid, SIGKILL);
}

void Subprocess::closeStderr()
{
    if (_stderrFd >= 0)
    {
        close(_stderrFd);
        _stderrFd = -1;
    }
}

void Subprocess::cleanup()
{
    closeStdin();

    if (_stdoutFd >= 0)
    {
        close(_stdoutFd);
        _stdoutFd = -1;
    }

    if (_stderrFd >= 0)
    {
        close(_stderrFd);
        _stderrFd = -1;
    }

    _pid = -1;
    _spawned = false;
}

// ============================================================================
// Windows implementation
// ============================================================================

#else // _WIN32

bool Subprocess::spawn(const std::string& command, const std::vector<std::string>& args)
{
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    // Create stdin pipe
    HANDLE hStdinRead = nullptr, hStdinWrite = nullptr;
    if (!CreatePipe(&hStdinRead, &hStdinWrite, &saAttr, 0))
    {
        _lastError = "CreatePipe(stdin) failed";
        return false;
    }
    // Ensure write handle is not inherited
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    // Create stdout pipe
    HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &saAttr, 0))
    {
        _lastError = "CreatePipe(stdout) failed";
        CloseHandle(hStdinRead);
        CloseHandle(hStdinWrite);
        return false;
    }
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    // Create stderr pipe
    HANDLE hStderrRead = nullptr, hStderrWrite = nullptr;
    if (!CreatePipe(&hStderrRead, &hStderrWrite, &saAttr, 0))
    {
        _lastError = "CreatePipe(stderr) failed";
        CloseHandle(hStdinRead);
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        CloseHandle(hStdoutWrite);
        return false;
    }
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    // Build command line
    std::string cmdLine = "\"" + command + "\"";
    for (const auto& arg : args)
    {
        cmdLine += " ";
        // Quote argument if it contains spaces
        if (arg.find(' ') != std::string::npos)
            cmdLine += "\"" + arg + "\"";
        else
            cmdLine += arg;
    }

    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hStderrWrite;
    si.hStdOutput = hStdoutWrite;
    si.hStdInput = hStdinRead;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    BOOL success = CreateProcessA(
        nullptr,
        const_cast<LPSTR>(cmdLine.c_str()),
        nullptr,
        nullptr,
        TRUE, // Inherit handles
        0,
        nullptr,
        nullptr,
        &si,
        &pi);

    // Close child-side handles in parent
    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    if (!success)
    {
        DWORD err = GetLastError();
        _lastError = "CreateProcessA failed, error code: " + std::to_string(err);
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        return false;
    }

    _hProcess = pi.hProcess;
    _hThread = pi.hThread;
    _hStdinWrite = hStdinWrite;
    _hStdoutRead = hStdoutRead;
    _hStderrRead = hStderrRead;

    _spawned = true;
    return true;
}

bool Subprocess::writeToStdin(const void* data, size_t size)
{
    if (!_spawned || !_hStdinWrite)
    {
        _lastError = "Process not spawned or stdin closed";
        return false;
    }

    DWORD bytesWritten = 0;
    const uint8_t* buf = static_cast<const uint8_t*>(data);
    size_t totalWritten = 0;

    while (totalWritten < size)
    {
        DWORD toWrite = static_cast<DWORD>(min(size - totalWritten, static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
        if (!WriteFile(_hStdinWrite, buf + totalWritten, toWrite, &bytesWritten, nullptr))
        {
            DWORD err = GetLastError();
            _lastError = "WriteFile failed, error code: " + std::to_string(err);
            return false;
        }
        totalWritten += bytesWritten;
    }

    return true;
}

void Subprocess::closeStdin()
{
    if (_hStdinWrite)
    {
        CloseHandle(_hStdinWrite);
        _hStdinWrite = nullptr;
    }
}

std::string Subprocess::readStderr()
{
    if (!_spawned || !_hStderrRead)
        return {};

    std::string result;
    char buffer[4096];
    DWORD bytesRead = 0;
    DWORD bytesAvailable = 0;

    while (PeekNamedPipe(_hStderrRead, nullptr, 0, nullptr, &bytesAvailable, nullptr) && bytesAvailable > 0)
    {
        DWORD toRead = min(bytesAvailable, static_cast<DWORD>(sizeof(buffer)));
        if (ReadFile(_hStderrRead, buffer, toRead, &bytesRead, nullptr) && bytesRead > 0)
        {
            result.append(buffer, bytesRead);
        }
        else
        {
            break;
        }
    }

    return result;
}

std::string Subprocess::readAllStderr()
{
    if (!_spawned || !_hStderrRead)
        return {};

    std::string result;
    char buffer[4096];
    DWORD bytesRead = 0;

    while (ReadFile(_hStderrRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
    {
        result.append(buffer, bytesRead);
    }

    return result;
}

std::string Subprocess::readAllStdout()
{
    if (!_spawned || !_hStdoutRead)
        return {};

    std::string result;
    char buffer[4096];
    DWORD bytesRead = 0;

    while (ReadFile(_hStdoutRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
    {
        result.append(buffer, bytesRead);
    }

    return result;
}

bool Subprocess::isRunning() const
{
    if (!_spawned || !_hProcess)
        return false;

    DWORD exitCode = 0;
    if (GetExitCodeProcess(_hProcess, &exitCode))
    {
        return exitCode == STILL_ACTIVE;
    }
    return false;
}

int Subprocess::waitForFinished(int timeoutMs)
{
    if (!_spawned || !_hProcess)
        return _exitCode;

    DWORD waitResult = WaitForSingleObject(_hProcess, timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs));

    if (waitResult == WAIT_OBJECT_0)
    {
        DWORD exitCode = 0;
        if (GetExitCodeProcess(_hProcess, &exitCode))
        {
            _exitCode = static_cast<int>(exitCode);
        }
        return _exitCode;
    }

    return -1; // Timeout or error
}

void Subprocess::terminate()
{
    if (!_spawned || !_hProcess)
        return;

    TerminateProcess(_hProcess, 1);
}

void Subprocess::kill()
{
    // On Windows, TerminateProcess is already unconditional (no SIGTERM equivalent)
    terminate();
}

void Subprocess::closeStderr()
{
    if (_hStderrRead)
    {
        CloseHandle(_hStderrRead);
        _hStderrRead = nullptr;
    }
}

void Subprocess::cleanup()
{
    closeStdin();

    if (_hStdoutRead)
    {
        CloseHandle(_hStdoutRead);
        _hStdoutRead = nullptr;
    }

    if (_hStderrRead)
    {
        CloseHandle(_hStderrRead);
        _hStderrRead = nullptr;
    }

    if (_hThread)
    {
        CloseHandle(_hThread);
        _hThread = nullptr;
    }

    if (_hProcess)
    {
        CloseHandle(_hProcess);
        _hProcess = nullptr;
    }

    _spawned = false;
}

#endif // _WIN32
