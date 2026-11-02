// net-socket.h - the blocking-with-deadline TCP layer of the WebAPI transports (HTTP and WebSocket): connect, send
// everything, receive what has come, each bounded by a deadline so the UI degrades instead of freezing.
#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#endif

namespace dbg {
namespace net {

#ifdef _WIN32
using Handle = SOCKET;
constexpr Handle kInvalid = INVALID_SOCKET;
#else
using Handle = int;
constexpr Handle kInvalid = -1;
#endif

using Clock = std::chrono::steady_clock;

constexpr int kTimedOut = -2;
constexpr int kFailed = -1;

// A connected non-blocking socket to host:port, or kInvalid with *error set
Handle Connect(const std::string& host, int port, int timeoutMs, std::string* error);
void Close(Handle fd);
// Sends all @p size bytes before @p deadline; false with *error set otherwise
bool SendAll(Handle fd, const char* data, size_t size, const Clock::time_point& deadline, std::string* error);
// The bytes that have come (> 0), 0 at the peer's close, kTimedOut past @p deadline, kFailed on an error
int Receive(Handle fd, char* buf, int cap, const Clock::time_point& deadline);

// The socket closed when it goes out of scope
class Scope {
public:
    explicit Scope(Handle fd) : fd_(fd) {}
    ~Scope() { Close(fd_); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Handle Get() const { return fd_; }

private:
    Handle fd_;
};

}  // namespace net
}  // namespace dbg
