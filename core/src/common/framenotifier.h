#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else  // macOS + Linux: FIFO-based cross-process signaling
#include <unistd.h>
#endif

/// Cross-platform frame notification mechanism.
/// Producer signals once per frame (~50Hz); consumer blocks waiting for signal.
///
/// Platform implementations:
/// - macOS/Linux: Named FIFO (cross-process via /tmp/unreal_frame_{id})
/// - Windows:     Named auto-reset event
class FrameNotifier
{
public:
    FrameNotifier() = default;
    ~FrameNotifier() { destroy(); }

    // Non-copyable
    FrameNotifier(const FrameNotifier&) = delete;
    FrameNotifier& operator=(const FrameNotifier&) = delete;

    /// Create the notification object (producer side).
    /// @param name Identifier for cross-process discovery (typically SHM name)
    /// @return true on success
    bool create(const std::string& name);

    /// Signal one waiting consumer. Non-blocking, ~200ns.
    /// Called from MonitoringManager::onFrameEnd().
    void signal();

    /// Destroy the notification object.
    void destroy();

    /// @return true if notification object is valid
    bool isValid() const;

    /// @return The name used to create/attach
    const std::string& name() const { return _name; }

private:
    std::string _name;

#ifdef _WIN32
    HANDLE _event = INVALID_HANDLE_VALUE;
#else  // macOS + Linux
    int _fifoFd = -1;
    std::string _fifoPath;
    bool _fifoCreated = false;
#endif
};

/// Observer-side frame waiter.
/// Attaches to a FrameNotifier created by the emulator and blocks until signaled.
class FrameWaiter
{
public:
    FrameWaiter() = default;
    ~FrameWaiter() { detach(); }

    // Non-copyable
    FrameWaiter(const FrameWaiter&) = delete;
    FrameWaiter& operator=(const FrameWaiter&) = delete;

    /// Attach to an existing notification object.
    /// @param name Same name used by FrameNotifier::create()
    /// @return true on success
    bool attach(const std::string& name);

    /// Block until next signal, or timeout.
    /// @param timeout_ms Maximum wait time in milliseconds
    /// @return true if signaled, false if timed out
    bool wait(uint32_t timeout_ms = 100);

    /// Detach from the notification object.
    void detach();

    /// @return true if attached
    bool isAttached() const;

private:
    std::string _name;

#ifdef _WIN32
    HANDLE _event = INVALID_HANDLE_VALUE;
#else  // macOS + Linux
    int _fifoFd = -1;
    std::string _fifoPath;
#endif
};
