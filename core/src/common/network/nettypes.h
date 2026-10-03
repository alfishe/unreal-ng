#pragma once

/// @file nettypes.h
/// @brief Types shared by the network adapters, the virtual network and the
/// host bridge (network adapters TDD, docs/inprogress/2026-09-30-nedoos-integration/tdd-network.md).
///
/// Addresses are IPv4 in host byte order: a.b.c.d = 0xAABBCCDD. Ports are host
/// byte order too. Nothing here depends on host socket headers.

#include <cstdint>
#include <string>
#include <vector>

/// What a host (or the virtual network itself) reports about one socket.
/// Journaled as TTDInputEvent::netEvent: values are part of the .ttd format.
enum class NetEventType : uint8_t
{
    None = 0,
    Connected = 1,       ///< TCP connect finished (netAddr/netPort: peer)
    ConnectFailed = 2,   ///< TCP connect failed (netStatus says why)
    Data = 3,            ///< TCP bytes arrived (payload)
    PeerClosed = 4,      ///< TCP peer sent FIN: no more data will come
    Reset = 5,           ///< TCP connection is gone (RST, host error, link reset)
    Accepted = 6,        ///< a host client connected to a listener: netSocket = listener, payload = new id (u16 LE)
    Datagram = 7,        ///< UDP datagram arrived (netAddr/netPort: sender, payload: data)
    EchoReply = 8,       ///< ICMP echo reply (netAddr: sender, payload: echo data)
    ListenFailed = 9,    ///< the host could not listen for a guest server
    ModemLines = 10,     ///< a host serial device's input lines changed (payload: 1 byte, MSR layout: CTS #10, DSR #20, RI #40, DCD #80)
};

/// Line format of a serial link (a host serial device follows what the ZX
/// programmed into its UART)
struct SerialLine
{
    uint32_t baud = 115200;
    uint8_t dataBits = 8;     ///< 5..8
    char parity = 'N';        ///< N, O, E, M (mark), S (space)
    uint8_t stopBits = 1;     ///< 1 or 2
    bool operator==(const SerialLine& o) const
    {
        return baud == o.baud && dataBits == o.dataBits && parity == o.parity && stopBits == o.stopBits;
    }
    bool operator!=(const SerialLine& o) const { return !(*this == o); }
};

/// Why a network operation failed. Journaled as TTDInputEvent::netStatus.
enum class NetEventStatus : uint8_t
{
    Ok = 0,
    Refused = 1,
    Timeout = 2,
    Unreachable = 3,
    AddressInUse = 4,
    Denied = 5,          ///< blocked by the virtual network's allow / deny rules
    Error = 6,
};

enum class NetProto : uint8_t
{
    Tcp = 0,
    Udp = 1,
    Icmp = 2,
    Serial = 3,   ///< a host serial device as a byte stream (COM port peer SERIAL:)
};

struct NetEndpoint
{
    uint32_t addr = 0;
    uint16_t port = 0;
};

inline bool operator==(const NetEndpoint& a, const NetEndpoint& b)
{
    return a.addr == b.addr && a.port == b.port;
}

/// Build an address from its dotted parts
constexpr uint32_t NetIp(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 8) | d;
}

std::string NetIpToString(uint32_t addr);

/// Short name of a status: ok, refused, timeout, unreachable, address-in-use, denied, error
const char* NetStatusText(NetEventStatus status);
bool NetIpFromString(const std::string& text, uint32_t& addr);

/// One event from the host bridge to the virtual network (bridge thread ->
/// emulation thread). Becomes a TTD NetEvent when applied.
struct HostNetEvent
{
    NetEventType type = NetEventType::None;
    NetEventStatus status = NetEventStatus::Ok;
    uint16_t socket = 0;
    NetEndpoint peer;
    std::vector<uint8_t> data;
};

/// The guest side of a virtual-network socket: an adapter (W5300 socket, ESP
/// module socket) that receives the socket's events.
class INetGuest
{
public:
    /// Delivered on the emulation thread. `data` is valid only during the call.
    /// `source` names where the bytes came from: the 1-based TTD journal index
    /// of the network record (0 = not journaled), so a device can checkpoint
    /// references to its buffered bytes instead of the bytes.
    virtual void OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                            const uint8_t* data, uint32_t length, uint32_t source) = 0;
    virtual ~INetGuest() = default;
};

/// The serial ports' peers on the network (an ESP module's stack, a TCP or
/// serial-device link): the #xxEF port's (a ZX-Evo AVR UART or a ZX-WiFi
/// card), the machine's own (ATM Turbo 2+ keyboard controller) and the
/// ATM2IOESP card's. In the TTD tables they are guests 2, 3 and 4; the
/// ZXNETUSB card's chip is guest 1
struct SerialGuests
{
    INetGuest* com = nullptr;
    INetGuest* machine = nullptr;
    INetGuest* atmIo = nullptr;   ///< the ATM2IOESP card's peer (guest 4)
    INetGuest* zifi = nullptr;    ///< the ZiFi line's peer (guest 5)
    INetGuest* ethernet = nullptr;   ///< the Ethernet gateway of the frame-level cards (guest 6; network tdd §7)
    /// The UART cards in expansion slots 1 / 2 (the Sprinter's SprinterESP; guests 7 / 8, network tdd §8.2)
    INetGuest* slotUart[2] = {nullptr, nullptr};
};
