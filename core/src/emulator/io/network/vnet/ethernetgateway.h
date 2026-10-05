#pragma once

/// @file ethernetgateway.h
/// @brief The host side of frame-level network cards: a switch with a home router behind it, on top of the virtual
/// network's socket API (network tdd §7; owner decision Q9 = A: our own component, shared by every Ethernet card,
/// deterministic, reusing DhcpServer, DNS, the hosts table, the forwards, the host bridge and the TTD journal).
///
/// What a guest sends, and what the gateway does with it:
///   - ARP request for the router (10.0.2.2), the DNS address (10.0.2.3) or any address that is not a station on
///     this switch: answered with the router's MAC 52:55:0A:00:02:02; a station's own address (duplicate check)
///     and another station's address: silent (that station answers itself through the switch);
///   - IPv4 (header checksum checked; fragments and options dropped and counted):
///       UDP: one virtual network UDP socket per guest (address, port): DHCP (port 67) reaches DhcpServer by the
///            card's MAC (answers go out as broadcasts, as slirp does), DNS (port 53) the hosts table / host resolver,
///            anything else the host (NAT); answers come back as UDP frames from the sender;
///       ICMP echo: one virtual network ICMP socket per guest address (the router and DNS answer themselves);
///       TCP: terminated here (§7.3): a guest SYN opens a virtual network TCP socket and Connects; Connected ->
///            SYN-ACK (MSS = the guest's or 536), ConnectFailed -> RST; data both ways with sequence numbers, the
///            guest's window and MSS, ACKs, duplicate ACKs for out-of-order segments (dropped: the guest resends),
///            retransmission after 25 frames (doubling, 5 tries, then RST); FIN both ways, RST; a link reset of
///            the host (TTD left the recorded past) resets every connection; inbound Forward= rules make the guest
///            a server (a host client becomes a SYN to the guest's address);
///   - frames between stations on the switch (two cards) are switched directly.
///
/// Time and determinism: frames from a card are handled at once (inside its TXP access); the gateway's own answers
/// leave at the frame boundary (OnFrame, before the virtual network's Pump), an answer of the virtual network sends
/// what it causes when it is applied; frames are offered to each card in order and kept while its receive ring is
/// full (a switch with a buffer). The order makes a TTD replay exact: live, the host's answers apply inside Pump at
/// the boundary; replayed, from the journal right after it - with no instruction and no gateway work in between. The gateway has no host timers and no random
/// numbers: its initial sequence numbers come from the connection's addresses and a counter, its timers count
/// emulated frames. Host answers are the virtual network's journaled NetEvents, so a TTD replay needs no host and
/// reproduces every frame byte for byte. Its tables are a TTD state (SaveState / LoadState).
///
/// Worked example (the RTL kit's IFUP): a DHCP DISCOVER broadcast from 0.0.0.0:68 -> the gateway's UDP socket for
/// (0.0.0.0, 68) sends it to 255.255.255.255:67 -> DhcpServer offers 10.0.2.15 (by the card's MAC) -> at the frame
/// boundary the OFFER leaves as a broadcast frame from 52:55:0A:00:02:02, 10.0.2.2:67 -> the card's ring.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "common/network/nettypes.h"
#include "emulator/io/network/ethernet/ethernetlink.h"
#include "emulator/io/network/virtualnetwork.h"
#include "emulator/state/statenode.h"

