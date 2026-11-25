#include "named_pipe.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#else
#include <windows.h>
#include <sddl.h>
#endif

namespace
{
/// The most one write hands over at once (a blocking write of more goes in such pieces)
constexpr size_t kWriteChunk = 256 * 1024;
}  // namespace

NamedPipe::~NamedPipe()
{
    close();
}

std::string NamedPipe::uniqueName(const std::string& tag)
{
    static std::atomic<uint64_t> counter{0};
    std::random_device device;
    const uint64_t random = (static_cast<uint64_t>(device()) << 32) ^ device();
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    char suffix[64];
    std::snprintf(suffix, sizeof(suffix), "-%lu-%llu-%016llx", pid, static_cast<unsigned long long>(counter++),
                  static_cast<unsigned long long>(random));
    return "unreal-ng-" + tag + suffix;
}

bool NamedPipe::fail(const std::string& why)
{
    if (!_failed)
        _lastError = why;
    _failed = true;
    return false;
}

// ============================================================================
// Unix implementation (macOS + Linux)
// ============================================================================

#ifndef _WIN32

std::string NamedPipe::readerPath(const std::string& path)
{
    return path;
}

bool NamedPipe::create(const std::string& path)
{
    _path = path;
    std::signal(SIGPIPE, SIG_IGN);  // a reader gone is an EPIPE from write(), not a signal
    ::unlink(path.c_str());
    if (mkfifo(path.c_str(), 0600) != 0)
        return fail("Cannot make the pipe '" + path + "': " + std::strerror(errno));
    return true;
}

bool NamedPipe::tryOpenForWrite()
{
    if (_fd >= 0)
        return true;
    if (_failed)
        return false;
    // Non-blocking: ENXIO while no reader has the FIFO open. The descriptor stays non-blocking: no write waits
    // without looking at the abort flag
    _fd = ::open(_path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (_fd >= 0)
        return true;
    if (errno == ENXIO || errno == EINTR)
        return false;
    return fail("Cannot open pipe '" + _path + "': " + std::strerror(errno));
}

bool NamedPipe::openForWriteTimeout(int timeoutMs, const std::atomic<bool>* abortFlag)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!tryOpenForWrite())
    {
        if (_failed)
            return false;
        if (abortFlag && abortFlag->load(std::memory_order_relaxed))
            return fail("Pipe open aborted");
        if (std::chrono::steady_clock::now() >= deadline)
            return fail("Timed out waiting for ffmpeg to connect to pipe '" + _path + "' (" +
                        std::to_string(timeoutMs) + "ms)");
        usleep(10 * 1000);
    }
    return true;
}

int64_t NamedPipe::tryWrite(const void* data, size_t size)
{
    if (_failed)
        return -1;
    if (_fd < 0)
        return 0;
    const ssize_t written = ::write(_fd, data, std::min(size, kWriteChunk));
    if (written >= 0)
        return written;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        return 0;
    if (errno == EPIPE)
        fail("Broken pipe: reader has exited");
    else
        fail(std::string("write() failed: ") + std::strerror(errno));
    return -1;
}

bool NamedPipe::write(const void* data, size_t size, const std::atomic<bool>* abortFlag)
{
    if (_fd < 0)
        return fail("Pipe not open");
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    while (done < size)
    {
        if (abortFlag && abortFlag->load(std::memory_order_relaxed))
            return fail("Write aborted");
        const int64_t written = tryWrite(bytes + done, size - done);
        if (written < 0)
            return false;
        if (written == 0)
        {
            pollfd pfd{_fd, POLLOUT, 0};
            ::poll(&pfd, 1, 100);  // room, or a slice before the abort flag is looked at again
            continue;
        }
        done += static_cast<size_t>(written);
    }
    return true;
}

bool NamedPipe::flushed()
{
    return true;  // write() puts the bytes in the pipe itself
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

#else  // _WIN32

namespace
{
std::wstring Widen(const std::string& text)
{
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring wide(length > 0 ? static_cast<size_t>(length) : 1, L'\0');
    if (length > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
    wide.resize(wcslen(wide.c_str()));
    return wide;
}

std::string ErrorText(const char* what, DWORD error)
{
    return std::string(what) + " failed, error " + std::to_string(error);
}

/// A security descriptor that lets this user alone open the pipe (the default one lets everyone read it).
/// LocalFree() it; null when the user's SID is not to be had
PSECURITY_DESCRIPTOR UserOnlyDescriptor()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return nullptr;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<unsigned char> buffer(size);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    LPWSTR sid = nullptr;
    if (size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size) &&
        ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid))
    {
        // Protected DACL: the user, all access; nobody else (no inherited entries)
        const std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr))
            descriptor = nullptr;
        LocalFree(sid);
    }
    CloseHandle(token);
    return descriptor;
}

