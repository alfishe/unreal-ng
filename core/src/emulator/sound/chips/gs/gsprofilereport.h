#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "emulator/state/statenode.h"

class EmulatorContext;
class GeneralSoundCard;
class LabelManager;

/// @file gsprofilereport.h
/// @brief The reports of the card CPU's profiler (gscardprofiler.h; gs-debugger X9, X2, X20): `gs profile` (the ISR
/// budget, the commands' cost, the routines, the activity per frame) and `gs events` (the interrupt periods of the last
/// complete host frame with the port trace's events in card time), and the session verbs.
///
/// Every plane renders these reports: REST `GET state/audio/gs/profile?cpu=&routines=&activity=` and
/// `GET state/audio/gs/events?cpu=&first=&count=&rows=` (and their WebSocket `call`), `POST control/audio/gs`
/// `{action: profile_start | profile_stop | profile_clear, cpu}`; CLI `gs profile [start|stop|clear] [--cpu ms]
/// [--routines N] [--activity N]` and `gs events [--cpu ms] [--first N] [--count N] [--rows N]`; Lua / Python
/// `gs_profile(cpu, routines, activity)`, `gs_profile_control(action, cpu)`, `gs_events(cpu, first, count, rows)`;
/// MCP `inspect_state` aspects `audio_gs_profile`, `audio_gs_events` and `emulator_manage` `gs_profile_start` /
/// `gs_profile_stop` / `gs_profile_clear`; the device catalog's `gs-profile`, `gs-events`, `ms-gs-profile`,
/// `ms-gs-events`.

struct GSProfileQuery
{
    std::string cpu;         ///< "" / "gs" the GS slot's card, "ms" the ZX-MultiSound's
    size_t routines = 32;    ///< the top N routines by inclusive time (0: all)
    size_t activity = 256;   ///< the newest N frames of activity (0: none)
};

struct GSEventsQuery
{
    std::string cpu;
    size_t first = 0;        ///< the first period row
    size_t count = 0;        ///< rows from `first` (0: to the frame's end)
    size_t rows = 16;        ///< rows in the text (0: none)
};

/// The REST / CLI strings ("" = the default); false with @p error on a bad number
bool GSProfileQueryFromStrings(const std::string& cpu, const std::string& routines, const std::string& activity,
                               GSProfileQuery& query, std::string& error);
bool GSEventsQueryFromStrings(const std::string& cpu, const std::string& first, const std::string& count,
                              const std::string& rows, GSEventsQuery& query, std::string& error);

struct GSProfileControlReply
{
    int status = 200;        ///< as HTTP: 400 a bad action, 404 no such card / no card CPU
    StateNode body;          ///< {action, cpu, card, on} or {error}
    std::string text;
};

/// `start` (a new session: cleared, then on), `stop` (off, the data kept), `clear`. The card applies it at its next
/// run (an instruction boundary); any thread
GSProfileControlReply GSProfileControl(EmulatorContext* context, const std::string& action, const std::string& cpu);

namespace DeviceState
{
/// `{available, card, cpu, implementation, on, frames, cycles, clock_hz, units_per_cycle, period_cycles,
/// isr{last_frame, since_start: {count, min, avg, max, used_pct, headroom_pct, overruns, latency_avg, latency_max,
/// nmis, lost}}, commands[{code, name, handler, count, avg, min, max, total}], current_command{code, name, cycles},
/// routines[{address, page_kind, page, label, calls, interrupts, inclusive, exclusive, avg, min, max,
/// inclusive_pct}], routines_total, activity[{frame, cycles, isr_cycles, isr_pct, idle_pct, halt_pct, busy_pct,
/// commands, interrupts, overruns, lost, isr_max}], stack_overflows, routines_dropped, resyncs, text}`. Cycles are
/// card CPU cycles. `available` false with a `description` without a card CPU (the lightweight player) or a card
StateNode GsProfile(EmulatorContext* context, const GSProfileQuery& query = {});

/// `{available, card, cpu, on, capturing, frame, card_start, cycles, period_cycles, periods, first, count,
/// columns, rows[{period, start, isr[{accept, end, length, entry, nmi}], events[[cycle, kind, value, detail]]}],
/// totals{...}, truncated, text}`: the last complete host frame; a row a card interrupt period, `start` its start in
/// cycles from the frame's start, `accept` / `end` / `cycle` from the period's start (an ISR that runs past its period
/// has `end` above the period; null: still running at the frame's end). Kinds: host_data_write, host_data_read,
/// host_command, host_status, host_control, dac (detail: the channel), page (detail: the port), volume (detail: the
/// channel), zxdma_read, zxdma_write. The ISR bars need the profiler on, the other events the GS port trace capturing
StateNode GsEvents(EmulatorContext* context, const GSEventsQuery& query = {});

/// The same of one card (`cpu` names it in the reply; `labels` the card CPU's symbols or nullptr): the standalone
/// cards' tests
StateNode GsProfileOf(GeneralSoundCard& card, const std::string& cpu, LabelManager* labels, const GSProfileQuery& query);
StateNode GsEventsOf(GeneralSoundCard& card, const std::string& cpu, const GSEventsQuery& query);
}  // namespace DeviceState
