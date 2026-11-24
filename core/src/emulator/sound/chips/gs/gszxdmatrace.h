#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "emulator/state/statenode.h"

class EmulatorContext;

/// @file gszxdmatrace.h
/// @brief The NeoGS ZX-DMA trace (gs-debugger N6; neogs-zxdma-design.md §6.4): the bytes the Spectrum read from or
/// wrote to the card's RAM through the ZX-DMA window, one row a byte, from the GS port trace's history (gsporttrace.h,
/// side ZxDma). Recorded only while the port trace captures (POST control/audio/gs/porttrace {action: start}, CLI
/// `gsporttrace start`). The NeoGS keeps the bytes in a ring of their own (the DAC fetches of the port trace's ring do not evict them); a full ring evicts its oldest bytes: the report says so.
///
/// Every plane renders this one report: REST `GET state/audio/gs/zxdma?last=&since=` (and its WebSocket `call`),
/// CLI `gs zxdma [last] [--since <seq>]`, Lua / Python `gs_zxdma(last, since)`, MCP `inspect_state` aspect
/// `audio_gs_zxdma`, the device catalog's `gs-zxdma`.

/// Which rows: the newest `last` ZX-DMA bytes (0: every one kept) after the sequence number `since` (0: from the oldest)
struct GSZxDmaTraceQuery
{
    size_t last = 256;
    uint64_t since = 0;
};

/// Parses the REST / CLI strings ("" = the default); false with @p error on a bad number
bool GSZxDmaTraceQueryFromStrings(const std::string& last, const std::string& since, GSZxDmaTraceQuery& query,
                                  std::string& error);

namespace DeviceState
{
/// `{available, card, device, capturing, armed, total_bytes_kept, evicted, last_seq, restarted, counters{bytes_read,
/// bytes_written, bytes_dropped, wait_t_states}, rows[{seq, frame, host_t, card_time, direction, card_address, page,
/// value, wait, host_pc, dropped}], text}`; rows oldest first. `seq` is the port trace history's number of the event
/// (every side counts): a poller passes the last `seq` it saw (or `last_seq`) as `since`. A `since` past `last_seq`
/// (a new session) reads from the start, `restarted` true. `card_time` in card CPU cycles (cardCycles), `host_t` the
/// host's T-state in frame `frame` at the access end (after the wait), `wait` the /WAIT T-states (capped at 255).
/// `available` false with a `description` without a NeoGS (the classic GS has no ZX-DMA). Peeked: reading changes
/// nothing
StateNode GsZxDmaTrace(EmulatorContext* context, const GSZxDmaTraceQuery& query = {});
}  // namespace DeviceState
