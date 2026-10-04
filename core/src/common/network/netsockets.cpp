#include "common/network/netsockets.h"

#include <cstring>
#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <chrono>

const char* NetStatusText(NetEventStatus status)
{
    switch (status)
    {
        case NetEventStatus::Ok: return "ok";
        case NetEventStatus::Refused: return "refused";
        case NetEventStatus::Timeout: return "timeout";
        case NetEventStatus::Unreachable: return "unreachable";
        case NetEventStatus::AddressInUse: return "address-in-use";
        case NetEventStatus::Denied: return "denied";
        case NetEventStatus::Error: return "error";
        case NetEventStatus::TlsFailed: return "tls-failed";
    }
    return "?";
}

std::string NetIpToString(uint32_t addr)
{
    return std::to_string((addr >> 24) & 0xFF) + "." + std::to_string((addr >> 16) & 0xFF) + "." +
           std::to_string((addr >> 8) & 0xFF) + "." + std::to_string(addr & 0xFF);
}

bool NetIpFromString(const std::string& text, uint32_t& addr)
{
    uint32_t parts[4] = {};
    int part = 0;
    bool digit = false;
    for (char c : text)
    {
        if (c >= '0' && c <= '9')
        {
            parts[part] = parts[part] * 10 + static_cast<uint32_t>(c - '0');
            if (parts[part] > 255)
                return false;
            digit = true;
        }
        else if (c == '.' && digit && part < 3)
        {
            ++part;
            digit = false;
        }
        else
        {
            return false;
        }
    }
    if (part != 3 || !digit)
        return false;
    addr = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return true;
}

namespace netsock
{

#ifdef _WIN32
using OsSocket = SOCKET;
static inline OsSocket Os(Handle h) { return static_cast<OsSocket>(h); }
static inline bool IsBad(OsSocket s) { return s == INVALID_SOCKET; }
static inline int LastError() { return WSAGetLastError(); }
static inline bool ErrWouldBlock(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY; }
#else
using OsSocket = int;
static inline OsSocket Os(Handle h) { return static_cast<OsSocket>(h); }
static inline bool IsBad(OsSocket s) { return s < 0; }
static inline int LastError() { return errno; }
static inline bool ErrWouldBlock(int e) { return e == EWOULDBLOCK || e == EAGAIN || e == EINPROGRESS || e == EALREADY; }
#endif

static Result MapError(int e)
{
#ifdef _WIN32
    switch (e)
    {
        case WSAEWOULDBLOCK:
        case WSAEINPROGRESS:
        case WSAEALREADY: return Result::WouldBlock;
        case WSAECONNREFUSED: return Result::Refused;
        case WSAENETUNREACH:
        case WSAEHOSTUNREACH: return Result::Unreachable;
        case WSAETIMEDOUT: return Result::Timeout;
        case WSAEADDRINUSE: return Result::AddressInUse;
        case WSAECONNRESET:
        case WSAECONNABORTED: return Result::Reset;
        default: return Result::Error;
    }
#else
    if (ErrWouldBlock(e))
        return Result::WouldBlock;
    switch (e)
    {
        case ECONNREFUSED: return Result::Refused;
        case ENETUNREACH:
        case EHOSTUNREACH: return Result::Unreachable;
        case ETIMEDOUT: return Result::Timeout;
        case EADDRINUSE: return Result::AddressInUse;
        case ECONNRESET:
        case EPIPE:
        case ECONNABORTED: return Result::Reset;
        default: return Result::Error;
    }
#endif
}

static sockaddr_in ToSockaddr(uint32_t addr, uint16_t port)
{
    sockaddr_in sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(addr);
    sa.sin_port = htons(port);
    return sa;
}

static void SetNonBlocking(OsSocket s)
{
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
#else
    const int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#if defined(__APPLE__)
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));  // a dead peer must not kill the emulator
#endif
#endif
}

bool Startup()
{
#ifdef _WIN32
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WSADATA data;
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return ok;
#else
    return true;
#endif
}

static Handle Open(int type, int proto)
{
    if (!Startup())
        return kInvalid;
    OsSocket s = socket(AF_INET, type, proto);
    if (IsBad(s))
        return kInvalid;
    SetNonBlocking(s);
    return static_cast<Handle>(s);
}