class EthernetGateway final : public IEthernetLink, public INetGuest
{
public:
    static constexpr uint8_t kRouterMac[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    static constexpr size_t kMaxQueuedFrames = 512;     ///< per card, then the oldest is dropped (counted)
    static constexpr size_t kMaxTcpQueue = 256 * 1024;  ///< host bytes kept per connection before the guest reads
    static constexpr uint32_t kRtoFrames = 25;          ///< first retransmission after 0.5 s of emulated time
    static constexpr int kMaxRetries = 5;
    static constexpr size_t kCaptureLength = 256;       ///< frames kept for the capture (pcap)

    /// `frameCounter`: the machine's frame number (the gateway's clock)
    EthernetGateway(VirtualNetwork& network, std::function<uint64_t()> frameCounter);
    ~EthernetGateway() override;

    EthernetGateway(const EthernetGateway&) = delete;
    EthernetGateway& operator=(const EthernetGateway&) = delete;

    /// Forward= guest ports another device answers (a Hayes modem's MODEM,<port>): the gateway does not listen on
    /// them, so a host client of that port reaches that device, not an Ethernet card. Set before the first Attach
    void SetReservedGuestPorts(std::vector<uint16_t> ports) { _reservedGuestPorts = std::move(ports); }

    /// NAT: the switch with the router behind it (above). BRIDGE (network SN6, sn6-bridge-design.md): the switch only -
    /// the router, DHCP, DNS and TCP termination are off; a frame that is not for a local station (and every broadcast
    /// or multicast) goes to the LAN output (the host adapter), and frames from the host LAN come in through FromLan
    enum class Mode : uint8_t
    {
        Nat = 0,
        Bridge = 1,
    };
    /// Switching closes every NAT connection (the router goes away or comes back)
    void SetMode(Mode mode);
    Mode GetMode() const { return _mode; }
    /// BRIDGE: where frames for the LAN go (the host adapter). Unset: nowhere (a TTD replay sends nothing)
    void SetLanOutput(std::function<void(const uint8_t*, size_t)> output) { _lanOutput = std::move(output); }
    /// BRIDGE: a frame from the host LAN (the NetFrame input, applied at the frame boundary): queued for the station
    /// it is addressed to (every station for a broadcast or multicast), delivered at once
    void FromLan(const uint8_t* frame, size_t length);
    /// Every frame on the wire, as the capture records it (the virtual network's traffic tap): the port ("isa2.eth",
    /// "lan"), true = towards the port / from the LAN, and the bytes
    void SetFrameObserver(std::function<void(const std::string& port, bool toCard, bool lan, const uint8_t*, size_t)> o)
    {
        _frameObserver = std::move(o);
    }
    /// The cards' station addresses (the bridge's capture filter)
    std::vector<std::array<uint8_t, 6>> StationMacs() const;

    /// A card plugs into the switch (nullptr-safe detach with Detach)
    void Attach(IEthernetPort* port);
    void Detach(IEthernetPort* port);

    // IEthernetLink: a card put a frame on the wire
    void Transmit(IEthernetPort& from, const uint8_t* frame, size_t length) override;

    // INetGuest: the virtual network's answers
    void OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer, const uint8_t* data,
                    uint32_t length, uint32_t source) override;

    /// The frame boundary, before VirtualNetwork::Pump: retransmissions, TCP output, then the frames to the cards (an
    /// answer of the virtual network sends what it causes at once, see OnNetEvent)
    void OnFrame();

    /// A frame from outside (automation, tests) towards the card with this port key, as if from the wire
    bool Inject(const std::string& portKey, const std::vector<uint8_t>& frame);

    /// The machine reset reaches no ISA card: nothing to do. A bus reset of the network (the virtual network's
    /// Reset) closes every connection: the gateway forgets them
    void ForgetConnections();

    // --- Observation -------------------------------------------------------------------------------------

    struct Counters
    {
        uint64_t framesFromCards = 0, framesToCards = 0, switched = 0, arpReplies = 0, dhcp = 0, dns = 0, udp = 0,
                 icmp = 0, tcpConnections = 0, tcpRefused = 0, retransmits = 0, resets = 0, unsupported = 0,
                 fragments = 0, badChecksum = 0, runts = 0, queueDrops = 0, ringFullWaits = 0;
    };
    const Counters& GetCounters() const { return _counters; }
    /// BRIDGE: frames to and from the host LAN (observation only: not in the TTD state, unlike Counters)
    struct LanCounters
    {
        uint64_t out = 0, in = 0;
    };
    const LanCounters& GetLanCounters() const { return _lanCounters; }

    struct CapturedFrame
    {
        uint64_t frame = 0;           ///< the machine frame it passed in
        uint64_t index = 0;           ///< running number
        bool toCard = false;          ///< true: gateway -> card; false: card -> network
        bool lan = false;             ///< BRIDGE: crossed the host LAN (card -> LAN, or LAN -> the gateway, port "lan")
        std::string port;
        std::vector<uint8_t> bytes;
    };
    const std::deque<CapturedFrame>& Capture() const { return _capture; }
    /// The capture as a pcap file (LINKTYPE_ETHERNET; the timestamp is the machine frame x 20 ms)
    std::vector<uint8_t> CapturePcap(const std::string& portKey = std::string()) const;

    /// The report every automation surface prints (the `ethernet_gateway` section of state/network)
    StateNode Describe() const;

    // --- TTD (EthernetNics blob, after the cards) ----------------------------------------------------------

    std::vector<uint8_t> SaveState() const;
    bool LoadState(const uint8_t* data, size_t size);

    /// Address of the first lease / learned station (tests, inbound forwards)
    uint32_t GuestAddress() const;

private:
    struct Station
    {
        uint8_t mac[6] = {};
        uint32_t ip = 0;
    };

    enum class TcpState : uint8_t
    {
        Connecting = 0,     ///< guest SYN taken, host connect in progress
        SynAckSent = 1,     ///< SYN-ACK sent, waiting for the guest's ACK
        Established = 2,
        SynSentToGuest = 3, ///< inbound: a host client, SYN sent to the guest
        Closed = 4,
    };

