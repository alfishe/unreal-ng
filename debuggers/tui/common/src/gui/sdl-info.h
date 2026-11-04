// sdl-info.h - what the pixel window is built on, for --help: the SDL version and its video drivers (the package
// checks read it: a package carries only the SDL3 builds, with x11 / wayland on Linux, cocoa on macOS, windows on
// Windows), or that the build has none.
#pragma once

#include <string>

#if UNREAL_TUI_HAVE_SDL
#include <SDL3/SDL.h>
#endif

namespace dbg {

inline std::string SdlWindowInfo() {
#if UNREAL_TUI_HAVE_SDL
    const int v = SDL_GetVersion();
    std::string info = "SDL " + std::to_string(SDL_VERSIONNUM_MAJOR(v)) + "." + std::to_string(SDL_VERSIONNUM_MINOR(v)) +
                       "." + std::to_string(SDL_VERSIONNUM_MICRO(v)) + ", video:";
    for (int i = 0; i < SDL_GetNumVideoDrivers(); i++) info += std::string(" ") + SDL_GetVideoDriver(i);
    return info;
#else
    return "none (built without SDL3: the terminal only)";
#endif
}

}  // namespace dbg
