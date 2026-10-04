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

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "emulator/io/serial/uart16550.h"

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
constexpr int kMaxComBytes = 4096;      ///< COM port: loopback echo / unsent bytes kept
constexpr int kMaxComRuns = 256;        ///< COM port: runs of received bytes by journal reference

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
    uint8_t proto, connected, hasGuest, reserved;   ///< hasGuest: 0 none, 1 the card, 2 the #xxEF port peer, 3 the machine serial port peer, 4 the ATM2IOESP peer, 5 the ZiFi peer, 6 the Ethernet gateway, 7 / 8 the UART card in expansion slot 1 / 2, 9 / 10 its second UART (SprinterSerial)
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

// --- Emulated ESP module (step N3): its socket stack -----------------------

constexpr int kEspSlots = 8;          ///< ESPNET: 8 sockets on ESP32 (4 on ESP8266); AT: 5 links + its server
constexpr int kEspRuns = 64;          ///< runs of unread TCP bytes per slot
constexpr int kEspDatagrams = 16;     ///< unread UDP datagrams per slot
constexpr int kEspPending = 8;        ///< clients queued on a server slot
constexpr int kEspPendingRuns = 16;   ///< runs of bytes a queued client already sent
constexpr int kEspClose = 16;         ///< sockets to close at the next frame boundary

struct EspDatagram
{
    uint32_t fromAddr;
    uint16_t fromPort, reserved;
    Reference data;         ///< one datagram is one journal record
};

struct EspPending
{
    uint16_t vnetId, peerPort;
    uint32_t peerAddr;
    uint8_t finSeen, reserved;
    uint16_t rxRuns;
    Reference rx[kEspPendingRuns];
};

struct EspSlot
{
    uint8_t state, connecting, finSeen, datagramCount, pendingCount, reserved[3];
    uint16_t vnetId, localPort, remotePort, waitingId;
    uint32_t remoteAddr;
    uint16_t rxRuns, reserved2;
    Reference rx[kEspRuns];
    EspDatagram datagrams[kEspDatagrams];
    EspPending pending[kEspPending];
};

struct EspStackState
{
    uint8_t slotCount, resolving, closeCount, rearmMask;
    uint16_t dnsSocket, dnsId, dnsSeq, pingSocket, querySocket, reserved;
    char resolveName[68];
    uint16_t closeLater[kEspClose];
    EspSlot slotStates[kEspSlots];   ///< not "slots": a Qt macro, and the GUI includes this header
};

constexpr int kEspRxBytes = 6144;     ///< bytes from the ZX not parsed yet (the 4 KB ring + a frame in progress)
constexpr int kEspOutBytes = 8192;    ///< reply bytes waiting for the ZX
constexpr int kEspFirmware = 256;     ///< the firmware's own state (EspnetModule / AtModule)

/// An emulated ESP module (EspModule)
struct EspModuleState
{
    uint8_t present, chip, wifi, lineMismatch;
    uint8_t mac[6], flowControl;
    uint8_t pins;           ///< bit 0 RST held, bit 1 GPIO0 low, bit 2 the ROM's download mode (0 in older blobs)
    char ssid[36];
    uint32_t ip, baud, pendingBaud, rxLength, outLength;
    uint32_t factoryBaud;   ///< the rate a hardware reset returns to (0 in older blobs: keep the module's)
    uint64_t wifiAt, pendingBaudAt, outReadyAt, requests;
    uint8_t rx[kEspRxBytes];
    uint8_t out[kEspOutBytes];
    uint8_t firmware[kEspFirmware];
    EspStackState stack;
};

/// An emulated Hayes modem (HayesModemPeer, network tdd §10): command state, S-registers, the replies waiting
/// for the ZX, the call (its link is the StreamLink + runs of Com), the escape and ring timers. The data link's
/// received bytes are journal references (Com::runs) like a stream peer's
constexpr int kModemCommand = 256;    ///< command line being typed / the last one (A/)
constexpr int kModemOut = 2048;       ///< result codes and echo waiting for the ZX
constexpr int kModemSRegs = 40;
constexpr int kModemCloses = 8;

