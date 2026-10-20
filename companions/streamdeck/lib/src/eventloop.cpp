#include "sdb/eventloop.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#endif

#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace sdb::net
{
namespace
{

#ifdef _WIN32
using PollFd = WSAPOLLFD;
int PollFds(PollFd* fds, std::size_t n, int ms)
{
    return WSAPoll(fds, static_cast<ULONG>(n), ms);
}
bool WouldBlock()
{
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
}
void StartupOnce()
{
    static const bool started = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!started)
        throw std::runtime_error("WSAStartup failed");
}
#else
using PollFd = pollfd;
int PollFds(PollFd* fds, std::size_t n, int ms)
{
    return ::poll(fds, static_cast<nfds_t>(n), ms);
}
bool WouldBlock()
{
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
}
void StartupOnce() {}
#endif

void Prepare(SocketHandle s, bool tcp)
{
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(static_cast<SOCKET>(s), FIONBIO, &on);
#else
    ::fcntl(s, F_SETFL, ::fcntl(s, F_GETFL, 0) | O_NONBLOCK);
    ::fcntl(s, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
#endif
    if (tcp)
    {
        int one = 1;
        ::setsockopt(static_cast<decltype(socket(0, 0, 0))>(s), IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<const char*>(&one), sizeof one);
    }
}

SocketHandle MakeSocket(int family, int type, int protocol)
{
    StartupOnce();
    const auto s = ::socket(family, type, protocol);
#ifdef _WIN32
    return s == INVALID_SOCKET ? kInvalidSocket : static_cast<SocketHandle>(s);
#else
    return s < 0 ? kInvalidSocket : s;
#endif
}

// The wake pair: a byte written to one end makes poll() return on the other.
void MakeWakePair(SocketHandle* readEnd, SocketHandle* writeEnd)
{
#ifdef _WIN32
    int port = 0;
    std::string error;
    const SocketHandle listener = Listen("127.0.0.1", 0, &port, &error);
    if (listener == kInvalidSocket)
        throw std::runtime_error("event loop wake pair: " + error);
    const SocketHandle w = ConnectStart("127.0.0.1", port, &error);
    SocketHandle r = kInvalidSocket;
    for (int i = 0; i < 1000 && r == kInvalidSocket; ++i)
    {
        WSAPOLLFD p{static_cast<SOCKET>(listener), POLLRDNORM, 0};
        WSAPoll(&p, 1, 10);
        r = Accept(listener);
    }
    CloseSocket(listener);
    if (w == kInvalidSocket || r == kInvalidSocket)
        throw std::runtime_error("event loop wake pair: no loopback connection");
    *readEnd = r;
    *writeEnd = w;
#else
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        throw std::runtime_error(std::string("event loop wake pair: ") + std::strerror(errno));
    Prepare(fds[0], false);
    Prepare(fds[1], false);
    *readEnd = fds[0];
    *writeEnd = fds[1];
#endif
}

}  // namespace

void CloseSocket(SocketHandle s)
{
    if (s == kInvalidSocket)
        return;
#ifdef _WIN32
    ::closesocket(static_cast<SOCKET>(s));
#else
    ::close(s);
#endif
}

SocketHandle ConnectStart(const std::string& host, int port, std::string* error)
{
    StartupOnce();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &res) != 0 || !res)
    {
        if (error)
            *error = "cannot resolve " + host;
        return kInvalidSocket;
    }
    SocketHandle s = kInvalidSocket;
    for (addrinfo* p = res; p && s == kInvalidSocket; p = p->ai_next)
    {
        s = MakeSocket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == kInvalidSocket)
            continue;
        Prepare(s, true);
#ifdef _WIN32
        const int rc = ::connect(static_cast<SOCKET>(s), p->ai_addr, static_cast<int>(p->ai_addrlen));
#else
        const int rc = ::connect(s, p->ai_addr, p->ai_addrlen);
