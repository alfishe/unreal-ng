#include "framenotifier.h"

// =============================================================================
// macOS + Linux — Named FIFO (cross-process)
// =============================================================================
// A named FIFO in /tmp/ allows the emulator to signal the companion process
// exactly when a new frame is ready. The writer (emulator) creates the FIFO
// and writes 1 byte per frame; the reader (companion) opens for reading
// and wakes via poll() or QSocketNotifier.

#ifndef _WIN32

#include <fcntl.h>
#include <sys/stat.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <poll.h>

/// Derive FIFO path from the notification name (which matches SHM name).
/// Input:  "/unreal_monitor_a1b2c3d4e5f6"
/// Output: "/tmp/unreal_frame_a1b2c3d4e5f6"
static std::string deriveFifoPath(const std::string& name)
{
    const std::string prefix = "/unreal_monitor_";
    std::string instanceId;
    if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0)
        instanceId = name.substr(prefix.size());
    else
        instanceId = name;

    return "/tmp/unreal_frame_" + instanceId;
}

// ─── FrameNotifier (emulator / producer) ────────────────────────────────────

bool FrameNotifier::create(const std::string& name)
{
    destroy();
    _name = name;
    _fifoPath = deriveFifoPath(name);

    // Suppress SIGPIPE globally — safe, idempotent.
    // Needed because write() to a broken pipe sends SIGPIPE by default.
    ::signal(SIGPIPE, SIG_IGN);

    // Remove stale FIFO from a previous crashed instance
    ::unlink(_fifoPath.c_str());

    // Create the FIFO node
    if (mkfifo(_fifoPath.c_str(), 0666) == -1 && errno != EEXIST)
        return false;

    _fifoCreated = true;

    // Don't open the write end yet — lazy open in signal().
    // open(O_WRONLY) blocks until a reader connects; we defer to
    // signal() with O_NONBLOCK so we never block the emulator.

    return true;
}

void FrameNotifier::signal()
{
    if (_fifoPath.empty())
        return;

    // Lazy-open the write end when a reader first connects
    if (_fifoFd == -1)
    {
        _fifoFd = ::open(_fifoPath.c_str(), O_WRONLY | O_NONBLOCK);
        if (_fifoFd == -1)
            return;  // ENXIO = no reader yet, silently skip

#ifdef __APPLE__
        // Per-fd SIGPIPE suppression (belt-and-suspenders with global SIG_IGN)
        int val = 1;
        fcntl(_fifoFd, F_SETNOSIGPIPE, &val);
#endif
    }

    char byte = 0x01;
    ssize_t ret = ::write(_fifoFd, &byte, 1);
    if (ret == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            // FIFO buffer full (reader slow) — lossy, OK
            return;
        }
        if (errno == EPIPE)
        {
            // Reader disconnected — close and will re-open on next signal
            ::close(_fifoFd);
            _fifoFd = -1;
        }
    }
}

void FrameNotifier::destroy()
{
    if (_fifoFd != -1)
    {
        ::close(_fifoFd);
        _fifoFd = -1;
    }

    if (_fifoCreated && !_fifoPath.empty())
    {
        ::unlink(_fifoPath.c_str());
        _fifoCreated = false;
    }

    _fifoPath.clear();
    _name.clear();
}

bool FrameNotifier::isValid() const
{
    return !_fifoPath.empty() && _fifoCreated;
}

// ─── FrameWaiter (observer / consumer) ──────────────────────────────────────

bool FrameWaiter::attach(const std::string& name)
{
    detach();
    _name = name;
    _fifoPath = deriveFifoPath(name);

    _fifoFd = ::open(_fifoPath.c_str(), O_RDONLY | O_NONBLOCK);
    return _fifoFd != -1;
}

bool FrameWaiter::wait(uint32_t timeout_ms)
{
    if (_fifoFd == -1)
    {
        // No FIFO attached — polling fallback
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = 1000000;  // 1ms
        nanosleep(&ts, nullptr);
        return false;
    }

    struct pollfd pfd;
    pfd.fd = _fifoFd;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, static_cast<int>(timeout_ms));
    if (ret > 0 && (pfd.revents & POLLIN))
    {
        // Drain all pending bytes (coalesce multiple frame signals)
        char buf[64];
        while (::read(_fifoFd, buf, sizeof(buf)) > 0) {}
        return true;
    }
    return false;
}

void FrameWaiter::detach()
{
    if (_fifoFd != -1)
    {
        ::close(_fifoFd);
        _fifoFd = -1;
    }
    _fifoPath.clear();
    _name.clear();
}

bool FrameWaiter::isAttached() const
{
    return _fifoFd != -1;
}

// =============================================================================
// Windows — Named auto-reset event (already cross-process)
// =============================================================================

#else  // _WIN32

bool FrameNotifier::create(const std::string& name)
{
    destroy();
    _name = name;

    std::string eventName = "Local\\unreal_frame_" + name;
    _event = CreateEventA(NULL, FALSE, FALSE, eventName.c_str());
    return _event != NULL && _event != INVALID_HANDLE_VALUE;
}

void FrameNotifier::signal()
{
    if (_event != INVALID_HANDLE_VALUE)
    {
        SetEvent(_event);
    }
}

void FrameNotifier::destroy()
{
    if (_event != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_event);
        _event = INVALID_HANDLE_VALUE;
    }
    _name.clear();
}

bool FrameNotifier::isValid() const
{
    return _event != INVALID_HANDLE_VALUE;
}

bool FrameWaiter::attach(const std::string& name)
{
    detach();
    _name = name;

    std::string eventName = "Local\\unreal_frame_" + name;
    _event = OpenEventA(SYNCHRONIZE, FALSE, eventName.c_str());
    return _event != NULL && _event != INVALID_HANDLE_VALUE;
}

bool FrameWaiter::wait(uint32_t timeout_ms)
{
    if (_event == INVALID_HANDLE_VALUE)
        return false;

    DWORD result = WaitForSingleObject(_event, timeout_ms);
    return result == WAIT_OBJECT_0;
}

void FrameWaiter::detach()
{
    if (_event != INVALID_HANDLE_VALUE)
    {
        CloseHandle(_event);
        _event = INVALID_HANDLE_VALUE;
    }
    _name.clear();
}

bool FrameWaiter::isAttached() const
{
    return _event != INVALID_HANDLE_VALUE;
}

#endif
