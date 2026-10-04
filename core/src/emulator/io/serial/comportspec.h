#pragma once

/// @file comportspec.h
/// @brief [NETWORK] ComPort= value (network adapters TDD §8): which peer sits
/// on the COM port's other end. One parser for the INI, NetworkManager and
/// every automation surface.
///
///   NONE (or empty)              no COM port
///   LOOPBACK                     every byte comes back; the peer holds CTS / DSR / DCD active
///   PLUG                         a loopback test plug: every byte comes back and the ZX's own RTS drives
///                                CTS, DTR drives DSR and DCD (an RS-232 loopback connector); with no peer
///                                at all the inputs read inactive, as an open RS-232 receiver input
///   TCP:<host>:<port>            a host TCP endpoint (telnet BBS, harness); the
///                                host is an IPv4 address or a name, resolved
///                                through the virtual network's DNS (Hosts=
///                                first, then the host resolver) at each connect
///   SERIAL:<device>[,<baud>]     a host serial device; <baud> until the ZX
///                                programs its own rate (115200 by default)
///   ESPNET[,<baud>]              an emulated ESP module with NedoOS's ESPNET
///                                firmware (binary socket protocol)
///   AT[,<baud>]                  an emulated ESP module with Espressif's AT firmware
///   MODEM[,<guest port>]         an emulated Hayes modem (network tdd §10): AT commands, ATD dials a
///                                host:port / name / phone book entry ([NETWORK] ModemPhonebook) through the
///                                virtual network, CONNECT / NO CARRIER, +++, DCD / RI; with <guest port> it
///                                also answers calls a host client makes to that guest TCP port (Forward=)
/// The ESP modules use the virtual network for their sockets; [NETWORK]
/// EspChip= picks ESP32 (8 sockets) or ESP8266 (4). Their <baud> is the rate
/// the module's firmware was built for; without it the port's own (the ATM
/// Turbo 2+ keyboard controller: 38400, the ESPNET "ATM2COM" build; any
/// other port: 115200)

#include <cstdint>
#include <string>

struct ComPortSpec
{
    enum class Kind : uint8_t
    {
        None,
        Loopback,
        Plug,
        Tcp,
        Serial,
        Espnet,
        At,
        Modem
    };
    Kind kind = Kind::None;
    std::string host;         ///< Tcp: the name or the address as written
    uint32_t addr = 0;        ///< Tcp: IPv4, host order; 0 = `host` is a name to resolve
    uint16_t port = 0;        ///< Tcp; Modem: the guest port it answers calls on (0: none)
    std::string device;       ///< Serial
    uint32_t baud = 115200;   ///< Serial; Espnet / At: 0 = the port's default rate

    static bool Parse(const std::string& text, ComPortSpec& out, std::string& error);
    /// Back to the INI form
    std::string ToString() const;
};
