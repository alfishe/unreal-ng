// net-socket.cpp - see net-socket.h.
#include "backend/net-socket.h"

#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace dbg {
namespace net {
namespace {

// Suppress SIGPIPE on send-to-closed-socket (crash risk on POSIX).
#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

int RemainingMs(const Clock::time_point& deadline) {
    const auto now = Clock::now();
    if (now >= deadline) return 0;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    return static_cast<int>(ms.count()) + 1;
}

#ifdef _WIN32

struct WinsockBoot {
    WinsockBoot() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockBoot() { WSACleanup(); }
};

void CloseSocket(Handle fd) { closesocket(fd); }

bool SetNonBlocking(Handle fd, bool enable) {
    u_long mode = enable ? 1UL : 0UL;
    return ioctlsocket(fd, FIONBIO, &mode) == 0;
}

bool WaitWritable(Handle fd, int ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select(static_cast<int>(fd) + 1, nullptr, &set, nullptr, &tv) > 0;
}

bool WaitReadable(Handle fd, int ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select(static_cast<int>(fd) + 1, &set, nullptr, nullptr, &tv) > 0;
}

int SocketErrorNow() { return WSAGetLastError(); }
bool ConnectPending(int err) { return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS; }
bool WouldBlock(int err) { return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS; }

#else

void CloseSocket(Handle fd) { ::close(fd); }

bool SetNonBlocking(Handle fd, bool enable) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    const int wanted = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return ::fcntl(fd, F_SETFL, wanted) == 0;
}

bool WaitWritable(Handle fd, int ms) {
    pollfd p{};
    p.fd = fd;
    p.events = POLLOUT;
    return ::poll(&p, 1, ms) == 1 && (p.revents & POLLOUT) != 0;
}

bool WaitReadable(Handle fd, int ms) {
    pollfd p{};
    p.fd = fd;
    p.events = POLLIN;
    return ::poll(&p, 1, ms) == 1 &&
           (p.revents & (POLLIN | POLLHUP | POLLERR)) != 0;
}

int SocketErrorNow() { return errno; }
bool ConnectPending(int err) { return err == EINPROGRESS; }
bool WouldBlock(int err) { return err == EAGAIN || err == EWOULDBLOCK; }

#endif

int GetSoError(Handle fd) {
#ifdef _WIN32
    int value = 0;
    int len = sizeof value;
#else
    int value = 0;
    socklen_t len = sizeof value;
#endif
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&value), &len) != 0) {
        return -1;
    }
    return value;
}

int RecvSome(Handle fd, char* buf, int cap) {
    const auto n = ::recv(fd, buf, cap, 0);
    return static_cast<int>(n);
}

}  // namespace

Handle Connect(const std::string& host, int port, int timeoutMs, std::string* error) {
#ifdef _WIN32
    static WinsockBoot boot;  // WSAStartup exactly once
    (void)boot;
#endif
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* result = nullptr;
    const std::string portText = std::to_string(port);
    if (::getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0 || result == nullptr) {
        *error = "cannot resolve " + host;
        return kInvalid;
    }
    const Clock::time_point deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    Handle fd = kInvalid;
    for (addrinfo* ai = result; ai != nullptr; ai = ai->ai_next) {
        fd = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == kInvalid) continue;
        if (!SetNonBlocking(fd, true)) {
            CloseSocket(fd);
            fd = kInvalid;
            continue;
        }
#ifdef SO_NOSIGPIPE
        int noSigPipe = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof noSigPipe);
#endif
        if (::connect(fd, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) break;
        if (!ConnectPending(SocketErrorNow())) {
            CloseSocket(fd);
            fd = kInvalid;
            continue;
        }
        const int left = RemainingMs(deadline);
        if (left <= 0 || !WaitWritable(fd, left) || GetSoError(fd) != 0) {
            CloseSocket(fd);
            fd = kInvalid;
            continue;
        }
        break;  // connected
    }
    ::freeaddrinfo(result);
    if (fd == kInvalid) *error = "connect to " + host + ":" + portText + " failed";
    return fd;
}

void Close(Handle fd) {
    if (fd != kInvalid) CloseSocket(fd);
}

bool SendAll(Handle fd, const char* data, size_t size, const Clock::time_point& deadline, std::string* error) {
    size_t sent = 0;
    while (sent < size) {
        const int left = RemainingMs(deadline);
        if (left <= 0 || !WaitWritable(fd, left)) {
            *error = "send timeout";
            return false;
        }
        // Winsock takes an int length: at most 1 GB a call (no std::min: windows.h defines min)
        const size_t chunk = size - sent < (size_t{1} << 30) ? size - sent : (size_t{1} << 30);
        const int n = static_cast<int>(::send(fd, data + sent, static_cast<int>(chunk), kSendFlags));
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && WouldBlock(SocketErrorNow())) continue;
        *error = "send failed";
        return false;
    }
    return true;
}

int Receive(Handle fd, char* buf, int cap, const Clock::time_point& deadline) {
    while (true) {
        const int left = RemainingMs(deadline);
        if (left <= 0 || !WaitReadable(fd, left)) return kTimedOut;
        const int n = RecvSome(fd, buf, cap);
        if (n < 0 && WouldBlock(SocketErrorNow())) continue;
        return n < 0 ? kFailed : n;
    }
}

}  // namespace net
}  // namespace dbg