OVERLAPPED* Overlapped(std::vector<unsigned char>& storage)
{
    return reinterpret_cast<OVERLAPPED*>(storage.data());
}
}  // namespace

std::string NamedPipe::readerPath(const std::string& path)
{
    return "\\\\.\\pipe\\" + path;
}

bool NamedPipe::create(const std::string& path)
{
    close();
    _path = path;
    _failed = false;
    _lastError.clear();

    PSECURITY_DESCRIPTOR descriptor = UserOnlyDescriptor();
    if (!descriptor)
        return fail(ErrorText("The pipe's security descriptor", ::GetLastError()));
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};  // not inherited by children
    // One instance, made first by us (a name taken by another process is refused, not shared); remote clients refused.
    // No output buffer of its own: the system's quota; the overlapped writes never block the writer
    HANDLE handle = CreateNamedPipeW(Widen(readerPath(path)).c_str(),
                                     PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                                     PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 0, 0,
                                     &attributes);
    const DWORD createError = ::GetLastError();
    LocalFree(descriptor);
    if (handle == INVALID_HANDLE_VALUE)
        return fail(ErrorText(("CreateNamedPipe '" + path + "'").c_str(), createError));
    _handle = handle;

    _connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    _writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!_connectEvent || !_writeEvent)
    {
        const DWORD error = ::GetLastError();
        close();
        return fail(ErrorText("CreateEvent", error));
    }
    _connectOverlapped.assign(sizeof(OVERLAPPED), 0);
    _writeOverlapped.assign(sizeof(OVERLAPPED), 0);
    Overlapped(_connectOverlapped)->hEvent = _connectEvent;
    Overlapped(_writeOverlapped)->hEvent = _writeEvent;

    // Listen now, before the reader starts: its open succeeds whenever it comes
    if (ConnectNamedPipe(_handle, Overlapped(_connectOverlapped)))
        return connectDone();
    const DWORD error = ::GetLastError();
    if (error == ERROR_IO_PENDING)
    {
        _connecting = true;
        return true;
    }
    if (error == ERROR_PIPE_CONNECTED)
        return connectDone();
    close();
    return fail(ErrorText("ConnectNamedPipe", error));
}

bool NamedPipe::connectDone()
{
    _connecting = false;
    if (_readerProcess != 0)
    {
        ULONG client = 0;
        if (!GetNamedPipeClientProcessId(static_cast<HANDLE>(_handle), &client))
            return fail(ErrorText("GetNamedPipeClientProcessId", ::GetLastError()));
        if (client != _readerProcess)
            return fail("Another process (" + std::to_string(client) + ") opened the pipe '" + _path + "'");
    }
    _connected = true;
    return true;
}

bool NamedPipe::tryOpenForWrite()
{
    if (_connected)
        return true;
    if (_failed)
        return false;
    if (!_handle)
        return fail("Pipe not created");
    if (!_connecting)
        return connectDone();
    DWORD unused = 0;
    if (GetOverlappedResult(static_cast<HANDLE>(_handle), Overlapped(_connectOverlapped), &unused, FALSE))
        return connectDone();
    const DWORD error = ::GetLastError();
    if (error == ERROR_IO_INCOMPLETE)
        return false;
    _connecting = false;
    if (error == ERROR_PIPE_CONNECTED)
        return connectDone();
    return fail(ErrorText("ConnectNamedPipe", error));
}

bool NamedPipe::openForWriteTimeout(int timeoutMs, const std::atomic<bool>* abortFlag)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!tryOpenForWrite())
    {
        if (_failed)
            return false;
        if (abortFlag && abortFlag->load(std::memory_order_relaxed))
            return fail("Pipe open aborted");
        if (std::chrono::steady_clock::now() >= deadline)
            return fail("Timed out waiting for ffmpeg to connect to pipe '" + _path + "' (" +
                        std::to_string(timeoutMs) + "ms)");
        WaitForSingleObject(static_cast<HANDLE>(_connectEvent), 20);
    }
    return true;
}

