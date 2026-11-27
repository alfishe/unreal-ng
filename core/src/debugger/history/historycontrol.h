#pragma once
/// @file historycontrol.h
/// @brief The device history verbs of every automation plane (debugger/history/devicehistory.h): the WebAPI (and
/// MCP and the WebSocket through it), the CLI, Lua and Python turn their input into a HistoryRequest and the reply
/// into their output, so they cannot drift apart.
///
/// Verbs:
///   list    every history of the machine: device (registry id), what it records, mode, limit, counts, stream
///   read    one device's history: its stats and the newest `last` records (0: all), oldest first. With `since`
///           (a cursor: the number of the last record the reader has) the first `last` records numbered after it
///           instead, each with its "seq", and next_since (the cursor of the next read), missed (records after the
///           cursor no longer kept) and restarted (the cursor was past the newest record: read from the oldest kept)
///   config  mode off / ring / capture, limit (ring size; capture cap, 0 = none), stream (a JSON Lines file, "" =
///           none), streamLimit (records, 0 = none) and filter (which records are recorded at all, as
///           "side=host out=true port=#B3,#BB pc=#8000-#9FFF": historyfilter.h; "" = every record). Fields not
///           given keep their value
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
    size_t last = 0;                            ///< read: the newest N (0: all); with since: the first N after it
    std::optional<uint64_t> since;              ///< read: the records numbered after this cursor
    std::optional<std::string> mode;            ///< config: off, ring, capture
    std::optional<uint64_t> limit;
    std::optional<std::string> stream;
    std::optional<uint64_t> streamLimit;
    std::optional<std::string> filter;          ///< config: which records are recorded (historyfilter.h); "": all
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
