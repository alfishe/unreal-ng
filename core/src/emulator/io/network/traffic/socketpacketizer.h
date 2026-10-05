#pragma once

/// @file socketpacketizer.h
/// @brief Socket operations as packets Wireshark decodes (network #91 T2, owner Q2): a socket adapter (W5300, an ESP
/// module, ZiFi, the modem) has no frames, only operations and bytes. For the pcapng each operation becomes Ethernet +
/// IPv4 + TCP / UDP / ICMP between the adapter and its peer, with sequence numbers kept per socket, so HTTP, DNS and
/// "Follow TCP Stream" work:
///
///   connect (out)         -> SYN                      adapter -> peer
///   connected (in)        -> SYN-ACK, ACK
///   accepted (in, a guest server) -> SYN from the peer, SYN-ACK, ACK
///   send (out) / data (in) -> data segments (split at 1460 bytes), PSH-ACK
///   shutdown / close (out) -> FIN-ACK; peer-closed (in) -> FIN-ACK from the peer
///   reset / connect-failed (in) -> RST-ACK from the peer
///   sendto (out) / datagram (in) -> one UDP datagram; echo / echo-reply -> the ICMP message as it is
///
/// The bytes are exact. The TCP framing (segment sizes, windows, the handshake's timing) is synthetic: the host's
/// stack did the real one. The adapter's address is synthetic too - 10.0.2.15 and up, one per adapter name (an
/// adapter's own configured address is not known here) - and its local port is the socket's (49152 + socket id when
/// the operation does not name one). Serial sockets (a host serial device) and listen / modem-line events give no
/// packet.

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "emulator/io/network/traffic/networktraffictap.h"

class SocketPacketizer
{
public:
    /// The Ethernet frames for one socket operation (empty: nothing to show)
    std::vector<std::vector<uint8_t>> Packets(const TrafficRecord& r);
    void Reset();

    /// The synthetic address of an adapter (10.0.2.15 for the first one seen)
    uint32_t AdapterAddress(const std::string& adapter);

private:
    struct Conn
    {
        uint32_t mySeq = 1000;      ///< the adapter's next sequence number
        uint32_t peerSeq = 50000;   ///< the peer's next sequence number
        bool finSent = false;       ///< shutdown then close: one FIN
    };
    std::map<std::pair<std::string, uint16_t>, Conn> _conns;
    std::map<std::string, uint32_t> _addresses;
};