    struct TcpConn
    {
        uint32_t id = 0;              ///< the vnet cookie
        uint16_t socket = 0;          ///< the virtual network socket
        TcpState state = TcpState::Connecting;
        uint32_t guestIp = 0, remoteIp = 0;
        uint16_t guestPort = 0, remotePort = 0;
        uint8_t guestMac[6] = {};
        uint32_t iss = 0;             ///< our initial sequence number
        uint32_t sndUna = 0, sndNxt = 0;   ///< oldest unacknowledged, next to send (ours)
        uint32_t rcvNxt = 0;          ///< next byte expected from the guest
        uint16_t guestWindow = 0, guestMss = 536;
        bool guestFin = false, hostFin = false, finSent = false;
        uint64_t lastSendFrame = 0;   ///< for the retransmission timer
        uint32_t rto = kRtoFrames;
        uint8_t retries = 0;
        std::deque<uint8_t> queue;    ///< host bytes from sndUna on (sent and not acknowledged, then unsent)
    };

    struct UdpFlow
    {
        uint32_t id = 0;
        uint16_t socket = 0;
        uint32_t guestIp = 0;
        uint16_t guestPort = 0;
        uint8_t guestMac[6] = {};
        uint64_t lastUsedFrame = 0;
    };

    struct IcmpFlow
    {
        uint32_t id = 0;
        uint16_t socket = 0;
        uint32_t guestIp = 0;
        uint8_t guestMac[6] = {};
    };

    struct Listener
    {
        uint32_t id = 0;
        uint16_t socket = 0;
        uint16_t guestPort = 0;
    };

    struct PortQueue
    {
        IEthernetPort* port = nullptr;
        std::deque<std::vector<uint8_t>> frames;
    };

    // Frames out
    void Queue(const uint8_t* dstMac, const std::vector<uint8_t>& frame, IEthernetPort* only = nullptr);
    std::vector<uint8_t> Ethernet(const uint8_t* dst, const uint8_t* src, uint16_t type, const std::vector<uint8_t>& payload) const;
    std::vector<uint8_t> Ipv4(uint32_t src, uint32_t dst, uint8_t protocol, const std::vector<uint8_t>& payload);
    void SendIp(const uint8_t* dstMac, uint32_t src, uint32_t dst, uint8_t protocol, const std::vector<uint8_t>& payload);
    void SendUdp(const uint8_t* dstMac, uint32_t src, uint16_t srcPort, uint32_t dst, uint16_t dstPort,
                 const uint8_t* data, size_t length);
    void SendTcp(TcpConn& c, uint8_t flags, uint32_t seq, const uint8_t* data, size_t length, bool withMss = false);
    void SendRst(const uint8_t* dstMac, uint32_t src, uint16_t srcPort, uint32_t dst, uint16_t dstPort, uint32_t seq,
                 uint32_t ack, bool withAck);

    // Frames in
    void HandleArp(IEthernetPort& from, const uint8_t* frame, size_t length);
    void HandleIpv4(IEthernetPort& from, const uint8_t* frame, size_t length);
    void HandleUdp(const uint8_t* srcMac, uint32_t srcIp, uint32_t dstIp, const uint8_t* udp, size_t length);
    void HandleIcmp(const uint8_t* srcMac, uint32_t srcIp, uint32_t dstIp, const uint8_t* icmp, size_t length);
    void HandleTcp(const uint8_t* srcMac, uint32_t srcIp, uint32_t dstIp, const uint8_t* tcp, size_t length);

    void HandleNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                        const uint8_t* data, uint32_t length, uint32_t source);
    /// Offer the queued frames to the cards (in order, while each takes them)
    void Deliver();

    // TCP
    TcpConn* FindConn(uint32_t guestIp, uint16_t guestPort, uint32_t remoteIp, uint16_t remotePort);
    TcpConn* FindConnById(uint32_t id);
    void CloseConn(uint32_t id, bool closeSocket);
    void PumpTcp(TcpConn& c);
    void OpenListeners();
    void Learn(const uint8_t* mac, uint32_t ip);
    bool IsStationIp(uint32_t ip) const;
    void Record(bool toCard, const std::string& port, const std::vector<uint8_t>& bytes);
    uint32_t NextId() { return _nextId++; }

    VirtualNetwork& _network;
    std::function<uint64_t()> _frameCounter;
    std::vector<PortQueue> _ports;
    std::vector<Station> _stations;            ///< MAC -> IP pairs learned from ARP / IP / DHCP
    std::map<uint32_t, TcpConn> _tcp;          ///< by id (the vnet cookie)
    std::map<uint32_t, UdpFlow> _udp;
    std::map<uint32_t, IcmpFlow> _icmp;
    std::vector<Listener> _listeners;
    std::vector<uint16_t> _reservedGuestPorts;
    uint32_t _nextId = 1;
    uint16_t _ipId = 1;
    uint32_t _issCounter = 0;
    Counters _counters;
    std::deque<CapturedFrame> _capture;
    uint64_t _captureIndex = 0;
    Mode _mode = Mode::Nat;
    LanCounters _lanCounters;
    std::function<void(const std::string&, bool, bool, const uint8_t*, size_t)> _frameObserver;
    std::function<void(const uint8_t*, size_t)> _lanOutput;
};
