// sdl-none.cpp - the build without SDL3: there is no pixel window, the app runs in the terminal.
#include "gui/sdl-runner.h"

namespace dbg {

bool RunSdlWindow(IDebuggerBackend&, UiState&, TextScreen&, const Palette&, const std::function<void()>&,
                  const std::function<void()>&, const std::function<void(int)>&, const SdlRunnerConfig&) {
    return false;
}

}  // namespace dbg
