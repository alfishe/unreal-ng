#pragma once

/// @file networkspec.h
/// @brief [NETWORK] Card= values (network adapters TDD §8): which ZX-Bus
/// cards are plugged. One parser for the INI, NetworkManager and every
/// automation surface.
///
///   NONE (or empty)           no card
///   ZXNETUSB                  NedoPC ZXNETUSB (WIZnet W5300), ports #xxAB
///   ZXWIFI                    ZX-WiFi (a 16550 + an ESP module), ports #F8EF..#FFEF
///   ATM2IOESP                 ATM2IOESP (a TL16C550C + an ESP32) on the ATM Turbo 2+
///                             INTERNAL I/O connector: bus address by #FB, data by #FA
///   ZXNETUSB,ZXWIFI           a list (',': ';' starts an INI comment)

#include <cstdint>
#include <string>

namespace networkspec
{
constexpr uint8_t kCardZxNetUsb = 0x01;
constexpr uint8_t kCardZxWifi = 0x02;
constexpr uint8_t kCardAtm2IoEsp = 0x04;   ///< not a ZX-Bus card: the ATM Turbo 2+ INTERNAL I/O connector

bool ParseCards(const std::string& text, uint8_t& mask, std::string& error);
std::string CardsToString(uint8_t mask);
}  // namespace networkspec
