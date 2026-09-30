#pragma once

/// @file w5300.h
/// @brief WIZnet W5300 Ethernet controller, 8-bit direct bus mode, as the
/// ZXNETUSB card uses it (network adapters TDD §4,
/// docs/inprogress/2026-09-30-nedoos-integration/reference-w5300-model.md).
///
/// Socket level, not packet level: a socket's commands become virtual-network
/// socket operations, and what the network answers becomes the chip's socket
/// state, interrupt bits and receive packets. Visible behavior follows the
/// datasheet (§10.1 of the reference), including:
///  - Sn_CR reads 0 as soon as any command was accepted (drivers spin on it);
///  - Sn_SSR is an explicit state machine, reads have no side effects;
///  - receive memory holds per packet PACKET-INFO + data + a pad byte for odd
///    lengths; Sn_RX_RSR counts all of it and drops by 2 per FIFO word;
///  - the FIFO pair (FIFOR0 then FIFOR1) survives other register accesses in
///    between (NedoOS splits a pair across two kernel calls).
///
/// Addresses are the chip's 10-bit byte addresses (#000..#3FF): common
/// registers #000..#0FF, socket n at #200 + #40 * n. 16-bit registers are big
/// endian: the even address holds the high byte.

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "common/network/nettypes.h"
#include "emulator/io/network/netstate.h"

class VirtualNetwork;

class W5300 : public INetGuest
{
public:
    static constexpr int kSockets = 8;

    // Socket states (Sn_SSR)
    static constexpr uint8_t kSockClosed = 0x00;
    static constexpr uint8_t kSockInit = 0x13;
    static constexpr uint8_t kSockListen = 0x14;
    static constexpr uint8_t kSockSynSent = 0x15;
    static constexpr uint8_t kSockEstablished = 0x17;
    static constexpr uint8_t kSockFinWait = 0x18;
    static constexpr uint8_t kSockCloseWait = 0x1C;
    static constexpr uint8_t kSockUdp = 0x22;
    static constexpr uint8_t kSockIpRaw = 0x32;

    // Sn_CR commands
    static constexpr uint8_t kCmdOpen = 0x01;
    static constexpr uint8_t kCmdListen = 0x02;
    static constexpr uint8_t kCmdConnect = 0x04;
    static constexpr uint8_t kCmdDiscon = 0x08;
    static constexpr uint8_t kCmdClose = 0x10;
    static constexpr uint8_t kCmdSend = 0x20;
    static constexpr uint8_t kCmdSendMac = 0x21;
    static constexpr uint8_t kCmdSendKeep = 0x22;
    static constexpr uint8_t kCmdRecv = 0x40;

    // Sn_IR bits
    static constexpr uint8_t kIrCon = 0x01;
    static constexpr uint8_t kIrDiscon = 0x02;
    static constexpr uint8_t kIrRecv = 0x04;
    static constexpr uint8_t kIrTimeout = 0x08;
    static constexpr uint8_t kIrSendOk = 0x10;

    // Sn_MR protocol
    static constexpr uint8_t kModeClosed = 0;
    static constexpr uint8_t kModeTcp = 1;
    static constexpr uint8_t kModeUdp = 2;
    static constexpr uint8_t kModeIpRaw = 3;

    /// Largest TCP payload per receive packet (the chip's MSS for Ethernet)
    static constexpr uint32_t kTcpMss = 1460;

    /// Bytes a TCP socket may hold beyond its receive memory while the program
    /// catches up; more resets the connection (the host has no window to shrink)
    static constexpr size_t kMaxTcpBacklog = 4u << 20;

    explicit W5300(VirtualNetwork* network = nullptr);
    ~W5300() override;

    /// The virtual network the sockets use (nullptr: no network, connects fail)
    void SetNetwork(VirtualNetwork* network);

    /// Hardware or software reset: every register to its reset value, every
    /// socket closed
    void Reset();

    uint8_t Read(uint16_t address);
    void Write(uint16_t address, uint8_t value);

    /// /INT pin state: (IR & IMR) != 0
    bool InterruptActive() const;

    // INetGuest
    void OnNetEvent(uint32_t cookie, NetEventType type, NetEventStatus status, const NetEndpoint& peer,
                    const uint8_t* data, uint32_t length, uint32_t source) override;

    // --- TTD state (netstate.h, network adapters TDD §6.3 option A) --------

    /// Registers, socket states, unsent bytes and references to the received
    /// bytes. Returns false when something did not fit the limits
    bool SaveState(netstate::Adapters& out) const;