/// The write in flight: 1 done (or none), 0 still going, -1 failed
int NamedPipe::pollWrite()
{
    if (!_writing)
        return 1;
    DWORD written = 0;
    if (GetOverlappedResult(static_cast<HANDLE>(_handle), Overlapped(_writeOverlapped), &written, FALSE))
    {
        _writing = false;
        return 1;
    }
    const DWORD error = ::GetLastError();
    if (error == ERROR_IO_INCOMPLETE)
        return 0;
    _writing = false;
    fail(error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA ? "Broken pipe: reader has exited"
                                                              : ErrorText("WriteFile", error));
    return -1;
}

void NamedPipe::cancelWrite()
{
    if (!_writing)
        return;
    // The system still reads _pending: cancel, then wait for the cancel (it completes at once)
    CancelIoEx(static_cast<HANDLE>(_handle), Overlapped(_writeOverlapped));
    DWORD unused = 0;
    GetOverlappedResult(static_cast<HANDLE>(_handle), Overlapped(_writeOverlapped), &unused, TRUE);
    _writing = false;
}

int64_t NamedPipe::tryWrite(const void* data, size_t size)
{
    if (_failed)
        return -1;
    if (!_connected || size == 0)
        return 0;
    const int state = pollWrite();
    if (state <= 0)
        return state;
    // Our own copy: the system reads it until the write completes, the caller's bytes may go away
    const size_t take = std::min(size, kWriteChunk);
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    _pending.assign(bytes, bytes + take);
    if (WriteFile(static_cast<HANDLE>(_handle), _pending.data(), static_cast<DWORD>(take), nullptr,
                  Overlapped(_writeOverlapped)))
    {
        _writing = true;  // done already; the next poll collects it
        return static_cast<int64_t>(take);
    }
    const DWORD error = ::GetLastError();
    if (error == ERROR_IO_PENDING)
    {
        _writing = true;
        return static_cast<int64_t>(take);
    }
    fail(error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA ? "Broken pipe: reader has exited"
                                                              : ErrorText("WriteFile", error));
    return -1;
}

bool NamedPipe::write(const void* data, size_t size, const std::atomic<bool>* abortFlag)
{
    if (!_connected)
        return fail("Pipe not open");
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    for (;;)
    {
        if (abortFlag && abortFlag->load(std::memory_order_relaxed))
        {
            cancelWrite();
            return fail("Write aborted");
        }
        const int state = pollWrite();
        if (state < 0)
            return false;
        if (state == 0)
        {
            WaitForSingleObject(static_cast<HANDLE>(_writeEvent), 100);  // done, or a slice before the abort flag
            continue;
        }
        if (done == size)
            return true;  // the last piece is in the pipe: the call is blocking, as the caller expects
        const int64_t taken = tryWrite(bytes + done, size - done);
        if (taken < 0)
            return false;
        done += static_cast<size_t>(taken);
    }
}

bool NamedPipe::flushed()
{
    return pollWrite() != 0;
}

void NamedPipe::close()
{
    if (_handle)
    {
        cancelWrite();
        if (_connecting)
        {
            CancelIoEx(static_cast<HANDLE>(_handle), Overlapped(_connectOverlapped));
            DWORD unused = 0;
            GetOverlappedResult(static_cast<HANDLE>(_handle), Overlapped(_connectOverlapped), &unused, TRUE);
            _connecting = false;
        }
        // No DisconnectNamedPipe: it would drop what the reader has not read yet. Closing leaves it to the
        // reader, then the reader sees the end
        CloseHandle(static_cast<HANDLE>(_handle));
        _handle = nullptr;
    }
    if (_connectEvent)
        CloseHandle(static_cast<HANDLE>(_connectEvent));
    if (_writeEvent)
        CloseHandle(static_cast<HANDLE>(_writeEvent));
    _connectEvent = nullptr;
    _writeEvent = nullptr;
    _connected = false;
}

void NamedPipe::remove(const std::string& path)
{
    (void)path;  // the pipe goes with its last handle
}

bool NamedPipe::isOpen() const
{
    return _connected;
}

#endif  // _WIN32
