#pragma once

/// @file pictureactions.h
/// @brief The catalogue's picture and window actions (Stream Deck use case C9): the GUI window's GPU, CRT effects,
/// profile and its parameters, temporal blending, full screen, scale, toolbar / status bar, HUD with its theme and
/// notices, the on-screen keyboard's picture, the ZX Next layers window - each one a WindowDisplay change, the same
/// validation as CLI `video window`, REST /video/window, Lua / Python window_display_set(), MCP capture_media - and the
/// overscan frame's viewport (VideoViewport, the machine's). Headless the window actions are listed and unavailable
/// ("no window"); their toggles read the settings the window published (WindowView::Publish), never waiting for the
/// GUI thread.
///
/// Readouts (readouts.h): `video.window` (the published WindowDisplay report), `video.crt_params` (the active
/// profile's parameters, published), `video.viewport` (VideoViewport::Report).

#include <string>
#include <vector>

#include "emulator/state/statenode.h"

class Emulator;

namespace actions
{

class ActionRegistry;
class RegistrationHandle;

/// Registers them on `registry`; they stay while the handles live
std::vector<RegistrationHandle> RegisterPictureActions(ActionRegistry& registry);

/// The readout names this file answers
const std::vector<std::string>& PictureReadoutNames();
/// One of them now (Null for another name); reads published state only
StateNode ReadPictureReadout(Emulator& emulator, const std::string& name);

}  // namespace actions