#endif
        if (rc != 0 && !WouldBlock())
        {
            if (error)
                *error = "connect " + host + ":" + service + ": " + LastErrorText();
            CloseSocket(s);
            s = kInvalidSocket;
        }
    }
    ::freeaddrinfo(res);
    return s;
}

int SocketError(SocketHandle s)
{
    int value = 0;
#ifdef _WIN32
    int len = sizeof value;
    ::getsockopt(static_cast<SOCKET>(s), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&value), &len);
#else
    socklen_t len = sizeof value;
    ::getsockopt(s, SOL_SOCKET, SO_ERROR, &value, &len);
#endif
    return value;
}

SocketHandle Listen(const std::string& host, int port, int* boundPort, std::string* error)
{
    StartupOnce();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* res = nullptr;
    const std::string service = std::to_string(port);
    if (::getaddrinfo(host.c_str(), service.c_str(), &hints, &res) != 0 || !res)
    {
        if (error)
            *error = "cannot resolve " + host;
        return kInvalidSocket;
    }
    SocketHandle s = MakeSocket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s != kInvalidSocket)
    {
        int one = 1;
#ifdef _WIN32
        const auto native = static_cast<SOCKET>(s);
        const bool ok =
            ::bind(native, res->ai_addr, static_cast<int>(res->ai_addrlen)) == 0 && ::listen(native, SOMAXCONN) == 0;
        (void)one;
#else
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        const bool ok = ::bind(s, res->ai_addr, res->ai_addrlen) == 0 && ::listen(s, SOMAXCONN) == 0;
#endif
        if (!ok)
        {
            if (error)
                *error = "listen " + host + ":" + service + ": " + LastErrorText();
            CloseSocket(s);
            s = kInvalidSocket;
        }
    }
    ::freeaddrinfo(res);
    if (s == kInvalidSocket)
        return s;
    Prepare(s, false);
    sockaddr_storage addr{};
#ifdef _WIN32
    int len = sizeof addr;
    ::getsockname(static_cast<SOCKET>(s), reinterpret_cast<sockaddr*>(&addr), &len);
#else
    socklen_t len = sizeof addr;
    ::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
#endif
    if (boundPort)
        *boundPort = addr.ss_family == AF_INET6 ? ntohs(reinterpret_cast<sockaddr_in6*>(&addr)->sin6_port)
                                                : ntohs(reinterpret_cast<sockaddr_in*>(&addr)->sin_port);
    return s;
}

SocketHandle Accept(SocketHandle listener)
{
#ifdef _WIN32
    const SOCKET c = ::accept(static_cast<SOCKET>(listener), nullptr, nullptr);
    if (c == INVALID_SOCKET)
        return kInvalidSocket;
    const auto s = static_cast<SocketHandle>(c);
#else
    const int s = ::accept(listener, nullptr, nullptr);
    if (s < 0)
        return kInvalidSocket;
#endif
    Prepare(s, true);
    return s;
}

long Send(SocketHandle s, const void* data, std::size_t size)
{
#ifdef _WIN32
    const int n = ::send(static_cast<SOCKET>(s), static_cast<const char*>(data),
                         static_cast<int>(std::min<std::size_t>(size, 1u << 30)), 0);
#elif defined(MSG_NOSIGNAL)
    const ssize_t n = ::send(s, data, size, MSG_NOSIGNAL);
#else
    const ssize_t n = ::send(s, data, size, 0);
#endif
    if (n >= 0)
        return static_cast<long>(n);
    return WouldBlock() ? 0 : -1;
}

long Recv(SocketHandle s, void* data, std::size_t size)
{
#ifdef _WIN32
    const int n = ::recv(static_cast<SOCKET>(s), static_cast<char*>(data),
                         static_cast<int>(std::min<std::size_t>(size, 1u << 30)), 0);
#else
    const ssize_t n = ::recv(s, data, size, 0);
#endif
    if (n > 0)
        return static_cast<long>(n);
    if (n == 0)
        return -1;  // the peer closed
    return WouldBlock() ? 0 : -1;
}

