#pragma once

/// @file viewportcontrol.h
/// @brief The Pentagon overscan frame (384x304) and the viewport that crops it, the same on every automation surface:
/// REST PUT /state/screen/overscan and GET / PUT /video/viewport, CLI `video viewport`, Lua / Python video_viewport()
/// / video_viewport_set(), MCP capture_media viewport / viewport_set, the catalogue's video.overscan_toggle and
/// video.viewport, the feature `overscan`. The GUI's View > Overscan > Viewport menu names the same four presets; the
/// window follows a change made here (NC_VIDEO_VIEWPORT_CHANGED).
///
/// The viewport crops the overscan frame only: without overscan the picture is the full frame ("full").

#include <string>
#include <vector>

#include "emulator/state/statenode.h"
#include "emulator/video/screen.h"

class Emulator;

namespace VideoViewport
{

/// "full", "symmetric", "standard", "screen_only": ViewportPresets in the menu's order
const std::vector<std::string>& Names();
/// "full, symmetric, standard, screen_only"
std::string NameList();
/// The preset of a name (any case; "screen-only" too); false for none
bool Parse(const std::string& name, DisplayViewport& out);
/// The name of a viewport; "custom" for a crop no preset makes
std::string NameOf(const DisplayViewport& viewport);
/// The preset the GUI picks when overscan goes on
inline constexpr const char* kDefaultOverscanViewport = "symmetric";

struct Result
{
    int status = 200;   ///< 200, 400 (an unknown name), 409 (no overscan frame on this machine)
    std::string error;  ///< "" on success, else one sentence
    bool changed = false;

    bool Ok() const { return status == 200; }
};

/// {overscan, overscan_available (a Pentagon), viewport, viewports[], framebuffer "384x304", display "352x304",
/// video_mode}
StateNode Report(Emulator& emulator);
/// One line: "overscan on, viewport symmetric (352x304 of 384x304)"
std::string Summary(Emulator& emulator);

/// Overscan on or off with its viewport ("" = the GUI's default: symmetric on, full off). 409 on a machine without the
/// frame (only a Pentagon has it); asking for the mode it is in is not an error
Result SetOverscan(Emulator& emulator, bool enable, const std::string& viewport = {});
/// A viewport by name, or "next" / "prev" through the four. Needs overscan on, except "full"
Result SetViewport(Emulator& emulator, const std::string& name);

}  // namespace VideoViewport
