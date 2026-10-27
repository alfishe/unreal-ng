#pragma once
// The benchmark loads no image files; this stub satisfies the RmlUi SDL backend without SDL_image.
#include <SDL3/SDL.h>
inline SDL_Surface* IMG_LoadTyped_IO(SDL_IOStream* src, bool closeio, const char*)
{
    if (closeio && src)
        SDL_CloseIO(src);
    return nullptr;
}
