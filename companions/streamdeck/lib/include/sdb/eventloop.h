// One thread, non-blocking sockets, timers, and work posted from other threads: the daemon's network loop
// (docs/inprogress/2026-10-19-streamdeck-integration/architecture.md §9, NFR-8). poll() on POSIX, WSAPoll on Windows.
//
// Everything but Post() and Stop() is called on the loop's thread (or before Run() starts).
#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sdb::net
{

#ifdef _WIN32
using SocketHandle = std::uintptr_t;  // SOCKET
inline constexpr SocketHandle kInvalidSocket = ~static_cast<SocketHandle>(0);
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
#endif

// Sockets. All of them are created non-blocking; none raises SIGPIPE.
void CloseSocket(SocketHandle s);
// Starts a connect to host:port and returns at once; the socket becomes writable when the connect ends, then
// SocketError() tells how. Name resolution itself blocks (the daemon talks to 127.0.0.1 / localhost only).
SocketHandle ConnectStart(const std::string& host, int port, std::string* error);
int SocketError(SocketHandle s);  // SO_ERROR: 0 = connected
// A listening socket on host:port (0 = any free port); *boundPort gets the port.
SocketHandle Listen(const std::string& host, int port, int* boundPort, std::string* error);
SocketHandle Accept(SocketHandle listener);  // kInvalidSocket when none is pending
// Bytes moved (> 0), 0 when it would block, -1 on error or (Recv) the peer's close.
long Send(SocketHandle s, const void* data, std::size_t size);
long Recv(SocketHandle s, void* data, std::size_t size);
std::string LastErrorText();

class EventLoop
{
public:
    using Clock = std::chrono::steady_clock;
    using TimerId = std::uint64_t;
    // readable, writable, error (hang-up included)
    using IoHandler = std::function<void(bool, bool, bool)>;

    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void Run();  // until Stop()
    // One wait and dispatch, at most maxWaitMs; false once stopped.
    bool RunOnce(int maxWaitMs);
    void Stop();  // any thread
    [[nodiscard]] bool InLoopThread() const;

    // Any thread: fn runs on the loop thread, in order, soon.
    void Post(std::function<void()> fn);

    TimerId After(std::chrono::milliseconds delay, std::function<void()> fn);
    TimerId Every(std::chrono::milliseconds period, std::function<void()> fn);
    void Cancel(TimerId id);

    void Watch(SocketHandle s, bool read, bool write, IoHandler handler);
    void Modify(SocketHandle s, bool read, bool write);
    void Unwatch(SocketHandle s);
    [[nodiscard]] std::size_t WatchCount() const
    {
        return _watches.size();
    }

private:
    struct WatchEntry
    {
        bool read = false;
        bool write = false;
        IoHandler handler;
    };
    struct Timer
    {
        std::chrono::milliseconds period{0};  // 0 = once
        std::function<void()> fn;
    };

    void RunTimers();
    void RunPosted();
    void DrainWake();
    int WaitMs(int maxWaitMs) const;

    std::map<SocketHandle, WatchEntry> _watches;
    std::multimap<Clock::time_point, TimerId> _due;
    std::map<TimerId, Timer> _timers;
    TimerId _nextTimer = 1;

    std::mutex _postMutex;
    std::vector<std::function<void()>> _posted;
    bool _wakePending = false;
    SocketHandle _wakeRead = kInvalidSocket;
    SocketHandle _wakeWrite = kInvalidSocket;

    bool _stopped = false;
    std::thread::id _thread;
};

}  // namespace sdb::net
