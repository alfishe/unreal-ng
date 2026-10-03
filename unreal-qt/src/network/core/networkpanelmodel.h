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
    bool internalIo = false;           ///< the ATM Turbo 2+ INTERNAL I/O connector (ATM2IOESP)
    std::string serialPort = "none";   ///< none | evo-avr | zifi | atm2-kbc
    bool zifiMachine = false;          ///< the TS AVR firmware's ZiFi (TS-Conf, ZX-Evo + a TS firmware)

    // ZX-Bus cards
    bool zxNetUsb = false;
    bool zxWifi = false;
    bool atm2IoEsp = false;            ///< not on the ZX-Bus: the ATM Turbo 2+ INTERNAL I/O connector
    ComPortSpec atm2IoEspPeer;         ///< default AT
    unsigned atm2IoEspAddress = 0xF0;  ///< #F0 (Rev 1.5 / 2.0) or #F8 (Rev 1.0)

    ComPortSpec zifiPeer;              ///< the ZiFi board's ESP; kind None = no board (default)

    // Serial ports: the machine's own and the ZX-WiFi card's
    ComPortSpec comPort;               ///< kind None = nothing on the line
    ComPortSpec zxWifiPeer;            ///< default AT
    std::string espChip = "ESP32";     ///< ESP32 | ESP8266 | ESP8266-AT221 | ESP8266-AT222
    bool modemLines = false;
    std::string avrFirmware = "BASE2023";
    std::string kbcFirmware;           ///< [ATM] Kbc= name; empty: no controller socket

    // UART cards in expansion slots (the Sprinter's SprinterESP): which slot holds one, what its 16550 is wired to
    bool slotUart[2] = {false, false};
    ComPortSpec slotPeer[2];            ///< default AT (the card's ESP-12F)

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
/// (card, com_port, zx_wifi, atm2ioesp, atm2ioesp_address, zifi, esp_chip, isa1_peer, isa2_peer, com_modem_lines,
/// avr_firmware, kbc_firmware,
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
    bool atm2IoEsp = true;      std::string atm2IoEspWhy;
    bool zifi = true;           std::string zifiWhy;
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

/// One expansion slot (the Sprinter's ISA slots) as the window shows it: what is plugged and what it uses, in one
/// line ("NE2000 RTL8019AS, I/O #300-#31F, IRQ 3, MAC 02:53:50:00:00:02, cable: ethernet-gateway"), or why a
/// configured card is not there. Built from the report's `slots` (network tdd §14)
struct NetworkSlotRow
{
    std::string id;      ///< "isa2"
    std::string label;   ///< "ISA slot 2 (J7), page #D6"
    std::string line;
};
std::vector<NetworkSlotRow> NetworkSlotRows(const StateNode& network);
