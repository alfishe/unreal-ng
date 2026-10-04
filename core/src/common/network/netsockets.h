#pragma once

/// @file netsockets.h
/// @brief Minimal portable non-blocking IPv4 sockets for the host bridge.
///
/// The header carries no OS headers: sockets are opaque handles, errors are a
/// small portable enum. BSD sockets on macOS / Linux, Winsock on Windows, all
/// inside netsockets.cpp.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common/network/nettypes.h"

namespace netsock
{

using Handle = intptr_t;
constexpr Handle kInvalid = -1;

enum class Result : uint8_t
{
    Ok,
    WouldBlock,     ///< nothing now (non-blocking), or connect in progress
    Refused,
    Unreachable,
    Timeout,
    AddressInUse,
    Closed,         ///< orderly close by the peer (recv returned 0)
    Reset,
    Error,
};

/// Initialise the socket library once per process (Winsock); safe to call often
bool Startup();

Handle OpenTcp();
Handle OpenUdp();
void Close(Handle h);

/// Start a non-blocking connect: Ok (done at once) or WouldBlock (in progress)
Result Connect(Handle h, const NetEndpoint& to);

/// Result of a finished non-blocking connect (after the socket became writable)
Result ConnectResult(Handle h);

Result Bind(Handle h, uint32_t addr, uint16_t port, bool reuse);
Result Listen(Handle h, int backlog);
Handle Accept(Handle h, NetEndpoint& peer);

/// Local port of a bound socket (0 on error)
uint16_t LocalPort(Handle h);

/// Local address of a bound or connected socket (0 = unbound / any, or an error). A UDP socket "connected" to a
/// remote address gives the host's own address on the route there (no packet is sent)
uint32_t LocalAddress(Handle h);

/// Send / receive: `done` is the byte count; Ok or WouldBlock or an error
Result Send(Handle h, const uint8_t* data, size_t length, size_t& done);
Result Recv(Handle h, uint8_t* data, size_t capacity, size_t& done);
Result SendTo(Handle h, const NetEndpoint& to, const uint8_t* data, size_t length);
Result RecvFrom(Handle h, uint8_t* data, size_t capacity, size_t& done, NetEndpoint& from);

/// Half-close (send FIN)
void ShutdownWrite(Handle h);

/// Wait for readiness. `wantWrite[i]` asks for writability too.
struct PollItem
{
    Handle handle = kInvalid;
    bool wantRead = true;
    bool wantWrite = false;
    bool readable = false;
    bool writable = false;
    bool failed = false;   ///< error / hang-up reported by poll
};
int Poll(std::vector<PollItem>& items, int timeoutMs);

/// Resolve a host name to IPv4 addresses (blocking; call from a worker thread)
bool ResolveIpv4(const std::string& name, std::vector<uint32_t>& out);

/// ICMP echo without privileges (blocking; call from a worker thread).
/// `request` is the whole ICMP echo request (type 8, code, checksum, id, seq,
/// data); `reply` gets the whole echo reply with the request's identifier and
/// sequence (the host may renumber them on the wire). macOS / Linux: a
/// datagram ICMP socket (Linux needs net.ipv4.ping_group_range to allow it);
/// Windows: IcmpSendEcho. False on timeout or when the host does not allow it
bool Ping(const NetEndpoint& to, const uint8_t* request, size_t length, uint32_t timeoutMs, std::vector<uint8_t>& reply);

} // namespace netsock
