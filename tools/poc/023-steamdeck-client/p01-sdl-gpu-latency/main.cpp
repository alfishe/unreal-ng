// P-01 SDL_GPU latency: the render path of rendering.md §3 with the core's own pacing (core-clocked).
//
// Per display refresh: acquire the swapchain image -> CopyPresentedFramebuffer straight into a mapped
// transfer buffer (cycle) -> upload to the frame texture -> one nearest-filter blit (integer scale, crop)
// -> submit. No shaders yet (P-07 ports the CRT pass).
//
// Default program: a border flip on Kempston fire (A / D-pad fire / Space), polled in a tight loop, so the
// border changes within a few T-states of the port read. The app logs, per press, the time from the SDL
// event to the submit of the first frame whose border shows the change ("software latency"); the camera
// measures the rest (README.md). Per frame it logs acquire wait, copy, submit and the emulator frame
// counter step (0 = repeated frame, 2+ = a frame skipped).
//
//   p01-sdl-gpu-latency [--model PENTAGON] [--file game.tap] [--present vsync|mailbox|immediate] [--fif 1|2]
//                       [--delay 0..3] [--crop full|deckfit|paper] [--seconds N] [--windowed] [--autofire MS]
//
// Logs: ~/steamdeck-poc-logs/p01-*.csv. Quit: View + Menu, Esc.

#include "emuhost.h"
#include "timinglog.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{

struct Options
{
    std::string model = "PENTAGON";   // decodes Kempston at #1F (a bare 48K does not)
    std::string file;
    std::string present = "vsync";
    std::string crop = "full";
    uint32_t framesInFlight = 1;
    int presentDelay = 0;
    double seconds = 0;
    double autofireMs = 0;   // > 0: toggle fire on a timer, no human needed (automated latency runs)
    bool windowed = false;   // development on a desktop: a 1280x800 window instead of fullscreen
};

struct Rect { uint32_t x, y, w, h; };

/// Source rectangle for a crop of a 352x288-style frame (48 px border); other sizes fall back to full
Rect CropRect(const std::string& crop, uint32_t w, uint32_t h)
{
    if (w == 352 && h == 288)
    {
        if (crop == "deckfit")
            return {16, 44, 320, 200};
        if (crop == "paper")
            return {48, 48, 256, 192};
    }
    return {0, 0, w, h};
}

/// Largest integer scale that fits, centred
Rect Fit(const Rect& src, uint32_t dw, uint32_t dh)
{
    uint32_t scale = std::max(1u, std::min(dw / src.w, dh / src.h));
    const uint32_t w = src.w * scale, h = src.h * scale;
    return {(dw - std::min(w, dw)) / 2, (dh - std::min(h, dh)) / 2, std::min(w, dw), std::min(h, dh)};
}

} // namespace

