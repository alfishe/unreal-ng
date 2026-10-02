#pragma once

/// @file networkpanelmodel.h
/// @brief Qt-free logic of the Network window: the network state report
/// (DeviceState::Network, the same tree every automation interface reads)
/// turned into the form the window edits, which controls the machine allows
/// and why not, and the edited form back into NetworkManager settings
/// (ParseChange key / value pairs, only those that changed). The window
/// (networkwindow.cpp) only draws these.
///
/// The machine decides what can be set (PortDecoder::DescribeNetwork): the
/// ZX-Bus takes cards, its own serial port (ZX-Evo: the AVR's; ATM Turbo 2+:
/// the keyboard controller's RS-232) takes a peer, the AVR firmware exists
/// only on a ZX-Evo, the keyboard controller firmware only on an ATM Turbo 2+
/// v7.xx. Network TDD §8, tdd-atm2-kbc.md §8.

#include <string>
#include <utility>
#include <vector>

#include "emulator/io/serial/comportspec.h"
#include "emulator/state/statenode.h"

/// Everything the window edits, as the settings in force describe it
struct NetworkForm
{
    // What the machine offers (read-only)
    bool zxBus = true;
    std::string serialPort = "none";   ///< none | evo-avr | zifi | atm2-kbc

    // ZX-Bus cards
    bool zxNetUsb = false;
    bool zxWifi = false;

    // Serial ports: the machine's own and the ZX-WiFi card's
    ComPortSpec comPort;               ///< kind None = nothing on the line
    ComPortSpec zxWifiPeer;            ///< default AT
    std::string espChip = "ESP32";     ///< ESP32 | ESP8266
    bool modemLines = false;
    std::string avrFirmware = "BASE2023";
    std::string kbcFirmware;           ///< [ATM] Kbc= name; empty: no controller socket

    // Virtual network
    bool hostAccess = true;
    std::string dnsMode = "HOST";      ///< HOST | PASS
    std::string hosts;
    std::string forwards;
    unsigned connectTimeoutMs = 10000;
};

/// The form from a network state report (its "machine" and "settings")
NetworkForm NetworkFormFromState(const StateNode& network);

/// The settings that differ between two forms, as ParseChange takes them
/// (card, com_port, zx_wifi, esp_chip, com_modem_lines, avr_firmware, kbc_firmware,
/// host_access, dns_mode, hosts, forwards, connect_timeout_ms)
std::vector<std::pair<std::string, std::string>> NetworkFormChanges(const NetworkForm& before, const NetworkForm& after);

/// Which controls the machine allows; a reason for each one it does not
struct NetworkAvailability
{
    bool cards = true;          std::string cardsWhy;
    bool zxWifi = true;         std::string zxWifiWhy;
    bool comPort = true;        std::string comPortWhy;
    bool avrFirmware = true;    std::string avrFirmwareWhy;
    bool kbcFirmware = true;    std::string kbcFirmwareWhy;
};
NetworkAvailability NetworkFormAvailability(const NetworkForm& form);

/// A peer is an emulated ESP module (the chip setting applies to it)
bool NetworkPeerIsEsp(const ComPortSpec& peer);

/// [EVO] Avr= presets, oldest first, with a line for people:
/// {"BASE2010", "NedoPC 2010: a register file, no transfer"}, ...
std::vector<std::pair<std::string, std::string>> NetworkAvrFirmwareChoices();

/// [ATM] Kbc= presets, oldest first, with a line for people:
/// {"NONE", "no controller: #FE is the plain matrix port"}, {"V22-7", ...}, ...
std::vector<std::pair<std::string, std::string>> NetworkKbcFirmwareChoices();

/// The baud rates offered for a SERIAL: peer (any other can be typed)
std::vector<uint32_t> NetworkSerialBaudChoices();
