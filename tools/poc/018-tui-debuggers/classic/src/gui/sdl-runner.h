// sdl-runner.h - SDL3 window runner for classic debugger.
#pragma once

#include <functional>

#include "backend/debugger-backend.h"
#include "panels/paint.h"
#include "screen/palette.h"
#include "screen/textscreen.h"

namespace dbg {

struct SdlRunnerConfig {
    const char* title = "UnrealSpeccy Debugger (Classic 80x30)";
    float scale = 1.5f;
};

// Returns false if SDL3 could not be initialized or window could not be created.
bool RunSdlWindow(IDebuggerBackend& be, UiState& ui, TextScreen& screen,
                  const Palette& palette,
                  const std::function<void()>& repaint,
                  const std::function<void()>& followCursor,
                  const SdlRunnerConfig& config = {});

}  // namespace dbg
