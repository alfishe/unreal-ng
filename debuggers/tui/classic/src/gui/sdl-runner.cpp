// sdl-runner.cpp - SDL3 window runner implementation for classic debugger.
#include "gui/sdl-runner.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

#include "gui/pixel-renderer.h"
#include "gui/sdl-keys.h"
#include "ui/actions.h"
#include "ui/keymap.h"

namespace dbg {

bool RunSdlWindow(IDebuggerBackend& be, UiState& ui, TextScreen& screen,
                  const Palette& palette,
                  const std::function<void()>& repaint,
                  const std::function<void()>& followCursor,
                  const SdlRunnerConfig& config) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    const int cellWidth = screen.Width();
    const int cellHeight = screen.Height();
    const int basePixelWidth = cellWidth * 8;
    const int basePixelHeight = cellHeight * 16;
    float currentScale = (config.scale >= 0.5f && config.scale <= 5.0f) ? config.scale : 1.5f;
    int winWidth = static_cast<int>(basePixelWidth * currentScale);
    int winHeight = static_cast<int>(basePixelHeight * currentScale);

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(config.title, winWidth, winHeight,
                                     SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &window, &renderer)) {
        std::fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    int renderWidth = 0;
    int renderHeight = 0;
    SDL_GetWindowSizeInPixels(window, &renderWidth, &renderHeight);
    if (renderWidth <= 0 || renderHeight <= 0) {
        renderWidth = winWidth;
        renderHeight = winHeight;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            renderWidth, renderHeight);
    if (!texture) {
        std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    PixelRenderer blitter(cellWidth, cellHeight, renderWidth, renderHeight);

    auto recreateTextureIfNeeded = [&]() {
        int newRw = 0;
        int newRh = 0;
        SDL_GetWindowSizeInPixels(window, &newRw, &newRh);
        if (newRw <= 0 || newRh <= 0) {
            newRw = winWidth;
            newRh = winHeight;
        }
        if (newRw == renderWidth && newRh == renderHeight && texture) return false;
        renderWidth = newRw;
        renderHeight = newRh;
        blitter.Resize(renderWidth, renderHeight);
        if (texture) {
            SDL_DestroyTexture(texture);
        }
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    renderWidth, renderHeight);
        return true;
    };

    // Three costs, paid only when needed: show the texture (a move, an expose), blit the screen into it (a new size,
    // a changed picture), paint the screen from the backend (input, a change of the emulator)
    auto present = [&]() {
        if (!texture) return;
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
    };
    auto blit = [&]() {
        blitter.Render(screen, palette);
        if (texture) {
            SDL_UpdateTexture(texture, nullptr, blitter.Pixels().data(),
                              renderWidth * static_cast<int>(sizeof(uint32_t)));
        }
        present();
    };
    TextScreen shown = screen;
    auto updateAndPresent = [&]() {
        repaint();
        if (screen == shown) return;
        shown = screen;
        blit();
    };

    repaint();
    shown = screen;
    blit();

    bool running = true;
    ActionContext ctx{be, ui, false, followCursor, [&] { running = false; }};
    while (running) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 50)) {
            bool needsRepaint = false;
            bool needsBlit = false;
            bool needsPresent = false;
            do {
                if (event.type == SDL_EVENT_QUIT) {
                    running = false;
                    break;
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        int curWinW = 0, curWinH = 0;
                        SDL_GetWindowSize(window, &curWinW, &curWinH);
                        if (curWinW > 0 && curWinH > 0) {
                            const int cx = static_cast<int>((event.button.x / static_cast<float>(curWinW)) * cellWidth);
                            const int cy = static_cast<int>((event.button.y / static_cast<float>(curWinH)) * cellHeight);
                            if (HandleMenuClick(ctx, cx, cy) || HandleMouseClick(be, ui, cx, cy, false)) {
                                if (ui.activeWidget == WidgetId::Trace) {
                                    followCursor();
                                }
                                needsRepaint = true;
                            }
                        }
                    }
                } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                    if (event.wheel.y > 0) {
                        HandleArrowKey(be, ui, NavKey::Up, false);
                        if (ui.activeWidget == WidgetId::Trace) followCursor();
                        needsRepaint = true;
                    } else if (event.wheel.y < 0) {
                        HandleArrowKey(be, ui, NavKey::Down, false);
                        if (ui.activeWidget == WidgetId::Trace) followCursor();
                        needsRepaint = true;
                    }
                } else if (event.type == SDL_EVENT_KEY_DOWN) {
                    const SDL_Keycode key = event.key.key;
                    const SDL_Keymod mod = event.key.mod;
                    const bool cmdOrCtrl = (mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0;

                    const bool isPlus = (key == SDLK_EQUALS || key == SDLK_PLUS || key == SDLK_KP_PLUS);
                    const bool isMinus = (key == SDLK_MINUS || key == SDLK_KP_MINUS);
                    const bool isZero = (key == SDLK_0 || key == SDLK_KP_0);

                    if (cmdOrCtrl && (isPlus || key == SDLK_EQUALS)) {
                        currentScale = std::min(4.0f, currentScale + 0.25f);
                        winWidth = static_cast<int>(basePixelWidth * currentScale);
                        winHeight = static_cast<int>(basePixelHeight * currentScale);
                        SDL_SetWindowSize(window, winWidth, winHeight);
                        recreateTextureIfNeeded();
                        needsRepaint = true;
                        needsBlit = true;
                    } else if (cmdOrCtrl && isMinus) {
                        currentScale = std::max(0.75f, currentScale - 0.25f);
                        winWidth = static_cast<int>(basePixelWidth * currentScale);
                        winHeight = static_cast<int>(basePixelHeight * currentScale);
                        SDL_SetWindowSize(window, winWidth, winHeight);
                        recreateTextureIfNeeded();
                        needsRepaint = true;
                        needsBlit = true;
                    } else if (cmdOrCtrl && isZero) {
                        currentScale = (config.scale >= 0.5f && config.scale <= 5.0f) ? config.scale : 1.5f;
                        winWidth = static_cast<int>(basePixelWidth * currentScale);
                        winHeight = static_cast<int>(basePixelHeight * currentScale);
                        SDL_SetWindowSize(window, winWidth, winHeight);
                        recreateTextureIfNeeded();
                        needsRepaint = true;
                        needsBlit = true;
                    } else if (HandleKey(ctx, KeyFromSdl(key, mod))) {
                        needsRepaint = true;
                        if (!running) break;
                    }
                } else if (event.type == SDL_EVENT_WINDOW_RESIZED ||
                           event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                    // The same picture at a new size: blit it again, nothing asked of the backend
                    if (recreateTextureIfNeeded()) needsBlit = true;
                    else needsPresent = true;
                } else if (event.type == SDL_EVENT_WINDOW_EXPOSED) {
                    needsPresent = true;  // a move or an uncovered part: the texture as it is
                }
            } while (SDL_PollEvent(&event));

            if (running) {
                if (needsRepaint) {
                    if (needsBlit) {
                        repaint();
                        shown = screen;
                        blit();
                    } else {
                        updateAndPresent();
                    }
                } else if (needsBlit) {
                    blit();
                } else if (needsPresent) {
                    present();
                }
            }
        }
        // Another client of the emulator stepped or stopped it, or changed its breakpoints
        if (running && be.PollExternalChanges()) {
            ui.traceCurs = be.GetRegs(0).pc;
            followCursor();
            updateAndPresent();
        }
    }

    if (texture) {
        SDL_DestroyTexture(texture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return true;
}

}  // namespace dbg
