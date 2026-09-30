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
///   ESPNET, AT                   ESP modules (step N3): refused until then

#include <cstdint>
#include <string>

struct ComPortSpec
{
    enum class Kind : uint8_t
    {
        None,
        Loopback,
        Tcp,
        Serial
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
