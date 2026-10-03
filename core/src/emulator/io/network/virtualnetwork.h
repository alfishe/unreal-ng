#pragma once

/// @file virtualnetwork.h
/// @brief The machine's virtual network (network adapters TDD §5, §6).
///
/// Network adapters (the W5300 on ZXNETUSB, later the ESP modules) open
/// sockets here. The virtual network answers what a home router would
/// (DHCP, gateway, DNS from the hosts table), forwards the rest to the host
/// through IHostNet, and delivers every answer back to the adapter's socket.
///
/// Determinism (TDD §6):
///  - Answers the virtual network computes itself (DHCP, hosts table, refused
///    connects to its own addresses, ping of the gateway) depend only on what
///    the program sent: they are queued and delivered at the next frame
///    boundary, live and in replay alike.
///  - Everything the host answered is a TTD input: Pump() drains the host at
///    the frame boundary and submits each answer as a NetEvent (payload =
///    the bytes); it reaches the adapter through ApplyHostEvent, from the live
///    path or from the journal during replay.
///  - While TTD replays (the journal owns input) nothing is sent to the host.
///    When the machine leaves the recorded past and runs live again, the host
///    connections that existed back then are gone: a NetLinkReset input tells
///    every TCP socket so.
///
/// Threading: everything here runs on the emulation thread. The host side
/// (HostNetBridge) has its own thread behind IHostNet.

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/network/hostnet.h"
#include "common/network/nettypes.h"
#include "emulator/io/network/netstate.h"
#include "emulator/io/network/vnet/dhcpserver.h"

class EmulatorContext;
namespace ttd { struct TTDNetInput; }

struct VirtualNetworkConfig
{
    uint32_t network = NetIp(10, 0, 2, 0);
    uint32_t mask = NetIp(255, 255, 255, 0);
    uint32_t gateway = NetIp(10, 0, 2, 2);
    uint32_t dnsServer = NetIp(10, 0, 2, 3);
    uint32_t firstLease = NetIp(10, 0, 2, 15);

    /// Host: every query to UDP port 53 (any server: the NedoOS kernel asks
    /// 8.8.4.4) is answered from the host resolver. Pass: sent as plain UDP.
    enum class DnsMode : uint8_t { Host, Pass } dnsMode = DnsMode::Host;

    /// Names answered by the virtual network itself (tests, pinned names)
    std::map<std::string, uint32_t> hosts;

    /// Guest server port -> host port on 127.0.0.1. Guest ports >= 1024 without
    /// a rule use the same host port; lower ones need a rule.
    std::map<uint16_t, uint16_t> forwards;
};

class VirtualNetwork
{
public:
    static constexpr uint16_t kNoSocket = 0;

    /// @param host the host side; nullptr = no host network (internal services only)
    VirtualNetwork(EmulatorContext* context, std::unique_ptr<IHostNet> host, const VirtualNetworkConfig& config);
    ~VirtualNetwork();

    VirtualNetwork(const VirtualNetwork&) = delete;
    VirtualNetwork& operator=(const VirtualNetwork&) = delete;

    // --- Guest socket API (adapters) -------------------------------------

    /// New socket owned by `guest`; events come back with `cookie`
    uint16_t Open(NetProto proto, INetGuest* guest, uint32_t cookie);

    /// Hand a socket to another guest socket (an accepted connection)
    void Rebind(uint16_t id, INetGuest* guest, uint32_t cookie);

    void Connect(uint16_t id, const NetEndpoint& to);

    /// A NetProto::Serial socket opens a host serial device (the COM port's
    /// SERIAL: peer): Connected or ConnectFailed follows, then Data events;
    /// Send writes to the device. Unreachable without host access
    void ConnectSerial(uint16_t id, const std::string& device, uint32_t baud);

    /// A serial socket's device follows the ZX's line format / modem control
    /// (commands to the host: not issued while TTD replays)
    void ConfigureSerial(uint16_t id, const SerialLine& line);
    void SerialModemLines(uint16_t id, bool rts, bool dtr);
    void Send(uint16_t id, const uint8_t* data, uint32_t length);
    void SendTo(uint16_t id, uint16_t localPort, const NetEndpoint& to, const uint8_t* data, uint32_t length);
    void ShutdownWrite(uint16_t id);

    /// The socket waits for a client on guest port `guestPort`: the next
    /// client of that port is delivered to it as Accepted (payload: its new id)
    void Listen(uint16_t id, uint16_t guestPort);

    void Close(uint16_t id);

    /// Close every socket, forget leases and listeners (machine reset, adapter
    /// removed). The sockets of `keep` stay: a ZX-Bus reset resets the card,
    /// not the COM port's cable (its TCP link or host device)
    void Reset(const SerialGuests& keep = {});

    // --- Time ------------------------------------------------------------

    /// Frame boundary (after the TTD checkpoint, with the other host input):
    /// deliver the virtual network's own answers, then the host's (journaled)
    void Pump();

    // --- TTD input (ttdinputapply.cpp) ------------------------------------

    void ApplyHostEvent(const ttd::TTDNetInput& net, const uint8_t* payload);
    void ApplyLinkReset();
    /// A guest stream (TCP connected or connecting, a serial line) a link reset would end
    bool HasResettableStreams() const;

    // --- State (automation, tests) ----------------------------------------

    struct SocketInfo
    {
        uint16_t id = 0;
        uint16_t hostId = 0;
        NetProto proto = NetProto::Tcp;
        bool connected = false;
        NetEndpoint remote;
        uint16_t listenPort = 0;
        uint64_t bytesIn = 0;
        uint64_t bytesOut = 0;
    };
    std::vector<SocketInfo> Sockets() const;