int main(int argc, char** argv)
{
    Options opt;
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], "--windowed") == 0)
            opt.windowed = true;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (std::strcmp(argv[i], "--windowed") == 0)
        {
            i--;   // a flag without a value
            continue;
        }
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--model") opt.model = v;
        else if (k == "--file") opt.file = v;
        else if (k == "--present") opt.present = v;
        else if (k == "--fif") opt.framesInFlight = static_cast<uint32_t>(std::stoi(v));
        else if (k == "--delay") opt.presentDelay = std::stoi(v);
        else if (k == "--crop") opt.crop = v;
        else if (k == "--seconds") opt.seconds = std::stod(v);
        else if (k == "--autofire") opt.autofireMs = std::stod(v);
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
    {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }

    // Emulator
    deckpoc::EmuHost emu;
    std::string error;
    if (!emu.Create(opt.model, &error))
    {
        SDL_Log("emulator: %s", error.c_str());
        return 1;
    }
    if (!opt.file.empty())
    {
        if (!emu.LoadFile(opt.file, &error))
            SDL_Log("load %s: %s", opt.file.c_str(), error.c_str());
    }
    else
    {
        // The border test reads Kempston at #1F: a model without it reads #FF there, "fire" always held
        if (!emu.HasKempston())
        {
            SDL_Log("model %s has no Kempston joystick at #1F: the border test needs one (use --model PENTAGON)",
                    opt.model.c_str());
            return 1;
        }
        // The test program never runs the ROM: say on screen what this is, or it looks hung
        emu.PrintScreen({"", " BORDER LATENCY TEST (P-01)", "",
                         " Hold FIRE: the border turns", " white while it is held.", "",
                         " FIRE  = Space / A button", " QUIT  = Esc / View + Menu", "",
                         " No ROM, no interrupts: the CPU", " polls Kempston port #1F in a", " tight loop. Every press is",
                         " timed into the log."});
        emu.InstallProgram(0x8000, deckpoc::EmuHost::BorderFlipProgram());
    }
    if (!emu.OpenAudio(&error))
        SDL_Log("audio: %s", error.c_str());
    emu.SetPresentDelayFrames(static_cast<uint8_t>(opt.presentDelay));
    emu.Start();

    // GPU
    SDL_Window* window = SDL_CreateWindow("P-01 SDL_GPU latency", 1280, 800, opt.windowed ? 0 : SDL_WINDOW_FULLSCREEN);
    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (!window || !device || !SDL_ClaimWindowForGPUDevice(device, window))
    {
        SDL_Log("GPU setup: %s", SDL_GetError());
        return 1;
    }
    SDL_RaiseWindow(window);   // take keyboard focus (a binary started outside a bundle on macOS may not)
    SDL_GPUPresentMode presentMode = opt.present == "mailbox" ? SDL_GPU_PRESENTMODE_MAILBOX
                                   : opt.present == "immediate" ? SDL_GPU_PRESENTMODE_IMMEDIATE
                                   : SDL_GPU_PRESENTMODE_VSYNC;
    if (!SDL_WindowSupportsGPUPresentMode(device, window, presentMode))
    {
        SDL_Log("present mode %s not supported, using vsync", opt.present.c_str());
        presentMode = SDL_GPU_PRESENTMODE_VSYNC;
    }
    SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode);
    SDL_SetGPUAllowedFramesInFlight(device, opt.framesInFlight);

    SDL_GPUTexture* frameTexture = nullptr;
    SDL_GPUTransferBuffer* transfer = nullptr;
    uint32_t texW = 0, texH = 0;

    // Logs
    deckpoc::CsvLog frameLog;
    frameLog.Open(deckpoc::LogPath("p01-frames", "csv"), "acquire_wait_us,copy_us,submit_us,emu_frame,emu_step,audio_queued,underruns,swapchain_ok,border_white,swapchain_w,swapchain_h,window_flags");
    deckpoc::CsvLog pressLog;
    pressLog.Open(deckpoc::LogPath("p01-presses", "csv"), "event_ns,first_frame_submit_ns,software_latency_ms,frames_waited");
    pressLog.Row(deckpoc::NowNs(), "# model=%s present=%s fif=%u delay=%d crop=%s gpu=%s video=%s audio_rate=%u",
                 opt.model.c_str(), opt.present.c_str(), opt.framesInFlight, opt.presentDelay, opt.crop.c_str(),
                 SDL_GetGPUDeviceDriver(device), SDL_GetCurrentVideoDriver(), emu.AudioRate());

    SDL_Gamepad* pad = nullptr;
    bool fireDown = false;
    uint64_t pendingPressNs = 0;    // event timestamp of a press not yet seen on screen
    bool pendingWantWhite = false;
    int framesWaited = 0;
    uint64_t lastEmuFrame = 0;
    std::vector<uint64_t> softwareLatencies;
    const uint64_t start = deckpoc::NowNs();
    bool quit = false;
    uint64_t nextAutofire = start + 1'000'000'000ull;   // first toggle after 1 s

    auto setFire = [&](bool down, uint64_t eventNs)
    {
        if (down == fireDown)
            return;
        fireDown = down;
        if (down)
            emu.JoystickPress(0x10);
        else
            emu.JoystickRelease(0x10);
        pendingPressNs = eventNs ? eventNs : deckpoc::NowNs();
        pendingWantWhite = down;
        framesWaited = 0;
    };

    while (!quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_EVENT_QUIT: quit = true; break;
            case SDL_EVENT_GAMEPAD_ADDED: if (!pad) pad = SDL_OpenGamepad(e.gdevice.which); break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                if (e.key.key == SDLK_ESCAPE) quit = true;
                if (e.key.key == SDLK_SPACE && !e.key.repeat) setFire(e.key.down, e.key.timestamp);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                const uint8_t mask = e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP ? 0x08
                                   : e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN ? 0x04
                                   : e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ? 0x02
                                   : e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? 0x01 : 0;
                if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)
                    setFire(e.gbutton.down, e.gbutton.timestamp);
                else if (mask)
                    e.gbutton.down ? emu.JoystickPress(mask) : emu.JoystickRelease(mask);
                break;
            }
            default: break;
            }
        }
        if (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK) && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START))
            quit = true;

        // Timer-driven presses at a period that is not a multiple of the frame time, so the press phase
        // relative to the emulated frame sweeps through every position
        if (opt.autofireMs > 0 && deckpoc::NowNs() >= nextAutofire)
        {
            setFire(!fireDown, deckpoc::NowNs());
            nextAutofire += static_cast<uint64_t>(opt.autofireMs * 1e6);
        }

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        const uint64_t t0 = deckpoc::NowNs();
        SDL_GPUTexture* swapchain = nullptr;
        uint32_t sw = 0, sh = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, &sw, &sh))
        {
            SDL_Log("acquire: %s", SDL_GetError());
            break;
        }
        const uint64_t t1 = deckpoc::NowNs();

        deckpoc::FrameInfo info;
        bool haveFrame = emu.FrameSize(info);
        if (haveFrame && (info.width != texW || info.height != texH))
        {
            // NC_VIDEO_MODE_CHANGED equivalent: recreate the texture and transfer buffer for the new size
            if (frameTexture) SDL_ReleaseGPUTexture(device, frameTexture);
            if (transfer) SDL_ReleaseGPUTransferBuffer(device, transfer);
            SDL_GPUTextureCreateInfo ti{};
            ti.type = SDL_GPU_TEXTURETYPE_2D;
            ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;   // core byte order R, G, B, A: no swizzle
            ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
            ti.width = info.width;
            ti.height = info.height;
            ti.layer_count_or_depth = 1;
            ti.num_levels = 1;
            frameTexture = SDL_CreateGPUTexture(device, &ti);
            SDL_GPUTransferBufferCreateInfo bi{};
            bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            bi.size = static_cast<uint32_t>(info.width) * info.height * 4;
            transfer = SDL_CreateGPUTransferBuffer(device, &bi);
            texW = info.width;
            texH = info.height;
        }

        uint64_t copyNs = 0;
        bool borderWhite = false;
        if (haveFrame && frameTexture && swapchain)
        {
            const size_t size = static_cast<size_t>(texW) * texH * 4;
            auto* mapped = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device, transfer, true));
            const uint64_t c0 = deckpoc::NowNs();
            const bool copied = mapped && emu.CopyFrameTo(mapped, size);
            copyNs = deckpoc::NowNs() - c0;
            // Border colour from the bottom-left corner pixel: white (7) vs black (0)
            if (copied)
                borderWhite = mapped[(static_cast<size_t>(texH - 2) * texW + 2) * 4] > 0x80;
            SDL_UnmapGPUTransferBuffer(device, transfer);

            SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = transfer;
            src.pixels_per_row = texW;
            src.rows_per_layer = texH;
            SDL_GPUTextureRegion dst{};
            dst.texture = frameTexture;
            dst.w = texW;
            dst.h = texH;
            dst.d = 1;
            SDL_UploadToGPUTexture(copyPass, &src, &dst, true);
            SDL_EndGPUCopyPass(copyPass);

            const Rect s = CropRect(opt.crop, texW, texH);
            const Rect d = Fit(s, sw, sh);
            SDL_GPUBlitInfo blit{};
            blit.source.texture = frameTexture;
            blit.source.x = s.x; blit.source.y = s.y; blit.source.w = s.w; blit.source.h = s.h;
            blit.destination.texture = swapchain;
            blit.destination.x = d.x; blit.destination.y = d.y; blit.destination.w = d.w; blit.destination.h = d.h;
            blit.load_op = SDL_GPU_LOADOP_CLEAR;
            blit.clear_color = SDL_FColor{0, 0, 0, 1};
            blit.filter = SDL_GPU_FILTER_NEAREST;
            SDL_BlitGPUTexture(cmd, &blit);
        }
        SDL_SubmitGPUCommandBuffer(cmd);
        const uint64_t t2 = deckpoc::NowNs();

        const uint64_t emuFrame = emu.FrameCounter();
        frameLog.Row(t1, "%llu,%llu,%llu,%llu,%lld,%u,%u,%d,%d,%u,%u,0x%llx", static_cast<unsigned long long>((t1 - t0) / 1000),
                     static_cast<unsigned long long>(copyNs / 1000), static_cast<unsigned long long>((t2 - t1) / 1000),
                     static_cast<unsigned long long>(emuFrame), static_cast<long long>(emuFrame - lastEmuFrame),
                     emu.AudioQueuedFrames(), emu.AudioUnderruns(), swapchain ? 1 : 0, borderWhite ? 1 : 0, sw, sh,
                     static_cast<unsigned long long>(SDL_GetWindowFlags(window)));
        lastEmuFrame = emuFrame;

        if (pendingPressNs)
        {
            framesWaited++;
            if (borderWhite == pendingWantWhite)
            {
                const double ms = (t2 - pendingPressNs) / 1e6;
                softwareLatencies.push_back(t2 - pendingPressNs);
                pressLog.Row(pendingPressNs, "%llu,%.3f,%d", static_cast<unsigned long long>(t2), ms, framesWaited);
                pressLog.Flush();
                pendingPressNs = 0;
            }
            else if (framesWaited > 30)
                pendingPressNs = 0;   // a loaded program that does not react: stop waiting
        }

        if (opt.seconds > 0 && (t2 - start) / 1e9 >= opt.seconds)
            quit = true;
    }

    pressLog.Row(deckpoc::NowNs(), "# software latency (event -> submit of the first changed frame): %s",
                 deckpoc::Describe(deckpoc::ComputeSamples(softwareLatencies)).c_str());
    SDL_Log("software latency: %s", deckpoc::Describe(deckpoc::ComputeSamples(softwareLatencies)).c_str());

    emu.Stop();
    if (frameTexture) SDL_ReleaseGPUTexture(device, frameTexture);
    if (transfer) SDL_ReleaseGPUTransferBuffer(device, transfer);
    if (pad) SDL_CloseGamepad(pad);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
