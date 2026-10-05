#pragma once

/// @file trafficpanelmodel.h
/// @brief Qt-free logic of the Network traffic window (network #91 T4): the traffic report (TrafficAccess::Records,
/// the same tree every automation interface reads) turned into table rows, the filter the window applies, a decode of
/// one packet (Ethernet / ARP / IPv4 / ICMP / UDP / TCP / DHCP / DNS, or a socket operation) and its hex dump. The
/// window (trafficwindow.cpp) only draws these.

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

struct TrafficRow
{
    uint64_t index = 0;
    uint64_t frame = 0;          ///< the TTD position: machine frame ...
    uint64_t tInFrame = 0;       ///< ... and TTD units into it
    uint64_t timeUs = 0;         ///< emulated microseconds
    bool frameKind = true;       ///< an Ethernet frame (false: a socket operation)
    bool out = false;            ///< from the adapter
    std::string adapter, summary, op, peer, proto;
    uint64_t socket = 0;
    std::vector<uint8_t> bytes;
};

/// The records of a traffic report, oldest first
std::vector<TrafficRow> TrafficRows(const StateNode& report);

/// The window's filter: every word must appear in the row's adapter, direction, operation or summary (case-blind);
/// `kind`: 0 any, 1 frames, 2 socket operations
bool TrafficRowMatches(const TrafficRow& row, const std::string& words, int kind);

/// "f 4504  +2.40 ms": the frame, and the time since `previousUs` (the row above; 0 = none)
std::string TrafficTimeText(const TrafficRow& row, uint64_t previousUs);

/// A decode tree for one row: a node per layer, its fields as children
struct TrafficDecodeNode
{
    std::string text;
    std::vector<TrafficDecodeNode> children;
};
std::vector<TrafficDecodeNode> TrafficDecode(const TrafficRow& row);

/// The classic hex dump: offset, 16 bytes, their printable characters
std::string TrafficHexDump(const std::vector<uint8_t>& bytes);
