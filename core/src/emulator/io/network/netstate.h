#pragma once

/// @file netstate.h
/// @brief Fixed-size TTD state of the network adapter set (network adapters
/// TDD §6.3, option A): the ZXNETUSB card, its W5300 and the virtual network.
///
/// Received bytes are not stored: every byte the chip holds came from a
/// journaled NetEvent, so the state keeps references (journal index of the
/// network record, offset, length) and the loader takes the bytes from the
/// journal. Bytes the Z80 wrote but the chip has not sent yet are stored (at
/// a frame boundary usually none). Limits below are generous; a state that
/// does not fit is saved as far as it goes and marked incomplete.
///
/// Plain structs, filled after a memset (padding bytes are always zero, so
/// equal states give equal blobs).

#include <cstdint>
#include <type_traits>

namespace netstate
{

constexpr int kSockets = 8;
constexpr int kMaxPackets = 64;         ///< receive packets per W5300 socket
constexpr int kMaxBacklog = 32;         ///< TCP chunks waiting for receive memory, per socket
constexpr int kMaxTxBytes = 1536;       ///< bytes written but not sent, per socket (one MSS and more)
constexpr int kMaxNetSockets = 32;
constexpr int kMaxListeners = 8;
constexpr int kMaxWaiting = 8;
constexpr int kMaxPending = 8;
constexpr int kMaxLeases = 16;

// Trivial types only (no member initializers): the blob is cleared with memset
// and copied with memcpy

struct Reference
{
    uint32_t source;            ///< 1-based journal index of the network record (0 = not journaled)
    uint32_t sourceOffset;      ///< offset inside that record's payload
    uint32_t length;
};

struct RxPacket
{
    uint8_t header[8];
    uint8_t headerLength;
    uint8_t reserved[3];
    Reference data;
};

struct W5300Socket
{
    uint8_t mr[2];
    uint8_t imr, ir, ssr;
    uint8_t portr[2];
    uint8_t dhar[6];
    uint8_t dportr[2];
    uint8_t dipr[4];
    uint8_t mssr[2];
    uint8_t kpalvtr, protor, tosr, ttlr, fragr;
    uint8_t txLatch, txHalf;
    uint8_t reserved[3];
    uint32_t wrsr;
    uint32_t txSize, rxSize;
    uint16_t net;
    uint16_t txLength;
    uint8_t tx[kMaxTxBytes];
    uint32_t rxRead;
    uint16_t packetCount;
    uint16_t backlogCount;
    RxPacket packets[kMaxPackets];
    Reference backlog[kMaxBacklog];
};

struct NetSocket
{
    uint16_t id, hostId;
    uint8_t proto, connected, hasGuest, reserved;
    uint32_t cookie;
    uint32_t remoteAddr;
    uint16_t remotePort, listenPort;
    uint64_t bytesIn, bytesOut;
};

struct Listener
{
    uint16_t guestPort, hostListenerId, hostPort;
    uint8_t waitingCount, pendingCount;
    uint16_t waiting[kMaxWaiting];
    struct
    {
        uint16_t hostId;
        uint16_t port;
        uint32_t addr;
    } pending[kMaxPending];
};

struct Lease
{
    uint8_t mac[6];
    uint8_t reserved[2];
    uint32_t addr;
};

struct VirtualNetwork
{
    uint16_t nextId;
    uint16_t socketCount, listenerCount, leaseCount;
    NetSocket sockets[kMaxNetSockets];
    Listener listeners[kMaxListeners];
    Lease leases[kMaxLeases];
    uint64_t counters[6];   ///< dhcpReplies, dnsLocalAnswers, dnsHostQueries, echoReplies, hostEvents, linkResets
};

struct Adapters
{
    uint32_t version;       ///< kVersion
    uint8_t present;        ///< a card was fitted at capture
    uint8_t incomplete;     ///< some state did not fit the limits above
    uint8_t p83, p82, p81;
    uint8_t reserved[3];
    uint8_t common[256];
    W5300Socket sockets[kSockets];
    VirtualNetwork network;
};

constexpr uint32_t kVersion = 1;

static_assert(std::is_trivial_v<Adapters>, "the network state blob is cleared and copied as bytes");

} // namespace netstate
