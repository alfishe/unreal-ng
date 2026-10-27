#pragma once

// Shared by the SDL3 + OpenGL variants (Dear ImGui, RmlUi).
//
// High DPI: the window is created in points (1600 x 1000 x UI scale) with SDL_WINDOW_HIGH_PIXEL_DENSITY, so on a Retina
// display the framebuffer has twice as many pixels per side, exactly like the Qt variants (Qt always renders at the
// window's device pixel ratio). The variants render at the framebuffer's real pixel size.
//
// No GPU: POC035_GL_SOFTWARE=1 asks for a non-accelerated pixel format (SDL_GL_ACCELERATED_VISUAL = 0). On macOS that
// is Apple's software renderer; on Linux use Mesa llvmpipe instead (LIBGL_ALWAYS_SOFTWARE=1, the attribute is not set
// there because GLX/EGL may refuse it). The renderer actually used is reported in the JSON ("backend").

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace sdlgl
{

/// Call before any window or GL attribute: initializes video and applies POC035_GL_SOFTWARE
inline bool PreInit()
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return false;
#if defined(__APPLE__)
    const char* sw = std::getenv("POC035_GL_SOFTWARE");
    if (sw && std::strcmp(sw, "1") == 0)
        SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 0);
#endif
    return true;
}

struct Pixels
{
    int w = 0, h = 0;
};

inline Pixels WindowPixels(SDL_Window* win)
{
    Pixels p;
    SDL_GetWindowSizeInPixels(win, &p.w, &p.h);
    return p;
}

/// GL_RENDERER of the current context, e.g. "Apple M2 Ultra", "Apple Software Renderer", "llvmpipe (LLVM 19.1.7, 256 bits)"
inline std::string RendererName(const unsigned char* (*getString)(unsigned int))
{
    const unsigned char* r = getString(0x1F01 /* GL_RENDERER */);
    std::string s = r ? reinterpret_cast<const char*>(r) : "unknown";
    for (char& c : s)
        if (c == '"' || c == '\\')
            c = ' ';
    return "opengl: " + s;
}

}  // namespace sdlgl