void SetSendBuffer(SocketHandle s, int bytes)
{
#ifdef _WIN32
    ::setsockopt(static_cast<SOCKET>(s), SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bytes), sizeof bytes);
#else
    ::setsockopt(s, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof bytes);
#endif
}

void SetReceiveBuffer(SocketHandle s, int bytes)
{
#ifdef _WIN32
    ::setsockopt(static_cast<SOCKET>(s), SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bytes), sizeof bytes);
#else
    ::setsockopt(s, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof bytes);
#endif
}

int WaitSocket(SocketHandle s, bool read, bool write, int timeoutMs)
{
    PollFd p{};
#ifdef _WIN32
    p.fd = static_cast<SOCKET>(s);
    p.events = static_cast<SHORT>((read ? POLLRDNORM : 0) | (write ? POLLWRNORM : 0));
#else
    p.fd = s;
    p.events = static_cast<short>((read ? POLLIN : 0) | (write ? POLLOUT : 0));
#endif
    const int n = PollFds(&p, 1, timeoutMs);
    if (n < 0)
        return -1;
    if (n == 0)
        return 0;
    // A hang-up with data still to read is "ready": the read then sees the data, and the end after it.
    return (p.revents & (POLLERR | POLLNVAL)) && !(p.revents & (read ? POLLIN : 0)) ? -1 : 1;
}

SocketHandle Connect(const std::string& host, int port, int timeoutMs, std::string* error)
{
    SocketHandle s = ConnectStart(host, port, error);
    if (s == kInvalidSocket)
        return s;
    const int ready = WaitSocket(s, false, true, timeoutMs);
    if (ready <= 0 || SocketError(s) != 0)
    {
        if (error)
            *error = "connect " + host + ":" + std::to_string(port) + (ready == 0 ? ": timed out" : ": refused");
        CloseSocket(s);
        return kInvalidSocket;
    }
    return s;
}

