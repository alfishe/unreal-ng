#pragma once

/// @file ethernetaccess.h
/// @brief The Ethernet frames of the frame-level cards for every automation interface (network tdd §14): the capture
/// the gateway keeps (the last 256 frames, both ways) as a report or a pcap file, and a frame injected towards a card
/// as if from the wire. WebAPI `GET /network/frames`, `POST /network/frame`; CLI `network frames`, `network frame`;
/// Lua / Python `network_frames()`, `network_inject_frame()`; MCP through `invoke_api`. One source for all of them.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;

namespace EthernetAccess
{
/// The captured frames (`link` = a port key such as "isa2.eth", empty = every card; the newest `last`, 0 = all):
/// index, frame, direction (to_card / from_card), port, length, ethertype, a one-line summary, hex
StateNode Frames(EmulatorContext* context, const std::string& link, unsigned last);

/// The capture as a pcap file (LINKTYPE_ETHERNET); false with `error` without a gateway
bool Pcap(EmulatorContext* context, const std::string& link, std::vector<uint8_t>& out, std::string& error);

/// A frame towards the card `link` (hex digits, spaces allowed): queued like a frame from the wire, offered at the
/// next frame boundary. A tool edit while TTD records (not replayable input). False with `error`
bool Inject(EmulatorContext* context, const std::string& link, const std::string& hex, const char* source, std::string& error);

/// The host's network adapters for the bridge (network SN6): name, description, IPv4 addresses, loopback, wireless,
/// up, running, and whether the bridge can take it (wired: yes; Wi-Fi: not yet, Q1); `library` and `error` when the
/// packet library is missing or refuses
StateNode Adapters();

/// A one-line summary of a frame ("ARP who-has 10.0.2.2 tell 10.0.2.15", "IPv4 10.0.2.15:1025 > 192.0.2.10:80 TCP S")
std::string Summary(const std::vector<uint8_t>& frame);
}  // namespace EthernetAccess
