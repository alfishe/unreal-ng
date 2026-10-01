#pragma once

/// @file comportspec.h
/// @brief [NETWORK] ComPort= value (network adapters TDD §8): which peer sits
/// on the COM port's other end. One parser for the INI, NetworkManager and
/// every automation surface.
///
///   NONE (or empty)              no COM port
///   LOOPBACK                     every byte comes back
///   TCP:<host>:<port>            a host TCP endpoint (telnet BBS, harness); the
///                                host is an IPv4 address or a name, resolved
///                                through the virtual network's DNS (Hosts=
///                                first, then the host resolver) at each connect
///   SERIAL:<device>[,<baud>]     a host serial device; <baud> until the ZX
///                                programs its own rate (115200 by default)
///   ESPNET                       an emulated ESP module with NedoOS's ESPNET
///                                firmware (binary socket protocol)
///   AT                           an emulated ESP module with Espressif's AT firmware
/// The ESP modules use the virtual network for their sockets; [NETWORK]
/// EspChip= picks ESP32 (8 sockets) or ESP8266 (4)

#include <cstdint>
#include <string>

struct ComPortSpec
{
    enum class Kind : uint8_t
    {
        None,
        Loopback,
        Tcp,
        Serial,
        Espnet,
        At
    };
    Kind kind = Kind::None;
    std::string host;         ///< Tcp: the name or the address as written
    uint32_t addr = 0;        ///< Tcp: IPv4, host order; 0 = `host` is a name to resolve
    uint16_t port = 0;        ///< Tcp
    std::string device;       ///< Serial
    uint32_t baud = 115200;   ///< Serial

    static bool Parse(const std::string& text, ComPortSpec& out, std::string& error);
    /// Back to the INI form
    std::string ToString() const;
};
