#pragma once

/// @file monitoringapi.h
/// @brief Monitoring on every automation plane, in one place (automation planes guidelines §1; Stream Deck use case
/// C10): what the host process uses, how fast each instance runs, who is connected, and a summary per instance of the
/// state a monitoring page shows (breakpoints, media, recording, labels, TTD, profilers, controllers). Read-only.
///
/// Each name below is at once a verb (WebAPI `GET /api/v1/monitor/{name}`, and so the WebSocket `call`; the CLI
/// `monitor <name>`; Lua / Python `monitor(name)`; MCP `inspect_state` aspect `monitor`), a WebSocket state topic of
/// /api/v1/ws (latest value, coalesced at the subscriber's `max_hz`) and a surface readout. The value is the same
/// bytes everywhere: the topic pushes what the verb returns, without `ok` / `instance`.
///
/// | Name | Of | Value |
/// |:--|:--|:--|
/// | `host.stats` | the process | available, cpu_percent (100 = one core), cpu_percent_host (of all cores), cores, rss_bytes, peak_rss_bytes, virtual_bytes, threads, cpu_time_s, uptime_s, interval_s |
/// | `host.clients` | the process | per plane (rest, ws, events, debug_ws, mcp, cli): connections, opened, requests; companions (paired), instances |
/// | `emulator.stats` | an instance | state, measuring, fps, nominal_fps, speed_percent, frame_time_us {avg, max}, frame_budget_percent, halted_percent, interrupts_per_frame, frame, t_states, turbo, cpu {mhz, base_mhz, multiplier (host x hardware), host_multiplier, hw_turbo_ratio} |
/// | `debug.breakpoints` | an instance | available, total, active, inactive, owned, by_kind {memory, exec, read, write, port, port_in, port_out, keyboard, nextreg, beam, copper, magic}, groups [{name, total, active}], last_hit {id, type, address, access, group, note} or null |
/// | `media.slots` | an instance | available, slots [{id, kind, present, pending, name, format, dirty, changed_units, protect}], loaded, dirty (any) |
/// | `recording.state` | an instance | available, enabled, recording, paused, mode, file, frames, duration_s, emulated_s, file_bytes, fps |
/// | `debug.labels` | an instance | available, total, sets [{id, title, enabled, symbols}] |
/// | `ttd.status` | an instance | available, state, recording, history, frame, first, end, frames; checkpoints, memory_bytes, history_bytes, page_store_bytes, bookmarks, write_journal_records, input_events, evicted_checkpoints (those refreshed every 2 s) |
/// | `debug.buffers` | an instance | How full each trace buffer is, at most once a second: `opcode`, `calltrace`, `calltrace_hot`, `porttrace`, `gsporttrace`, `pchist`, `aylog`, `coverage` (addresses run of 65536) each {used, capacity, slot_bytes, used_bytes, capacity_bytes, percent} (a buffer not allocated is absent); `ttd` {history_bytes, limit_bytes, limit_frames, memory_bytes, checkpoints, write_journal_records, write_journal_bytes} |
/// | `debug.analyzers` | an instance | opcode, memory, calltrace (stopped / capturing / paused), coverage {available, recording, instructions}, pchist (armed / off) |
/// | `controllers.state` | an instance | available, connectors [{id, kind, device, plugged, fixed}], plugged |
/// | `analysis.progress` | an instance | The `analysis build` job (analysisjobs.h): job, state (none, running, done, failed, cancelled), frames_done, frames_total, percent, error |
/// | `analysis.ready` | an instance | The L1 the last build made: ready, job, routines, blocks, loops, instructions, frames, checked, diverged, path |
///
/// One more verb, not a topic: `breakpoint_groups` (instance) {groups: [{name, total, active, ids}]}, also WebAPI
/// `GET /api/v1/emulator/{id}/breakpoints/groups`, Lua / Python `bp_groups()`.
///
/// Reads are published state only: never a pause, never a wait for the emulation thread, no work proportional to the
/// machine (a short lock of a manager at most). The `all` verb returns the process values and every instance's.

#include <string>
#include <string_view>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

namespace monitoring
{

struct Reply
{
    int status = 200;                      ///< 200, 400 Bad Request, 404 Not Found
    StateNode body = StateNode::Object();  ///< {ok: true, ...} or {ok: false, error, message}
    bool Ok() const { return status == 200; }
};

/// The names of the process (no instance) and of an instance, in the table's order
const std::vector<std::string>& ProcessNames();
const std::vector<std::string>& InstanceNames();
bool IsProcessName(const std::string& name);
bool IsInstanceName(const std::string& name);

/// One value now; Null for an unknown name
StateNode ReadProcess(const std::string& name);
StateNode ReadInstance(const std::string& name, Emulator& emulator);

/// Every verb: `all`, the names, `breakpoint_groups`
const std::vector<std::string>& Verbs();

/// Run a verb. Args: `instance` (an id or an index; default the followed instance) for an instance's name and for
/// `all` (default: every instance). The body: {ok, instance?, ...the value's fields}; `all`: {ok, host, clients,
/// instances: [{instance, index, model, emulator.stats: …, …}]}
Reply Call(std::string_view verb, const StateNode& args);

}  // namespace monitoring