Handle OpenTcp()
{
    Handle h = Open(SOCK_STREAM, IPPROTO_TCP);
    if (h != kInvalid)
    {
        int one = 1;
        setsockopt(Os(h), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    }
    return h;
}

Handle OpenUdp()
{
    return Open(SOCK_DGRAM, IPPROTO_UDP);
}

void Close(Handle h)
{
    if (h == kInvalid)
        return;
#ifdef _WIN32
    closesocket(Os(h));
#else
    close(Os(h));
#endif
}

Result Connect(Handle h, const NetEndpoint& to)
{
    sockaddr_in sa = ToSockaddr(to.addr, to.port);
    if (connect(Os(h), reinterpret_cast<const sockaddr*>(&sa), sizeof(sa)) == 0)
        return Result::Ok;
    return MapError(LastError());
}

Result ConnectResult(Handle h)
{
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(Os(h), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0)
        return Result::Error;
    return err == 0 ? Result::Ok : MapError(err);
}

Result Bind(Handle h, uint32_t addr, uint16_t port, bool reuse)
{
    if (reuse)
    {
        int one = 1;
        setsockopt(Os(h), SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    }
    sockaddr_in sa = ToSockaddr(addr, port);
    if (bind(Os(h), reinterpret_cast<const sockaddr*>(&sa), sizeof(sa)) == 0)
        return Result::Ok;
    return MapError(LastError());
}

Result Listen(Handle h, int backlog)
{
    return listen(Os(h), backlog) == 0 ? Result::Ok : MapError(LastError());
}

Handle Accept(Handle h, NetEndpoint& peer)
{
    sockaddr_in sa;
    socklen_t len = sizeof(sa);
    OsSocket s = accept(Os(h), reinterpret_cast<sockaddr*>(&sa), &len);
    if (IsBad(s))
        return kInvalid;
    SetNonBlocking(s);
    peer.addr = ntohl(sa.sin_addr.s_addr);
    peer.port = ntohs(sa.sin_port);
    return static_cast<Handle>(s);
}

uint16_t LocalPort(Handle h)
{
    sockaddr_in sa;
    socklen_t len = sizeof(sa);
    if (getsockname(Os(h), reinterpret_cast<sockaddr*>(&sa), &len) != 0)
        return 0;
    return ntohs(sa.sin_port);
}

uint32_t LocalAddress(Handle h)
{
    sockaddr_in sa;
    socklen_t len = sizeof(sa);
    if (getsockname(Os(h), reinterpret_cast<sockaddr*>(&sa), &len) != 0)
        return 0;
    return ntohl(sa.sin_addr.s_addr);
}

static int SendFlags()
{
#if defined(MSG_NOSIGNAL)
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

Result Send(Handle h, const uint8_t* data, size_t length, size_t& done)
{
    done = 0;
    const auto n = send(Os(h), reinterpret_cast<const char*>(data), static_cast<int>(length), SendFlags());
    if (n >= 0)
    {
        done = static_cast<size_t>(n);
        return Result::Ok;
    }
    return MapError(LastError());
}

Result Recv(Handle h, uint8_t* data, size_t capacity, size_t& done)
{
    done = 0;
    const auto n = recv(Os(h), reinterpret_cast<char*>(data), static_cast<int>(capacity), 0);
    if (n > 0)
    {
        done = static_cast<size_t>(n);
        return Result::Ok;
    }
    if (n == 0)
        return Result::Closed;
    return MapError(LastError());
}

Result SendTo(Handle h, const NetEndpoint& to, const uint8_t* data, size_t length)
{
    sockaddr_in sa = ToSockaddr(to.addr, to.port);
    const auto n = sendto(Os(h), reinterpret_cast<const char*>(data), static_cast<int>(length), SendFlags(),
                          reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
    return n >= 0 ? Result::Ok : MapError(LastError());
}

Result RecvFrom(Handle h, uint8_t* data, size_t capacity, size_t& done, NetEndpoint& from)
{
    done = 0;
    sockaddr_in sa;
    socklen_t len = sizeof(sa);
    const auto n = recvfrom(Os(h), reinterpret_cast<char*>(data), static_cast<int>(capacity), 0,
                            reinterpret_cast<sockaddr*>(&sa), &len);
    if (n >= 0)
    {
        done = static_cast<size_t>(n);
        from.addr = ntohl(sa.sin_addr.s_addr);
        from.port = ntohs(sa.sin_port);
        return Result::Ok;
    }
    return MapError(LastError());
}

void ShutdownWrite(Handle h)
{
#ifdef _WIN32
    shutdown(Os(h), SD_SEND);
#else
    shutdown(Os(h), SHUT_WR);
#endif
}

int Poll(std::vector<PollItem>& items, int timeoutMs)
{
#ifdef _WIN32
    std::vector<WSAPOLLFD> fds(items.size());
#else
    std::vector<pollfd> fds(items.size());
#endif
    for (size_t i = 0; i < items.size(); ++i)
    {
        fds[i].fd = Os(items[i].handle);
        fds[i].events = static_cast<short>((items[i].wantRead ? POLLIN : 0) | (items[i].wantWrite ? POLLOUT : 0));
        fds[i].revents = 0;
    }
    if (items.empty())
        return 0;
#ifdef _WIN32
    const int n = WSAPoll(fds.data(), static_cast<ULONG>(fds.size()), timeoutMs);
#else
    const int n = poll(fds.data(), static_cast<nfds_t>(fds.size()), timeoutMs);
#endif
    for (size_t i = 0; i < items.size(); ++i)
    {
        items[i].readable = (fds[i].revents & POLLIN) != 0;
        items[i].writable = (fds[i].revents & POLLOUT) != 0;
        items[i].failed = (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
    }
    return n;
}

bool ResolveIpv4(const std::string& name, std::vector<uint32_t>& out)
{
    out.clear();
    if (!Startup())
        return false;
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    if (getaddrinfo(name.c_str(), nullptr, &hints, &list) != 0 || !list)
        return false;
    for (addrinfo* ai = list; ai; ai = ai->ai_next)
    {
        if (ai->ai_family != AF_INET || !ai->ai_addr)
            continue;
        const auto* sa = reinterpret_cast<const sockaddr_in*>(ai->ai_addr);
        const uint32_t addr = ntohl(sa->sin_addr.s_addr);
        bool seen = false;
        for (uint32_t a : out)
            seen = seen || a == addr;
        if (!seen)
            out.push_back(addr);
    }
    freeaddrinfo(list);
    return !out.empty();
}

static uint16_t IcmpChecksum(const uint8_t* data, size_t length)
{
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < length; i += 2)
        sum += static_cast<uint32_t>((data[i] << 8) | data[i + 1]);
    if (length & 1)
        sum += static_cast<uint32_t>(data[length - 1] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

/// An echo reply with the request's identifier and sequence, checksum redone
static void MakeReply(const uint8_t* request, const uint8_t* data, size_t dataLength, std::vector<uint8_t>& reply)
{
    reply.assign(8 + dataLength, 0);
    reply[0] = 0;                                  // echo reply
    reply[1] = 0;
    reply[4] = request[4];                         // identifier
    reply[5] = request[5];
    reply[6] = request[6];                         // sequence
    reply[7] = request[7];
    if (dataLength)
        std::memcpy(reply.data() + 8, data, dataLength);
    const uint16_t sum = IcmpChecksum(reply.data(), reply.size());
    reply[2] = static_cast<uint8_t>(sum >> 8);
    reply[3] = static_cast<uint8_t>(sum & 0xFF);
}

bool Ping(const NetEndpoint& to, const uint8_t* request, size_t length, uint32_t timeoutMs, std::vector<uint8_t>& reply)
{
    reply.clear();
    if (!request || length < 8 || request[0] != 8 || !Startup())
        return false;

#ifdef _WIN32
    HANDLE icmp = IcmpCreateFile();
    if (icmp == INVALID_HANDLE_VALUE)
        return false;
    const size_t dataLength = length - 8;
    std::vector<uint8_t> buffer(sizeof(ICMP_ECHO_REPLY) + dataLength + 8 + 64);
    const DWORD count = IcmpSendEcho(icmp, htonl(to.addr), const_cast<uint8_t*>(request + 8),
                                     static_cast<WORD>(dataLength), nullptr, buffer.data(),
                                     static_cast<DWORD>(buffer.size()), timeoutMs);
    bool ok = false;
    if (count > 0)
    {
        const auto* echo = reinterpret_cast<const ICMP_ECHO_REPLY*>(buffer.data());
        if (echo->Status == IP_SUCCESS)
        {
            MakeReply(request, static_cast<const uint8_t*>(echo->Data), echo->DataSize, reply);
            ok = true;
        }
    }
    IcmpCloseHandle(icmp);
    return ok;
#else
    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (s < 0)
        return false;
    sockaddr_in sa = ToSockaddr(to.addr, 0);
    std::vector<uint8_t> packet(request, request + length);
    packet[2] = packet[3] = 0;
    const uint16_t sum = IcmpChecksum(packet.data(), packet.size());
    packet[2] = static_cast<uint8_t>(sum >> 8);
    packet[3] = static_cast<uint8_t>(sum & 0xFF);
    if (sendto(s, packet.data(), packet.size(), 0, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa)) < 0)
    {
        close(s);
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    uint8_t buffer[2048];
    bool ok = false;
    while (!ok)
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0)
            break;
        pollfd fd{s, POLLIN, 0};
        if (poll(&fd, 1, static_cast<int>(left.count())) <= 0)
            break;
        sockaddr_in from;
        socklen_t fromLength = sizeof(from);
        const ssize_t n = recvfrom(s, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &fromLength);
        if (n <= 0)
            continue;
        // macOS delivers the IP header too, Linux only the ICMP message
        size_t at = 0;
        if ((buffer[0] >> 4) == 4)
            at = static_cast<size_t>(buffer[0] & 0x0F) * 4;
        if (static_cast<size_t>(n) < at + 8 || ntohl(from.sin_addr.s_addr) != to.addr)
            continue;
        const uint8_t* icmp = buffer + at;
        // Echo reply for our sequence (the identifier may be the kernel's)
        if (icmp[0] != 0 || icmp[6] != request[6] || icmp[7] != request[7])
            continue;
        MakeReply(request, icmp + 8, static_cast<size_t>(n) - at - 8, reply);
        ok = true;
    }
    close(s);
    return ok;
#endif
}

} // namespace netsock
