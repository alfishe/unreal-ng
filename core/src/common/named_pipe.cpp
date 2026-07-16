#include "named_pipe.h"

#ifndef _WIN32
// Unix includes
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <csignal>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
// Windows includes
#include <windows.h>
#include <cstring>
#endif

// ============================================================================
// Destructor
// ============================================================================

NamedPipe::~NamedPipe()
{
    close();
}

// ============================================================================
// Unix implementation (macOS + Linux)
// ============================================================================

#ifndef _WIN32

bool NamedPipe::create(const std::string& path)
{
    // Remove existing pipe if any
    ::unlink(path.c_str());

    // Create FIFO with read/write permissions
    if (mkfifo(path.c_str(), 0666) != 0)
    {
        if (errno != EEXIST)
        {
            return false;
        }
    }

    chmod(path.c_str(), 0666);
    return true;
}

bool NamedPipe::openForWrite(const std::string& path, int timeoutMs)
{
    return openForWriteTimeout(path, timeoutMs, nullptr);
}

bool NamedPipe::openForWriteTimeout(const std::string& path, int timeoutMs, const std::atomic<bool>* abortFlag)
{
    _path = path;

    // Ignore SIGPIPE — we handle EPIPE in write() instead
    std::signal(SIGPIPE, SIG_IGN);

    // Retry the non-blocking open until a reader (ffmpeg) connects.
    // O_WRONLY|O_NONBLOCK returns ENXIO when no reader exists — that's normal.
    const int retrySliceMs = 100;
    int remaining = timeoutMs;

    while (remaining > 0)
    {
        if (abortFlag && abortFlag->load(std::memory_order_relaxed))
        {
            _lastError = "Pipe open aborted";
            return false;
        }

        _fd = ::open(path.c_str(), O_WRONLY | O_NONBLOCK);

        if (_fd >= 0)
        {
            // Reader is connected — switch back to blocking mode for natural backpressure
            int flags = ::fcntl(_fd, F_GETFL, 0);
            ::fcntl(_fd, F_SETFL, flags & ~O_NONBLOCK);
            return true;
        }

        if (errno != ENXIO)
        {
            // Real error, not just "no reader yet"
            _lastError = "Cannot open pipe '" + path + "': " + std::string(strerror(errno));
            return false;
        }

        // ENXIO: no reader yet, wait and retry
        int slice = (remaining < retrySliceMs) ? remaining : retrySliceMs;
        usleep(slice * 1000);
        remaining -= slice;
    }

    _lastError = "Timed out waiting for ffmpeg to connect to pipe '" + path + "' (" + std::to_string(timeoutMs) + "ms)";
    return false;
}

bool NamedPipe::write(const void* data, size_t size, const std::atomic<bool>* abortFlag)
{
    if (_fd < 0)
    {
        _lastError = "Pipe not open";
        return false;
    }

    const uint8_t* buf = static_cast<const uint8_t*>(data);
    size_t totalWritten = 0;

    while (totalWritten < size)
    {
        if (abortFlag && abortFlag->load(std::memory_order_relaxed))
        {
            _lastError = "Write aborted";
            return false;
        }

        ssize_t written = ::write(_fd, buf + totalWritten, size - totalWritten);

        if (written < 0)
        {
            if (errno == EINTR)
                continue;

            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Buffer full — poll for writability
                struct pollfd pfd;
                pfd.fd = _fd;
                pfd.events = POLLOUT;
                pfd.revents = 0;
                ::poll(&pfd, 1, 200);
                continue;
            }

            if (errno == EPIPE)
            {
                _lastError = "Broken pipe: reader has exited";
                return false;
            }

            _lastError = "write() failed: " + std::string(strerror(errno));
            return false;
        }

        if (written == 0)
        {
            _lastError = "write() returned 0";
            return false;
        }

        totalWritten += static_cast<size_t>(written);
    }

    return true;
}

void NamedPipe::close()
{
    if (_fd >= 0)
    {
        ::close(_fd);
        _fd = -1;
    }
}

void NamedPipe::remove(const std::string& path)
{
    ::unlink(path.c_str());
}

bool NamedPipe::isOpen() const
{
    return _fd >= 0;
}

// ============================================================================
// Windows implementation
// ============================================================================

#else // _WIN32

bool NamedPipe::create(const std::string& path)
{
    (void)path;
    return true;
}

bool NamedPipe::openForWrite(const std::string& path, int timeoutMs)
{
    return openForWriteTimeout(path, timeoutMs, nullptr);
}

bool NamedPipe::openForWriteTimeout(const std::string& path, int timeoutMs, const std::atomic<bool>* abortFlag)
{
    _path = path;

    std::string pipeName = "\\\\.\\pipe\\" + path;

    _handle = CreateNamedPipeA(
        pipeName.c_str(),
        PIPE_ACCESS_OUTBOUND,
        PIPE_TYPE_BYTE | PIPE_WAIT,
        1,
        65536,
        65536,
        static_cast<DWORD>(timeoutMs > 0 ? timeoutMs : 0),
        nullptr);

    if (_handle == INVALID_HANDLE_VALUE)
    {
        _lastError = "CreateNamedPipe failed, error: " + std::to_string(GetLastError());
        _handle = nullptr;
        return false;
    }

    if (!ConnectNamedPipe(_handle, nullptr))
    {
        DWORD err = GetLastError();
        if (err != ERROR_PIPE_CONNECTED)
        {
            _lastError = "ConnectNamedPipe failed, error: " + std::to_string(err);
            CloseHandle(_handle);
            _handle = nullptr;
            return false;
        }
    }

    return true;
}

bool NamedPipe::write(const void* data, size_t size, const std::atomic<bool>* abortFlag)
{
    if (!_handle)
    {
        _lastError = "Pipe not open";
        return false;
    }

    const uint8_t* buf = static_cast<const uint8_t*>(data);
    size_t totalWritten = 0;

    while (totalWritten < size)
    {
        if (abortFlag && abortFlag->load(std::memory_order_relaxed))
        {
            _lastError = "Write aborted";
            return false;
        }

        DWORD bytesWritten = 0;
        DWORD toWrite = static_cast<DWORD>(min(size - totalWritten, static_cast<size_t>(DWORD_MAX)));

        if (!WriteFile(_handle, buf + totalWritten, toWrite, &bytesWritten, nullptr))
        {
            _lastError = "WriteFile failed, error: " + std::to_string(GetLastError());
            return false;
        }

        if (bytesWritten == 0)
        {
            _lastError = "WriteFile returned 0";
            return false;
        }

        totalWritten += bytesWritten;
    }

    return true;
}

void NamedPipe::close()
{
    if (_handle)
    {
        CloseHandle(_handle);
        _handle = nullptr;
    }
}

void NamedPipe::remove(const std::string& path)
{
    (void)path;
}

bool NamedPipe::isOpen() const
{
    return _handle != nullptr;
}

#endif // _WIN32
