#pragma once

/// @file hostnet.h
/// @brief What the virtual network asks of the host network (network adapters
/// TDD §3). Commands are facts, not inputs: they are issued only while the
/// machine runs live, never during a TTD replay. Everything the host answers
/// comes back as HostNetEvent through PollEvent.
///
/// Socket ids: the virtual network allocates ids 1..0x7FFF; ids of
/// connections the host accepted for a guest server are allocated by the host
/// side from 0x8000 up and announced in an Accepted event.

#include <string>

#include "common/network/nettypes.h"

class IHostNet
{
public:
    static constexpr uint16_t kFirstAcceptedId = 0x8000;

    /// Start a TCP connection; Connected or ConnectFailed follows
    virtual void TcpConnect(uint16_t socket, const NetEndpoint& to) = 0;

    /// Queue bytes on a connected TCP socket (the host keeps them until sent)
    virtual void TcpSend(uint16_t socket, const uint8_t* data, uint32_t length) = 0;

    /// Half-close: send FIN after the queued bytes
    virtual void TcpShutdownWrite(uint16_t socket) = 0;

    /// Listen on 127.0.0.1:hostPort for a guest server; every client arrives as
    /// Accepted(listener = socket, new id in the payload). ListenFailed if the
    /// port is taken.
    virtual void TcpListen(uint16_t socket, uint16_t hostPort) = 0;

    /// Send one UDP datagram (the host socket is opened on first use); replies
    /// arrive as Datagram events for the same socket
    virtual void UdpSend(uint16_t socket, const NetEndpoint& to, const uint8_t* data, uint32_t length) = 0;

    /// Answer a DNS query (a UDP payload the guest sent to `server`) from the
    /// host resolver; the reply arrives as a Datagram from `server`
    virtual void DnsQuery(uint16_t socket, const NetEndpoint& server, const uint8_t* query, uint32_t length) = 0;

    /// ICMP echo: `data` is the whole echo request; an EchoReply event (peer =
    /// `to`, payload = the whole reply) follows if the host got an answer.
    /// Nothing follows on timeout or when the host does not allow ICMP
    virtual void IcmpEcho(uint16_t socket, const NetEndpoint& to, const uint8_t* data, uint32_t length) = 0;

    /// Open a host serial device (a real ESP on USB, a modem) raw, 8N1 at
    /// `baud`: Connected or ConnectFailed follows, then Data events with what
    /// the device sent, Reset when it fails (unplugged). Bytes go out with
    /// TcpSend (the stream send of this socket). RTS / DTR stay as the OS set
    /// them: many USB ESP boards wire them to the module's reset and boot pins
    virtual void SerialOpen(uint16_t socket, const std::string& device, uint32_t baud) = 0;

    /// Change an open serial device's line format (the ZX reprogrammed its UART)
    virtual void SerialConfigure(uint16_t socket, const SerialLine& line) = 0;

    /// Drive the device's RTS / DTR, and from now on report its CTS / DSR /
    /// RI / DCD as ModemLines events (only when the machine config asks for
    /// modem lines: see [NETWORK] ComModemLines)
    virtual void SerialModemLines(uint16_t socket, bool rts, bool dtr) = 0;

    /// Forget a socket (closes the host socket, drops queued data)
    virtual void Close(uint16_t socket) = 0;

    /// Close everything (machine reset, adapter removed)
    virtual void CloseAll() = 0;

    /// Next event, if any (called on the emulation thread)
    virtual bool PollEvent(HostNetEvent& out) = 0;

    virtual ~IHostNet() = default;
};
