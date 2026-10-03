#pragma once

/// @file vdac2control.h
/// @brief The VDAC2 card's control for the automation surfaces: one place
/// that every interface (WebAPI, MCP, CLI, Lua, Python) calls, so all of
/// them behave and answer the same (vdac2-integration-design.md §11).
///
/// Bus capture: the FT812's bus traffic written to an .evr replay stream
/// (vdac2-test-corpus.md §4). Start and stop at any moment; a capture
/// started on a running chip begins with the chip's whole state.
///
/// Line budget metrics (line-budget-metrics.md, line-budget-model.md): the
/// FT812's per-line cost of the last finished frame, kept in the chip state
/// (so a TTD seek shows the metrics of that moment), and the frame in flight.
///
/// Every call fails with a reason when the machine has no VDAC2 card (not
/// TS-Conf, not the VDAC2 build, or a binary built without ENABLE_VDAC2).

#include <cstdint>
#include <string>
#include <vector>

class EmulatorContext;

namespace Vdac2Control
{

struct CaptureStatus
{
    bool capturing = false;     ///< a capture is running
    std::string path;           ///< the current or the last capture's file ("" before the first)
    uint64_t bytesWritten = 0;  ///< bytes in the file so far
    uint64_t selects = 0;       ///< chip select changes recorded
    uint64_t exchanges = 0;     ///< bus bytes recorded
    uint64_t frames = 0;        ///< FT812 frame ends recorded
    uint64_t startClock = 0;    ///< FT812 system clock the stream starts at
    uint64_t lastClock = 0;     ///< FT812 system clock of the last record written
};

/// The line budget metrics of the last finished FT812 frame, and of the frame in flight
struct FrameMetrics
{
    // The last finished frame (the block in the chip state)
    bool valid = false;            ///< every visible line was drawn and measured (see measureAlways)
    uint64_t frame = 0;            ///< the chip's frame number (REG_FRAMES) of the block
    uint32_t lines = 0;            ///< visible lines (VSIZE)
    uint32_t hardBudget = 0;       ///< clocks a line has (HCYCLE x PCLK)
    uint32_t softBudget = 0;       ///< the warning threshold the frame was measured with
    uint32_t worstLine = 0;        ///< the most expensive line
    uint32_t worstClocks = 0;      ///< and its cost
    uint64_t totalClocks = 0;      ///< sum over the lines
    uint32_t linesOverSoft = 0;    ///< softBudget < cost <= hardBudget
    uint32_t linesOverHard = 0;    ///< cost > hardBudget: broken on a real card
    std::vector<uint16_t> lineClocks;  ///< cost per line (only when asked for lines)

    // Settings
    uint32_t margin = 0;           ///< soft budget margin, percent ([VDAC2] LineBudgetMargin)
    bool measureAlways = false;    ///< every frame drawn and measured, shown or not

    // The frame in flight (only when asked, and only while the machine is paused:
    // reading it brings the chip up to the machine's position)
    bool inFlightKnown = false;    ///< false: not asked, or the machine runs
    uint32_t inFlightLinesPassed = 0;      ///< lines of the frame in flight the scan has passed
    std::vector<int32_t> inFlightLineClocks;  ///< cost of each passed line, -1 = passed without drawing (with lines)
};

/// The machine has a VDAC2 card; `error` says why not
bool HasCard(EmulatorContext* context, std::string* error = nullptr);

/// Start a capture into `path` (replaces a capture in progress)
bool StartCapture(EmulatorContext* context, const std::string& path, std::string* error = nullptr);
/// Finish the running capture
bool StopCapture(EmulatorContext* context, std::string* error = nullptr);
/// The current or the last capture
bool GetCaptureStatus(EmulatorContext* context, CaptureStatus& status, std::string* error = nullptr);

/// The line budget metrics; `withLines` adds the per-line costs, `inFlight` the
/// frame in flight (paused machine only; inFlightKnown says whether it was read)
bool GetFrameMetrics(EmulatorContext* context, FrameMetrics& metrics, bool withLines, bool inFlight,
                     std::string* error = nullptr);
/// The metrics of the picture the monitor shows now (the FT812 Debug window's
/// source: the same moment as the main screen, also after a TTD seek); false
/// while the monitor shows the Evo or before measure-always kept one
bool GetPresentedFrameMetrics(EmulatorContext* context, FrameMetrics& metrics, std::string* error = nullptr);
/// The soft budget margin, percent 0..50 (from the next finished frame on)
bool SetLineBudgetMargin(EmulatorContext* context, uint32_t percent, std::string* error = nullptr);
/// Draw and measure every frame (also not shown, also under turbo); off by default
bool SetMeasureAlways(EmulatorContext* context, bool on, std::string* error = nullptr);

} // namespace Vdac2Control