    struct ListenerInfo
    {
        uint16_t guestPort = 0;
        uint16_t hostPort = 0;   ///< 0: no host listener (no rule for a port below 1024)
        size_t waitingSockets = 0;
        size_t pendingClients = 0;
    };
    std::vector<ListenerInfo> Listeners() const;

    /// What the virtual network did (automation status, tests)
    struct Counters
    {
        uint64_t dhcpReplies = 0;     ///< OFFER / ACK / NAK sent
        uint64_t dnsLocalAnswers = 0; ///< answered from the hosts table
        uint64_t dnsHostQueries = 0;  ///< passed to the host resolver
        uint64_t echoReplies = 0;     ///< pings answered for the gateway / DNS address
        uint64_t hostEvents = 0;      ///< host answers applied (journaled NetEvents)
        uint64_t linkResets = 0;
    };
    const Counters& GetCounters() const { return _counters; }

    /// Recent socket activity, newest last (a short history for status views;
    /// the full traffic view is the network debugging TDD)
    struct Activity
    {
        uint64_t frame = 0;
        uint16_t socket = 0;
        NetProto proto = NetProto::Tcp;
        std::string action;       ///< open, connect, connected, connect-failed, send, dns, dhcp, close, reset, peer-closed, accepted
        NetEndpoint remote;
        NetEventStatus status = NetEventStatus::Ok;
        uint32_t bytes = 0;
    };
    static constexpr size_t kActivityLength = 64;
    const std::deque<Activity>& RecentActivity() const { return _activity; }

    const VirtualNetworkConfig& Config() const { return _config; }

    /// The address of a station with this MAC (a DHCP lease, made now if needed)
    uint32_t LeaseFor(const DhcpServer::Mac& mac) { return _dhcp.Lease(mac); }
    const DhcpServer& Dhcp() const { return _dhcp; }
    IHostNet* Host() const { return _host.get(); }

    /// Swap the host side (automation: host access on / off; tests: a fake).
    /// Existing host connections are gone: TCP sockets see a link reset
    void ReplaceHost(std::unique_ptr<IHostNet> host);

    /// True while nothing talks to the host (TTD replay owns input)
    bool IsReplaying() const;

    /// Host port a guest server port is reachable on (0 = none)
    uint16_t HostPortFor(uint16_t guestPort) const;

    // --- TTD state (netstate.h) --------------------------------------------

    /// Guest-side tables (sockets, guest servers, leases, counters). Returns
    /// false when something did not fit the fixed limits (saved as far as it goes)
    /// `serial`: the serial ports' peers, saved as guests 2 and 3 (every other guest is 1)
    bool SaveState(netstate::VirtualNetwork& out, const SerialGuests& serial = {}) const;

    /// Restore the tables; a socket of guest 1 is handed to `guest` (the
    /// card's chip), of guests 2 / 3 to the serial ports' peers. Queued
    /// answers are dropped: after the checkpoint they come from the journal
    void LoadState(const netstate::VirtualNetwork& in, INetGuest* guest, const SerialGuests& serial = {});

private:
    struct Socket
    {
        uint16_t id = 0;
        uint16_t hostId = 0;           ///< the host bridge's id (differs for accepted connections)
        NetProto proto = NetProto::Tcp;
        INetGuest* guest = nullptr;
        uint32_t cookie = 0;
        bool connected = false;
        NetEndpoint remote;
        uint16_t listenPort = 0;
        uint64_t bytesIn = 0;
        uint64_t bytesOut = 0;
    };

    struct Listener
    {
        uint16_t hostListenerId = 0;
        uint16_t hostPort = 0;
        std::deque<uint16_t> waiting;          ///< guest sockets in LISTEN, oldest first
        struct Pending { uint16_t hostId; NetEndpoint peer; };
        std::deque<Pending> pending;           ///< host clients no guest socket took yet
    };

    struct Deferred
    {
        uint16_t id = 0;
        NetEventType type = NetEventType::None;
        NetEventStatus status = NetEventStatus::Ok;
        NetEndpoint peer;
        std::vector<uint8_t> data;
    };

    bool IsInternal(uint32_t addr) const;
    uint16_t AllocateId();
    Socket* Find(uint16_t id);
    Socket* FindByHostId(uint16_t hostId);
    void Defer(uint16_t id, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
               std::vector<uint8_t> data = {});
    void Deliver(Socket& s, NetEventType type, NetEventStatus status, const NetEndpoint& peer, const uint8_t* data,
                 uint32_t length, uint32_t source = 0);
    void HandOver(uint16_t guestPort, Listener& l);
    void SubmitHostEvent(const HostNetEvent& ev);
    bool AnswerIcmpEcho(uint16_t id, const NetEndpoint& to, const uint8_t* data, uint32_t length);

    EmulatorContext* _context = nullptr;
    std::unique_ptr<IHostNet> _host;
    VirtualNetworkConfig _config;
    DhcpServer _dhcp;

    std::map<uint16_t, Socket> _sockets;
    std::map<uint16_t, Listener> _listeners;   ///< by guest port
    std::deque<Deferred> _deferred;
    uint16_t _nextId = 1;
    bool _wasReplaying = false;
    Counters _counters;
    std::deque<Activity> _activity;
    void Note(uint16_t id, NetProto proto, const char* action, const NetEndpoint& remote,
              NetEventStatus status = NetEventStatus::Ok, uint32_t bytes = 0);
};