    /// Takes a received byte range from the TTD journal: fills `out` with
    /// `length` bytes of record `source` from `offset`; false when unknown
    using ByteSource = std::function<bool(uint32_t source, uint32_t offset, uint32_t length, std::vector<uint8_t>& out)>;

    /// Restore from SaveState; received bytes come from `bytes`. Returns false
    /// when a referenced range could not be found (filled with zeros)
    bool LoadState(const netstate::Adapters& in, const ByteSource& bytes);

    // --- State for automation, debugging and tests -----------------------

    struct SocketView
    {
        uint8_t mode = 0;          ///< Sn_MR protocol bits
        uint8_t state = 0;         ///< Sn_SSR
        uint8_t ir = 0;            ///< Sn_IR
        uint16_t sourcePort = 0;
        NetEndpoint destination;
        uint32_t txFree = 0;
        uint32_t rxReceived = 0;   ///< Sn_RX_RSR
        size_t tcpBacklog = 0;     ///< bytes waiting for receive memory
        uint16_t networkSocket = 0;
    };
    SocketView GetSocket(int n) const;
    const std::array<uint8_t, 256>& CommonRegisters() const { return _common; }

private:
    struct Socket
    {
        uint8_t mr[2] = {};        ///< Sn_MR: [0] = bits 15..8, [1] = bits 7..0
        uint8_t imr = 0xFF;
        uint8_t ir = 0;
        uint8_t ssr = kSockClosed;
        uint8_t portr[2] = {};
        uint8_t dhar[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        uint8_t dportr[2] = {};
        uint8_t dipr[4] = {};
        uint8_t mssr[2] = {};
        uint8_t kpalvtr = 0;
        uint8_t protor = 0;
        uint8_t tosr = 0;
        uint8_t ttlr = 0x80;
        uint8_t fragr = 0x40;
        uint32_t wrsr = 0;         ///< Sn_TX_WRSR, 17 bits

        std::vector<uint8_t> tx;   ///< bytes written through TX_FIFOR, in order
        uint8_t txLatch = 0;       ///< FIFOR0 written, waiting for FIFOR1
        bool txHalf = false;

        /// Receive memory: packets (PACKET-INFO + data + pad), each knowing
        /// where its bytes came from; rxRead bytes of the first are consumed
        struct RxPacket
        {
            uint8_t header[8] = {};
            uint8_t headerLength = 0;
            std::vector<uint8_t> data;
            uint32_t source = 0;
            uint32_t sourceOffset = 0;
            uint32_t Size() const { return headerLength + static_cast<uint32_t>(data.size() + (data.size() & 1)); }
        };
        std::deque<RxPacket> rx;
        uint32_t rxRead = 0;
        uint32_t rxBytes = 0;          ///< Sn_RX_RSR: bytes in rx not yet consumed

        /// TCP bytes not yet placed into receive memory, per arriving chunk
        struct Chunk
        {
            std::vector<uint8_t> data;
            uint32_t source = 0;
            uint32_t sourceOffset = 0; ///< offset of data[0] in the source record
        };
        std::deque<Chunk> backlog;
        size_t backlogBytes = 0;

        uint32_t txSize = 8192;
        uint32_t rxSize = 8192;
        uint16_t net = 0;          ///< virtual-network socket (0 = none)
    };

    uint8_t ReadCommon(uint16_t offset) const;
    void WriteCommon(uint16_t offset, uint8_t value);
    uint8_t ReadSocket(int n, uint16_t offset);
    void WriteSocket(int n, uint16_t offset, uint8_t value);

    void Command(int n, uint8_t cmd);
    void Open(int n);
    void Send(int n);
    void Release(int n);           ///< drop the virtual-network socket
    void SetIr(int n, uint8_t bits);
    void FillFromBacklog(int n);
    void QueuePacket(int n, const uint8_t* header, size_t headerLength, const uint8_t* data, size_t length,
                     uint32_t source, uint32_t sourceOffset);
    static uint8_t RxByteAt(const Socket& s, uint32_t position);
    static void RxConsume(Socket& s, uint32_t count);
    static void ClearRx(Socket& s);
    uint32_t TxFree(const Socket& s) const;
    NetEndpoint Destination(const Socket& s) const;
    void SetDestination(Socket& s, const NetEndpoint& peer);

    VirtualNetwork* _network = nullptr;
    std::array<uint8_t, 256> _common{};
    std::array<Socket, kSockets> _sockets;
};
