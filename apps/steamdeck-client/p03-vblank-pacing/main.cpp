// P-03 vblank pacing: does gamescope deliver vblanks at the refresh set in Quick Access, and can the app
// measure it (and notice a change) quickly?
//
// An SDL_GPU loop with no emulation: acquire the swapchain image (blocks on the present queue), clear it,
// submit. The time acquire returns, per frame, gives the refresh the app actually gets. Every second the
// median interval, the refresh it implies, the missed vblanks (interval > 1.5 x median) and a change marker
// go to ~/steamdeck-poc-logs/p03-*.csv; the raw per-frame times go to a second CSV.
//
//   p03-vblank-pacing [--present vsync|mailbox|immediate] [--fif 1|2|3] [--work-ms N] [--seconds N] [--windowed]
//
// The screen alternates two greys every frame (a 25 / 30 / 45 Hz flicker a camera can count) and turns
// red for a second whenever the measured refresh changes. Quit: View + Menu, Esc, or --seconds.

#include "timinglog.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
    uint32_t framesInFlight = 2;
    double workMs = 0;
    double seconds = 0;
    const char* presentName = "vsync";
    bool windowed = false;   // development on a desktop: a 1280x800 window instead of fullscreen
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "--windowed") == 0)
            windowed = true;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (std::strcmp(argv[i], "--windowed") == 0)
        {
            i--;   // a flag without a value
            continue;
        }
        if (std::strcmp(argv[i], "--present") == 0)
        {
            presentName = argv[i + 1];
            if (std::strcmp(presentName, "mailbox") == 0)
                presentMode = SDL_GPU_PRESENTMODE_MAILBOX;
            else if (std::strcmp(presentName, "immediate") == 0)
                presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
        }
        else if (std::strcmp(argv[i], "--fif") == 0)
            framesInFlight = static_cast<uint32_t>(std::atoi(argv[i + 1]));
        else if (std::strcmp(argv[i], "--work-ms") == 0)
            workMs = std::atof(argv[i + 1]);
        else if (std::strcmp(argv[i], "--seconds") == 0)
            seconds = std::atof(argv[i + 1]);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("P-03 vblank pacing", 1280, 800, windowed ? 0 : SDL_WINDOW_FULLSCREEN);
    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (!window || !device || !SDL_ClaimWindowForGPUDevice(device, window))
    {
        SDL_Log("GPU setup: %s", SDL_GetError());
        return 1;
    }
    SDL_RaiseWindow(window);   // take keyboard focus (a binary started outside a bundle on macOS may not)
    if (!SDL_WindowSupportsGPUPresentMode(device, window, presentMode))
    {
        SDL_Log("present mode %s not supported, using vsync", presentName);
        presentMode = SDL_GPU_PRESENTMODE_VSYNC;
        presentName = "vsync(fallback)";
    }
    SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode);
    SDL_SetGPUAllowedFramesInFlight(device, framesInFlight);

    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    deckpoc::CsvLog summary;
    summary.Open(deckpoc::LogPath("p03-summary", "csv"),
                 "frames,median_ms,hz,p1_ms,p99_ms,max_ms,missed,acquire_wait_median_ms,changed");
    deckpoc::CsvLog frames;
    frames.Open(deckpoc::LogPath("p03-frames", "csv"), "frame,acquire_wait_ns,submit_ns");
    summary.Row(deckpoc::NowNs(), "# driver=%s gpu=%s present=%s fif=%u work_ms=%.2f display_mode_hz=%.3f size=%dx%d",
                SDL_GetCurrentVideoDriver(), SDL_GetGPUDeviceDriver(device), presentName, framesInFlight, workMs,
                mode ? mode->refresh_rate : 0.0f, mode ? mode->w : 0, mode ? mode->h : 0);

    std::vector<uint64_t> acquired;          // this second
    std::vector<uint64_t> acquireWaits;      // this second
    uint64_t lastAcquired = 0;
    uint64_t secondStart = deckpoc::NowNs();
    const uint64_t runStart = secondStart;
    double lastHz = 0;
    uint64_t redUntil = 0;
    uint64_t frame = 0;
    bool quit = false;
    SDL_Gamepad* pad = nullptr;

    while (!quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
                quit = true;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad)
                pad = SDL_OpenGamepad(e.gdevice.which);
        }
        if (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK) && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START))
            quit = true;

        // Simulated per-frame work (emulation + upload) before acquiring, as the real loop does
        if (workMs > 0)
        {
            const uint64_t until = deckpoc::NowNs() + static_cast<uint64_t>(workMs * 1e6);
            while (deckpoc::NowNs() < until) {}
        }

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        const uint64_t t0 = deckpoc::NowNs();
        SDL_GPUTexture* swapchain = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, nullptr, nullptr))
        {
            SDL_Log("acquire: %s", SDL_GetError());
            break;
        }
        const uint64_t t1 = deckpoc::NowNs();

        if (swapchain)
        {
            const bool red = t1 < redUntil;
            const float g = (frame & 1) ? 0.35f : 0.15f;
            SDL_GPUColorTargetInfo target{};
            target.texture = swapchain;
            target.clear_color = red ? SDL_FColor{0.8f, 0.1f, 0.1f, 1.0f} : SDL_FColor{g, g, g, 1.0f};
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
            SDL_EndGPURenderPass(pass);
        }
        SDL_SubmitGPUCommandBuffer(cmd);
        const uint64_t t2 = deckpoc::NowNs();

        frames.Row(t1, "%llu,%llu,%llu", static_cast<unsigned long long>(frame), static_cast<unsigned long long>(t1 - t0),
                   static_cast<unsigned long long>(t2 - t1));
        acquired.push_back(t1);
        acquireWaits.push_back(t1 - t0);
        lastAcquired = t1;
        frame++;

        if (t1 - secondStart >= 1'000'000'000ull)
        {
            const deckpoc::IntervalStats s = deckpoc::ComputeIntervals(acquired);
            const deckpoc::IntervalStats w = deckpoc::ComputeSamples(acquireWaits);
            size_t missed = 0;
            for (size_t i = 1; i < acquired.size(); i++)
                if ((acquired[i] - acquired[i - 1]) / 1e6 > 1.5 * s.medianMs)
                    missed++;
            const double hz = s.medianMs > 0 ? 1000.0 / s.medianMs : 0;
            const bool changed = lastHz > 0 && std::fabs(hz - lastHz) / lastHz > 0.01;
            if (changed)
                redUntil = t1 + 1'000'000'000ull;
            lastHz = hz;
            summary.Row(t1, "%zu,%.4f,%.3f,%.4f,%.4f,%.4f,%zu,%.4f,%d", acquired.size(), s.medianMs, hz, s.p01Ms, s.p99Ms, s.maxMs,
                        missed, w.medianMs, changed ? 1 : 0);
            summary.Flush();
            frames.Flush();
            SDL_Log("%.3f Hz  median %.3f ms  p99 %.3f  missed %zu  acquire wait %.3f ms%s", hz, s.medianMs, s.p99Ms, missed,
                    w.medianMs, changed ? "  CHANGED" : "");
            // keep the last timestamp so the next second's first interval is counted
            acquired.assign(1, lastAcquired);
            acquireWaits.clear();
            secondStart = t1;
        }
        if (seconds > 0 && (t1 - runStart) / 1e9 >= seconds)
            quit = true;
    }

    if (pad)
        SDL_CloseGamepad(pad);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