bool SendAll(SocketHandle s, const void* data, std::size_t size, int timeoutMs)
{
    const auto* p = static_cast<const char*>(data);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (size > 0)
    {
        const long n = Send(s, p, size);
        if (n < 0)
            return false;
        if (n == 0)
        {
            const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (left.count() <= 0 || WaitSocket(s, false, true, static_cast<int>(left.count())) <= 0)
                return false;
            continue;
        }
        p += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

std::string LastErrorText()
{
#ifdef _WIN32
    return "WSA error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

EventLoop::EventLoop() : _thread(std::this_thread::get_id())
{
    MakeWakePair(&_wakeRead, &_wakeWrite);
}

EventLoop::~EventLoop()
{
    CloseSocket(_wakeRead);
    CloseSocket(_wakeWrite);
}

void EventLoop::Run()
{
    while (RunOnce(-1))
    {
    }
}

void EventLoop::Stop()
{
    Post([this] { _stopped = true; });
}

bool EventLoop::InLoopThread() const
{
    return std::this_thread::get_id() == _thread;
}

void EventLoop::Post(std::function<void()> fn)
{
    bool wake = false;
    {
        std::lock_guard lock(_postMutex);
        _posted.push_back(std::move(fn));
        wake = !_wakePending;
        _wakePending = true;
    }
    if (wake)
    {
        const char byte = 1;
        (void)Send(_wakeWrite, &byte, 1);
    }
}

EventLoop::TimerId EventLoop::After(std::chrono::milliseconds delay, std::function<void()> fn)
{
    const TimerId id = _nextTimer++;
    _timers[id] = Timer{std::chrono::milliseconds(0), std::move(fn)};
    _due.emplace(Clock::now() + delay, id);
    return id;
}

EventLoop::TimerId EventLoop::Every(std::chrono::milliseconds period, std::function<void()> fn)
{
    const TimerId id = _nextTimer++;
    period = std::max(period, std::chrono::milliseconds(1));
    _timers[id] = Timer{period, std::move(fn)};
    _due.emplace(Clock::now() + period, id);
    return id;
}

void EventLoop::Cancel(TimerId id)
{
    _timers.erase(id);  // its entry in _due is skipped when it comes up
}

void EventLoop::Watch(SocketHandle s, bool read, bool write, IoHandler handler)
{
    _watches[s] = WatchEntry{read, write, std::move(handler)};
}

void EventLoop::Modify(SocketHandle s, bool read, bool write)
{
    if (auto it = _watches.find(s); it != _watches.end())
    {
        it->second.read = read;
        it->second.write = write;
    }
}

void EventLoop::Unwatch(SocketHandle s)
{
    _watches.erase(s);
}

int EventLoop::WaitMs(int maxWaitMs) const
{
    int wait = maxWaitMs;
    if (!_due.empty())
    {
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(_due.begin()->first - Clock::now()).count();
        const int untilTimer = static_cast<int>(std::clamp<long long>(left, 0, 60'000));
        wait = wait < 0 ? untilTimer : std::min(wait, untilTimer);
    }
    return wait;
}

bool EventLoop::RunOnce(int maxWaitMs)
{
    _thread = std::this_thread::get_id();
    if (_stopped)
        return false;

    std::vector<PollFd> fds;
    std::vector<SocketHandle> handles;
    fds.reserve(_watches.size() + 1);
    handles.reserve(_watches.size() + 1);
    auto add = [&](SocketHandle s, bool read, bool write) {
        PollFd p{};
#ifdef _WIN32
        p.fd = static_cast<SOCKET>(s);
        p.events = static_cast<SHORT>((read ? POLLRDNORM : 0) | (write ? POLLWRNORM : 0));
#else
        p.fd = s;
        p.events = static_cast<short>((read ? POLLIN : 0) | (write ? POLLOUT : 0));
#endif
        fds.push_back(p);
        handles.push_back(s);
    };
    add(_wakeRead, true, false);
    for (const auto& [s, w] : _watches)
        add(s, w.read, w.write);

    const int n = PollFds(fds.data(), fds.size(), WaitMs(maxWaitMs));
    if (n > 0)
    {
        if (fds[0].revents)
            DrainWake();
        for (std::size_t i = 1; i < fds.size(); ++i)
        {
            const auto revents = fds[i].revents;
            if (!revents)
                continue;
            // A handler may unwatch or close any socket, its own included: look each one up again.
            const auto it = _watches.find(handles[i]);
            if (it == _watches.end())
                continue;
            const IoHandler handler = it->second.handler;
#ifdef _WIN32
            const bool readable = (revents & (POLLRDNORM | POLLRDBAND)) != 0;
            const bool writable = (revents & POLLWRNORM) != 0;
#else
            const bool readable = (revents & POLLIN) != 0;
            const bool writable = (revents & POLLOUT) != 0;
#endif
            const bool error = (revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            handler(readable, writable, error);
        }
    }
    RunPosted();
    RunTimers();
    return !_stopped;
}

void EventLoop::DrainWake()
{
    char buf[64];
    while (Recv(_wakeRead, buf, sizeof buf) > 0)
    {
    }
}

void EventLoop::RunPosted()
{
    std::vector<std::function<void()>> work;
    {
        std::lock_guard lock(_postMutex);
        work.swap(_posted);
        _wakePending = false;
    }
    for (auto& fn : work)
        fn();
}

void EventLoop::RunTimers()
{
    const auto now = Clock::now();
    while (!_due.empty() && _due.begin()->first <= now)
    {
        const auto [when, id] = *_due.begin();
        _due.erase(_due.begin());
        auto it = _timers.find(id);
        if (it == _timers.end())
            continue;  // cancelled
        std::function<void()> fn = it->second.fn;
        if (it->second.period.count() > 0)
            _due.emplace(std::max(when + it->second.period, now), id);
        else
            _timers.erase(it);
        fn();
    }
}

}  // namespace sdb::net
