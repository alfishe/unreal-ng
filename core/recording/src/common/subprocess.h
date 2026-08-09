#pragma once

// NOTE: This file is intentionally named `subprocess.h` (not `process.h`) to avoid
// shadowing the C runtime header <process.h> on Windows. A local file named
// `process.h` placed in an include path (-I) directory causes MSVC to pick it up
// instead of the CRT <process.h> even for angle-bracket includes like
// `#include <process.h>` performed by MSVC's own <thread> header. That starves
// std::thread of the `_beginthreadex` symbol it needs, breaking any translation
// unit that instantiates std::thread. See MSVC <thread> line ~13/76.

#include <cstdint>
#include <string>
#include <vector>

/// @brief Cross-platform subprocess management
///
/// Spawns child processes with stdin/stdout/stderr pipe redirection.
/// Used for launching external ffmpeg binary with pipe-based frame/audio input.
///
/// Usage:
/// 1. Call spawn() with command and arguments
/// 2. Write to stdin via writeToStdin()
/// 3. Read stderr via readStderr()
/// 4. Call closeStdin() when done feeding data
/// 5. Call waitForFinished() to get exit code
/// 6. Destructor calls terminate() if still running
class Subprocess
{
public:
    Subprocess() = default;
    ~Subprocess();

    // Non-copyable, non-movable (owns OS handles)
    Subprocess(const Subprocess&) = delete;
    Subprocess& operator=(const Subprocess&) = delete;
    Subprocess(Subprocess&&) = delete;
    Subprocess& operator=(Subprocess&&) = delete;

    /// @brief Spawn a child process with stdin as a pipe
    /// @param command Path to executable
    /// @param args Command-line arguments (excluding the command itself)
    /// @return true if process spawned successfully
    bool spawn(const std::string& command, const std::vector<std::string>& args);

    /// @brief Write data to child's stdin pipe
    /// @param data Pointer to data buffer
    /// @param size Number of bytes to write
    /// @return true if all bytes were written
    bool writeToStdin(const void* data, size_t size);

    /// @brief Close the stdin pipe (signals EOF to child process)
    void closeStdin();

    /// @brief Read available stderr output (non-blocking)
    /// @return String containing stderr output since last read
    std::string readStderr();

    /// @brief Read all stderr until EOF (blocking)
    /// @return All remaining stderr output
    std::string readAllStderr();

    /// @brief Read all stdout until EOF (blocking)
    /// @return All remaining stdout output
    std::string readAllStdout();

    /// @brief Check if child process is still running
    bool isRunning() const;

    /// @brief Wait for process to finish
    /// @param timeoutMs Timeout in milliseconds (-1 = infinite)
    /// @return Process exit code, or -1 if timed out
    int waitForFinished(int timeoutMs = -1);

    /// @brief Terminate the child process (SIGTERM / TerminateProcess)
    void terminate();

    /// @brief Force-kill the child process (SIGKILL / TerminateProcess)
    void kill();

    /// @brief Close stderr pipe (unblocks readAllStderr)
    void closeStderr();

    /// @brief Get last exit code (valid after waitForFinished returns >= 0)
    int exitCode() const { return _exitCode; }

    /// @brief Get last error message
    std::string getLastError() const { return _lastError; }

private:
    void cleanup();

#ifdef _WIN32
    void* _hProcess = nullptr;   // HANDLE
    void* _hThread = nullptr;    // HANDLE
    void* _hStdinWrite = nullptr;   // HANDLE
    void* _hStdoutRead = nullptr;   // HANDLE
    void* _hStderrRead = nullptr;   // HANDLE
#else
    int _pid = -1;
    int _stdinFd = -1;
    int _stdoutFd = -1;
    int _stderrFd = -1;
#endif

    int _exitCode = -1;
    bool _spawned = false;
    std::string _lastError;
};
