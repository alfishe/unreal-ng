#pragma once
/// @file historycontrol.h
/// @brief The device history verbs of every automation plane (debugger/history/devicehistory.h): the WebAPI (and
/// MCP and the WebSocket through it), the CLI, Lua and Python turn their input into a HistoryRequest and the reply
/// into their output, so they cannot drift apart.
///
/// Verbs:
///   list    every history of the machine: device (registry id), what it records, mode, limit, counts, stream
///   read    one device's history: its stats and the newest `last` records (0: all), oldest first
///   config  mode off / ring / capture, limit (ring size; capture cap, 0 = none), stream (a JSON Lines file, "" =
///           none) and streamLimit (records, 0 = none). Fields not given keep their value
///   clear   drops the records and the counts; the configuration stays
///   pause   holds the history: nothing recorded, the records kept (resume goes on)
///   resume
///
/// A config starts afresh: the records, the counts and the stream file (opened again, truncated).

#include <cstdint>
#include <optional>
#include <string>

#include "emulator/state/statenode.h"

class EmulatorContext;

struct HistoryRequest
{
    std::string verb;                           ///< list, read, config, clear, pause, resume
    std::string device;                         ///< the registry id: "beta128", "zxbus.1"
    size_t last = 0;                            ///< read: the newest N (0: all)
    std::optional<std::string> mode;            ///< config: off, ring, capture
    std::optional<uint64_t> limit;
    std::optional<std::string> stream;
    std::optional<uint64_t> streamLimit;
};

struct HistoryReply
{
    int httpStatus = 200;                       ///< 200, 400 (bad request), 404 (no such history)
    StateNode body;                             ///< the answer, or {"error": ...}
};

namespace HistoryControl
{
HistoryReply Execute(EmulatorContext* context, const HistoryRequest& request);
}
