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
/// Every call fails with a reason when the machine has no VDAC2 card (not
/// TS-Conf, not the VDAC2 build, or a binary built without ENABLE_VDAC2).

#include <cstdint>
#include <string>

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

/// The machine has a VDAC2 card; `error` says why not
bool HasCard(EmulatorContext* context, std::string* error = nullptr);

/// Start a capture into `path` (replaces a capture in progress)
bool StartCapture(EmulatorContext* context, const std::string& path, std::string* error = nullptr);
/// Finish the running capture
bool StopCapture(EmulatorContext* context, std::string* error = nullptr);
/// The current or the last capture
bool GetCaptureStatus(EmulatorContext* context, CaptureStatus& status, std::string* error = nullptr);

} // namespace Vdac2Control
