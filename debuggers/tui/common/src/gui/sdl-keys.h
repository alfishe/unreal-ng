// sdl-keys.h - an SDL key event as the monitor's KeyPress (ui/actions.h). The window gets every UnrealSpeccy key with
// its modifiers; Cmd counts as Ctrl on macOS.
#pragma once

#include <SDL3/SDL.h>

#include "ui/actions.h"

namespace dbg {

inline KeyPress KeyFromSdl(SDL_Keycode code, SDL_Keymod mod) {
    KeyPress k;
    if ((mod & SDL_KMOD_SHIFT) != 0) k.mods |= kModShift;
    if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0) k.mods |= kModCtrl;
    if ((mod & SDL_KMOD_ALT) != 0) k.mods |= kModAlt;
    if (code >= SDLK_F1 && code <= SDLK_F12) {
        k.key = static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(code - SDLK_F1));
        return k;
    }
    switch (code) {
        case SDLK_ESCAPE: k.key = Key::Esc; return k;
        case SDLK_TAB: k.key = Key::Tab; return k;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: k.key = Key::Enter; return k;
        case SDLK_BACKSPACE: k.key = Key::Backspace; return k;
        case SDLK_SPACE: k.key = Key::Space; return k;
        case SDLK_HOME: k.key = Key::Home; return k;
        case SDLK_END: k.key = Key::End; return k;
        case SDLK_UP: k.key = Key::Up; return k;
        case SDLK_DOWN: k.key = Key::Down; return k;
        case SDLK_LEFT: k.key = Key::Left; return k;
        case SDLK_RIGHT: k.key = Key::Right; return k;
        case SDLK_PAGEUP: k.key = Key::PgUp; return k;
        case SDLK_PAGEDOWN: k.key = Key::PgDn; return k;
        default: break;
    }
    // Keycodes of printable keys are their unshifted characters ('a', '1', '`')
    if (code >= 0x21 && code < 0x7F) {
        k.key = Key::Char;
        k.ch = static_cast<char>(code);
    }
    return k;
}

}  // namespace dbg
