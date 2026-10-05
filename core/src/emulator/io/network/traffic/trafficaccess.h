#pragma once

/// @file trafficaccess.h
/// @brief The network traffic tap (network #91) for every automation interface: one source for the WebAPI
/// (`GET / POST .../network/traffic`), the CLI (`network traffic`), Lua / Python (`network_traffic`,
/// `network_traffic_pcapng`, `network_traffic_control`) and MCP (through `invoke_api`).

#include <cstdint>
#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class EmulatorContext;

namespace TrafficAccess
{
struct Query
{
    uint64_t since = 0;      ///< records with index >= since (poll: pass the previous next_index)
    std::string adapter;     ///< "isa2.eth", "zxnetusb", "com.esp" ... (empty = all)
    std::string kind;        ///< "frame" | "socket" | "" (all)
    unsigned last = 64;      ///< the newest N (0 = all in the ring)
};

/// The tap's state (ring, counters, the file) and the records the query picks
StateNode Records(EmulatorContext* context, const Query& query);

/// The ring's records as a pcapng file (Wireshark: one interface per adapter); false with `error` without a network
bool Pcapng(EmulatorContext* context, const Query& query, std::vector<uint8_t>& out, std::string& error);

/// `action`: "clear" (empty the ring), "start" (record into the pcapng file `path`, unbounded, until "stop"),
/// "stop", "ring" (`ringBytes`: the ring's budget). False with `error`
bool Control(EmulatorContext* context, const std::string& action, const std::string& path, uint64_t ringBytes,
             std::string& error);
}  // namespace TrafficAccess