struct HayesModemState
{
    uint8_t present, mode, echo, quiet;
    uint8_t verbose, resultSet, dcdMode, dtrMode;
    uint8_t dsrMode, dtr, plusCount, dropPending;
    uint8_t ringing, ri, offHook, reserved;
    uint8_t sregs[kModemSRegs];
    uint16_t commandLength, lastCommandLength, outLength, listenSocket;
    uint16_t ringSocket, closeCount, specPort, remotePort;
    uint16_t closeLater[kModemCloses];
    uint32_t specAddr, remoteAddr, lineBaud, ringCount;
    uint64_t lastDataAt, lastPlusAt, dialDeadline, nextRingAt, riOffAt, escapedAt;
    uint64_t dials, connects, failures, escapes, rings, answered, bytesToLine, bytesFromLine;
    char specHost[128];
    char dialed[128];
    char lastResult[32];
    uint8_t command[kModemCommand];
    uint8_t lastCommand[kModemCommand];
    uint8_t out[kModemOut];
};

/// A COM port stream peer's link (StreamPeer): phase, sockets, DNS lookup,
/// the device's modem lines, the line format last asked of the device
struct StreamLink
{
    uint8_t phase;          ///< StreamPeer::Phase
    uint8_t closePending, connectPending, deviceLines;
    uint8_t rts, dtr;
    uint8_t lineDataBits, lineParity, lineStopBits;
    uint8_t reserved[3];
    uint16_t socket, dnsSocket, dnsId, dnsSeq;
    uint32_t resolvedAddr, retryFrames, waitFrames, lineBaud;
    uint64_t bytesIn, bytesOut;
};

/// The COM port (network TDD §7): the UART and its peer
struct Com
{
    uint8_t present;        ///< a COM port was fitted at capture
    uint8_t peerKind;       ///< 1 loopback, 2 tcp, 3 serial, 4 ESPNET module, 5 AT module, 6 Hayes modem
    uint8_t reserved[2];
    StreamLink link;        ///< stream peers
    Uart16550::State uart;
    uint32_t loopbackLength;
    uint8_t loopback[kMaxComBytes];   ///< the echo queue: bytes the ZX wrote
    uint32_t runCount;
    Reference runs[kMaxComRuns];      ///< stream peer: bytes waiting for the ZX, by journal reference
    uint32_t unsentLength;
    uint8_t unsent[kMaxComBytes];     ///< stream peer: bytes the ZX sent, not yet flushed
    union
    {
        EspModuleState esp;           ///< peerKind 4 (ESPNET) / 5 (AT)
        HayesModemState modem;        ///< peerKind 6 (the modem's link is `link` + `runs` above)
    };
};
static_assert(sizeof(HayesModemState) <= sizeof(EspModuleState), "the modem shares the ESP module's bytes: the blob size stays");

struct Adapters
{
    uint32_t version;       ///< kVersion
    uint8_t present;        ///< a card was fitted at capture
    uint8_t incomplete;     ///< some state did not fit the limits above
    uint8_t p83, p82, p81;
    uint8_t networkPresent; ///< the virtual network existed (a card or a COM stream peer)
    uint8_t reserved[2];
    uint8_t common[256];
    W5300Socket sockets[kSockets];
    VirtualNetwork network;
};

constexpr uint32_t kVersion = 3;   ///< 2: the COM port; 3: the COM port in its own blob (SerialPort)

/// The machine's serial port on #xxEF (PeripheralId::SerialPort). Without a
/// peer (ZX-Evo, ComPort=NONE: the AVR's UART on its own) only the header and
/// the UART are written: the blob is that short
struct SerialPort
{
    uint32_t version;       ///< kSerialPortVersion
    uint8_t incomplete;     ///< some peer state did not fit the limits above
    uint8_t reserved[3];
    Com com;                ///< com.uart always; the peer part only in the full size
};
constexpr uint32_t kSerialPortVersion = 1;
constexpr size_t kSerialPortShortSize = offsetof(SerialPort, com) + offsetof(Com, uart) + sizeof(Uart16550::State);
static_assert(std::is_trivial_v<SerialPort>, "the serial port blob is cleared and copied as bytes");

static_assert(std::is_trivial_v<Adapters>, "the network state blob is cleared and copied as bytes");

} // namespace netstate
