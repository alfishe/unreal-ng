#pragma once

/// @file espstack.h
/// @brief The socket side of an emulated ESP module (network TDD §7.2, step
/// N3): what the ESP's lwIP does for the ESPNET and AT firmwares, on top of
/// the machine's virtual network. Slots like the firmware's (8 on ESP32, 4 on
/// ESP8266); TCP clients, UDP sockets, TCP servers with a queue of clients;
/// host names resolved through the virtual network's DNS.
///
/// Everything that arrives (bytes, connect results, clients, DNS answers) is a
/// journaled NetEvent of the virtual network, so the stack is deterministic
/// and a TTD replay needs no host. Received bytes keep their journal source
/// (record index + offset): a checkpoint stores references, not bytes.
///
/// Slow operations (connect, DNS) finish later: the stack reports them
/// through onDone, at the emulated time the network delivered the answer.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "common/network/nettypes.h"
#include "emulator/io/network/netstate.h"

class VirtualNetwork;

class EspStack : public INetGuest
{
public:
    enum class State : uint8_t
    {
        Free = 0,
        TcpIdle = 1,    ///< a TCP socket, not connected yet
        Tcp = 2,        ///< connected (or accepted)
        Udp = 3,
        Listen = 4,
    };

    /// A received byte and where it came from
    struct RxByte
    {
        uint8_t value = 0;
        uint32_t source = 0;   ///< 1-based journal index of the NetEvent (0 = not journaled)
        uint32_t offset = 0;
    };

    /// A received UDP datagram
    struct Datagram
    {
        NetEndpoint from;
        std::vector<RxByte> data;
    };

    /// A client a listening slot holds until ACCEPT takes it
    struct Pending
    {
        uint16_t vnetId = 0;
        NetEndpoint peer;
        bool finSeen = false;
        std::deque<RxByte> rx;
    };

    struct Slot
    {
        State state = State::Free;
        uint16_t vnetId = 0;       ///< the virtual-network socket (0: none)
        uint16_t localPort = 0;    ///< BIND
        NetEndpoint remote;
        bool connecting = false;   ///< CONNECT waits for the network
        bool finSeen = false;      ///< the peer closed its side
        std::deque<RxByte> rx;     ///< TCP: bytes not read yet
        std::deque<Datagram> datagrams;   ///< UDP
        uint16_t waitingId = 0;    ///< Listen: the virtual-network socket waiting for the next client
        std::deque<Pending> pending;      ///< Listen: clients not accepted yet
    };

    /// A slow operation finished
    struct Done
    {
        enum class Kind : uint8_t
        {
            Connect,
            Resolve,
            Ping,       ///< an ICMP echo came back (data: the reply)
            Query       ///< the firmware's own UDP query was answered (data: the datagram)
        } kind = Kind::Connect;
        int slot = -1;              ///< Connect
        NetEventStatus status = NetEventStatus::Ok;
        uint32_t addr = 0;          ///< Resolve: the address (0 = not found)
        std::vector<uint8_t> data;  ///< Ping, Query
    };
    std::function<void(const Done&)> onDone;

    /// Called when data, a datagram, a client or a close arrived (the module
    /// may have output to produce: AT's +IPD)
    std::function<void(int slot)> onData;

    EspStack(VirtualNetwork* network, int slots);
    ~EspStack() override;

    EspStack(const EspStack&) = delete;
    EspStack& operator=(const EspStack&) = delete;

    int SlotCount() const { return static_cast<int>(_slots.size()); }
    const Slot& GetSlot(int i) const { return _slots[static_cast<size_t>(i)]; }
    bool Valid(int i) const { return i >= 0 && i < SlotCount() && _slots[static_cast<size_t>(i)].state != State::Free; }
    uint8_t SlotMask() const;

    /// A free slot for a new TCP (state TcpIdle) or UDP socket; -1 when all
    /// are taken (dead TCP slots - closed, nothing left to read - are reaped first)
    int Open(bool tcp);
    /// The same in a given slot (AT link ids); -1 when the slot is taken
    int OpenAt(int slot, bool tcp);

    /// Start a TCP connection; onDone(Connect) follows
    void Connect(int slot, const NetEndpoint& to);
    void Bind(int slot, uint16_t port) { _slots[static_cast<size_t>(slot)].localPort = port; }
    /// Make a TCP slot a server on its bound port (backlog = the slot count)
    void Listen(int slot);
    /// The first queued client into a free slot; -1 when none is queued or no slot is free
    int Accept(int listenSlot);
    /// TCP: send everything (the virtual network queues it)
    void Send(int slot, const uint8_t* data, uint32_t length);
    /// UDP: one datagram
    void SendTo(int slot, const NetEndpoint& to, const uint8_t* data, uint32_t length);
    /// Up to `max` received TCP bytes
    std::vector<uint8_t> Read(int slot, uint32_t max);
    /// The oldest received UDP datagram (false when none)
    bool PopDatagram(int slot, Datagram& out);
    /// Free the slot (closing its sockets); -1 = every slot
    void Close(int slot);
    /// Stop every connection, keep the slots (Wi-Fi went down)
    void StopAll();

    /// Resolve a host name through the virtual network's DNS; onDone(Resolve) follows
    void Resolve(const std::string& name);

    /// ICMP echo to `to` (`request` = the whole echo message); onDone(Ping) follows if it answers
    void Ping(uint32_t to, const std::vector<uint8_t>& request);

    /// One datagram from the firmware itself (SNTP), not on a socket slot;
    /// onDone(Query) follows with the first datagram that comes back
    void Query(const NetEndpoint& to, const std::vector<uint8_t>& request);

    /// Is the TCP connection still open (no FIN yet)?
    bool Established(int slot) const;

    /// TTD state (netstate::EspStack); false when something did not fit
    bool SaveState(netstate::EspStackState& out) const;
    using ByteSource = std::function<bool(uint32_t source, uint32_t offset, uint32_t length, std::vector<uint8_t>& out)>;
    bool LoadState(const netstate::EspStackState& in, const ByteSource& bytes);
    /// The virtual-network sockets of this stack belong to it (TTD restore)
    void RebindAll();

    // INetGuest
    void OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                    const uint8_t* data, uint32_t length, uint32_t source) override;

    /// Frame boundary: deferred socket changes (never inside the network's delivery)
    void OnFrame();

private:
    uint16_t OpenVnet(NetProto proto);
    void ArmListener(int slot);
    void CloseVnet(uint16_t id);
    void Reap();

    VirtualNetwork* _network = nullptr;
    std::vector<Slot> _slots;
    std::vector<uint16_t> _closeLater;   ///< virtual-network sockets to close at the frame boundary
    std::vector<int> _rearm;             ///< listening slots to give a new waiting socket

    uint16_t _pingSocket = 0;
    uint16_t _querySocket = 0;

    // DNS
    uint16_t _dnsSocket = 0;
    uint16_t _dnsId = 0;
    uint16_t _dnsSeq = 0;
    bool _resolving = false;
    std::string _resolveName;
};
