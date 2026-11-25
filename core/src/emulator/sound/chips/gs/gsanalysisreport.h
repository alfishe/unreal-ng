#pragma once

/// @file gsanalysisreport.h
/// @brief The reports and the switch of the card CPU analysis (gscardanalysis.h): coverage / CDL with the command
/// handlers executed and the reads of RAM never written (X8, X11), and the RAM heat map with the ranges of the last N
/// frames (X10), for the GS slot's card (`cpu` "gs" or "") and the ZX-MultiSound's GS (`cpu` "ms").
///
/// Every plane renders these reports: REST `GET state/audio/gs/analysis|coverage|heatmap` and `POST
/// control/audio/gs/analysis` (and their WebSocket `call`), CLI `gs analysis|coverage|heatmap`, Lua / Python
/// `gs_analysis`, `gs_coverage`, `gs_heatmap`, MCP tool `gs_analysis`, the device catalog's `gs-coverage` and
/// `gs-heatmap`. Reads never stop the machine: they read the arrays on the caller's thread.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "emulator/sound/chips/gs/gscardanalysis.h"
#include "emulator/state/statenode.h"

class EmulatorContext;

/// What a coverage or heat-map read asks for
struct GSAnalysisQuery
{
    std::string cpu;            ///< "" or "gs": the GS slot's card; "ms": the ZX-MultiSound's GS

    // Coverage
    std::string space;          ///< "" both, "rom" (ROM / flash), "ram"
    int page = -1;              ///< one 16 KB page of `space` (-1: every page that has a mark)
    size_t ranges = 16;         ///< ranges listed per page and kind (0: none)
    bool raw = false;           ///< with `page`: its 16,384 flag bytes as base64 (`flags`)

    // Heat map
    std::vector<GSCardAnalysis::Channel> channels;  ///< the channels whose grid is reported (empty: all)
    std::vector<GSCardAnalysis::Channel> recent;    ///< the channels whose recent ranges are listed (default: dac)
    uint32_t frames = 50;       ///< "recent": touched in the last N host frames
    uint32_t cells = 4096;      ///< the grid's cells at most (neighbours merged by their maximum; 0: every cell)
    std::string format = "base64";  ///< the grid as base64, hex (rows of `columns` cells), none (counts only)
};

/// Parses the named parameters of a read (REST query, CLI `--key value`, Lua table, Python kwargs): cpu, space, page,
/// ranges, raw, channels, recent, frames, cells, format; `get` returns "" for an absent one. False with @p error
bool GSAnalysisQueryFromParams(const std::function<std::string(const std::string&)>& get, GSAnalysisQuery& query,
                               std::string& error);

/// The switch: `action` status (default), on, off (stop marking, keep the data), reset (clear the data), release
/// (drop the data and its memory); `decay` (on / status: the heat lost per frame, 0-255; -1 keeps it)
struct GSAnalysisControlRequest
{
    std::string action;
    std::string cpu;
    int decay = -1;
};

struct GSAnalysisReply
{
    int status = 200;   ///< as HTTP: 400 bad action / not available, 404 no such card
    StateNode body;     ///< the status report, or {error}
};

/// Any thread. The card binds or unbinds the analysis at its next run (a paused machine: when it runs again)
GSAnalysisReply GSAnalysisControl(EmulatorContext* context, const GSAnalysisControlRequest& request);

namespace DeviceState
{
/// `{available, card, cpu, enabled, recording, memory_bytes, decay, rom_bytes, ram_bytes, cell_bytes, cells,
/// card_resets, created_frame, last_reset_frame, frame, text}`; `available` false with a `description` on a card
/// without a CPU (the lightweight GS) or when none is fitted
StateNode GsAnalysisStatus(EmulatorContext* context, const std::string& cpu = {});

/// The code / data log: `{...status, legend, spaces[{space, name, bytes, pages, totals{exec, code, read, write, jump,
/// sub, dac, written}, page_list[{page, exec, code, read, write, jump, sub, dac, written, code_ranges[[start, end]],
/// data_ranges, dac_ranges, sub_entries (one page asked), flags (raw)}]}], handlers{profile, table, entries,
/// executed_count, executed[{command, name, handler, space, page}], not_executed[command]}, never_written{count,
/// dropped, rows[{address, space, page, offset, pc, frame, kind}]}, text}`. Range ends inclusive, offsets in the page
StateNode GsCoverage(EmulatorContext* context, const GSAnalysisQuery& query = {});

/// The heat map: `{...status, bump, frame, data_cells, group, columns, channels[{name, accesses, hot_cells, max, data
/// (base64) | rows (hex)}], recent{frames, channels[{name, ranges[{start, end, length, page, offset}], truncated}]},
/// text}`; heat decayed to the frame of the read; `start` / `end` RAM byte offsets (inclusive)
StateNode GsHeatmap(EmulatorContext* context, const GSAnalysisQuery& query = {});
}  // namespace DeviceState
