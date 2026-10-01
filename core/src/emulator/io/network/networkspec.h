#pragma once

/// @file networkspec.h
/// @brief [NETWORK] Card= values (network adapters TDD §8): which ZX-Bus
/// cards are plugged. One parser for the INI, NetworkManager and every
/// automation surface.
///
///   NONE (or empty)           no card
///   ZXNETUSB                  NedoPC ZXNETUSB (WIZnet W5300), ports #xxAB
///   ZXWIFI                    ZX-WiFi (a 16550 + an ESP module), ports #F8EF..#FFEF
///   ZXNETUSB,ZXWIFI           both (',': ';' starts an INI comment)

#include <cstdint>
#include <string>

namespace networkspec
{
constexpr uint8_t kCardZxNetUsb = 0x01;
constexpr uint8_t kCardZxWifi = 0x02;

bool ParseCards(const std::string& text, uint8_t& mask, std::string& error);
std::string CardsToString(uint8_t mask);
}  // namespace networkspec
