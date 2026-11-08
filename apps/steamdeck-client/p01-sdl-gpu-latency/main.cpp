// P-01 SDL_GPU latency: the render path of rendering.md §3 with the core's own pacing (core-clocked).
//
// Per display refresh: acquire the swapchain image -> CopyPresentedFramebuffer straight into a mapped
// transfer buffer (cycle) -> upload to the frame texture -> one linear blit (fitted by height, aspect kept; the part
// of the frame shown is the window setting `view`: PictureView, the same rule as unreal-qt)
// -> submit. With a CRT preset (--crt <key>, or WebAPI /video/window crt_profile=shader:<key>) the frame goes through
// the libretro passes of unreal-qt (crtpreset.h) at the picture's size first.
//
// Default program: a border flip on Kempston fire (A / D-pad fire / Space), polled in a tight loop, so the
// border changes within a few T-states of the port read. The app logs, per press, the time from the SDL
// event to the submit of the first frame whose border shows the change ("software latency"); the camera
// measures the rest (README.md). Per frame it logs acquire wait, copy, submit and the emulator frame
// counter step (0 = repeated frame, 2+ = a frame skipped).
//
//   p01-sdl-gpu-latency [--model PENTAGON] [--file game.tap] [--present vsync|mailbox|immediate] [--fif 1|2]
//                       [--delay 0..3] [--view full|screen|content] [--seconds N] [--windowed] [--autofire MS]
//                       [--headless] [--boot] [--sound-haptics] [--crt <preset key>]
//
// Built with automation (build.sh default) the instance is driven over the network: WebAPI on port 8090
// (UNREAL_WEBAPI_PORT). --headless runs the emulator and automation only, no window and no GPU (a container, an
// SSH session); Ctrl+C or --seconds ends it. --boot starts the ROM instead of the border test: the app the Deck
// launcher runs (deck-install.sh), a machine to drive over the WebAPI.
//
// With --boot or --file the controllers library drives the machine (deckcontrollers.h): the Deck's own controller
// through hidraw (every button, paddle, trackpad, the motion sensors, both motors), any other
// pad through evdev, the play and game profiles, the game profiles' haptics (their events; --sound-haptics: the motors
// follow the sound when no game profile has haptics). SDL's gamepad layer stays off then, so nothing else writes the
// Deck's feature reports; the border test keeps the SDL gamepad (A / D-pad on Kempston).
//
// Logs: ~/steamdeck-poc-logs/p01-*.csv. Quit: Esc; the border test also View + Menu, --boot / --file the Steam menu.

#include "crtpreset.h"
#include "deckcontrollers.h"
#include "emuhost.h"
#include "emulator/emulator.h"
#include "emulator/video/displaygeometry.h"
#include "emulator/video/pictureview.h"
#include "emulator/video/windowview.h"
#include "timinglog.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{

struct Options
{
    std::string model = "PENTAGON";   // decodes Kempston at #1F (a bare 48K does not)
    std::string file;
    std::string present = "vsync";
    std::string view = "full";   // the part of the frame shown: full | screen | content (pictureview.h)
    uint32_t framesInFlight = 1;
    int presentDelay = 0;
    double seconds = 0;
    double autofireMs = 0;   // > 0: toggle fire on a timer, no human needed (automated latency runs)
    bool windowed = false;   // development on a desktop: a 1280x800 window instead of fullscreen
    bool headless = false;   // no window, no GPU: the emulator and automation only
    bool boot = false;       // the ROM boots: no border test program
    bool soundHaptics = false;   // no game profile: the motors follow the sound (off: the game profiles' events only)
    std::string crt;         // a CRT preset key (shaders/crt/<key>), "" = none
};

/// The window's display settings for the automation surfaces (WebAPI / CLI / MCP /video/window): the CRT preset is
/// the one setting the Deck client has. The surfaces call from their threads; the render loop picks the change up
class DisplayProvider : public IWindowViewProvider
{
public:
    DisplayProvider(std::vector<std::string> presets, std::string emulatorId, const std::string& initial,
                    PictureView::Mode view = PictureView::Mode::Full)
        : _presets(std::move(presets)), _emulatorId(std::move(emulatorId)), _view(view)
    {
        _last = !initial.empty() ? initial
              : std::find(_presets.begin(), _presets.end(), "crt-lottes") != _presets.end() ? "crt-lottes"
              : _presets.empty() ? "" : _presets.front();
        _active = !initial.empty();
    }

    /// The preset to draw with, "" = none
    std::string Wanted()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _active ? _last : std::string();
    }

    /// TEMPORARY (D-pad preview): the next (+1) or previous (-1) entry of "none" + the presets, wrapping. Returns the
    /// label to show: "<n>/<count> <key>" or "none"
    std::string Step(int delta)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const int count = static_cast<int>(_presets.size()) + 1;   // + "none"
        const auto found = std::find(_presets.begin(), _presets.end(), _last);
        const int current = !_active || found == _presets.end() ? 0 : static_cast<int>(found - _presets.begin()) + 1;
        const int next = (current + delta + count) % count;
        _active = next != 0;
        if (_active)
            _last = _presets[next - 1];
        return _active ? std::to_string(next) + "/" + std::to_string(count - 1) + " " + _last : "none";
    }

    /// The part of the frame shown (the window setting `view`); read per frame by the render loop
    PictureView::Mode View()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _view;
    }

    /// The next view of full, screen, content, wrapping; the label to show: "view <name>"
    std::string StepView()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _view = static_cast<PictureView::Mode>((static_cast<int>(_view) + 1) % static_cast<int>(PictureView::Names().size()));
        return "view " + PictureView::NameOf(_view);
    }

    /// The render loop could not load `key`: back to no preset
    void Failed(const std::string& key)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_last == key)
            _active = false;
    }

    bool GrabWindowView(const std::string&, WindowViewGrab&, std::string& error) override
    {
        error = "The Deck client does not grab its window (use the screenshot source \"screen\")";
        return false;
    }

    bool GetWindowDisplay(WindowDisplaySettings& out, std::string&) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        Report(out);
        return true;
    }

    bool SetWindowDisplay(const WindowDisplayChange& change, WindowDisplaySettings& out, std::string& error) override
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (change.gpu && !*change.gpu)
        {
            error = "The Deck client draws with the GPU only";
            return false;
        }
        if (change.blending.value_or(false))
        {
            error = "The Deck client has no temporal blending";
            return false;
        }
        // A profile other than "none" switches the effects on; crt on alone takes the last preset back
        if (change.crtProfile)
        {
            _active = *change.crtProfile != "none";
            if (_active)
                _last = change.crtProfile->substr(std::strlen(kShader));
        }
        if (change.crt)
            _active = *change.crt && !_last.empty();
        if (change.view)
        {
            PictureView::Mode mode;
            if (!PictureView::Parse(*change.view, mode))
            {
                error = "'view' takes " + PictureView::NameList() + ", not '" + *change.view + "'";
                return false;
            }
            _view = mode;
        }
        Report(out);
        return true;
    }

private:
    static constexpr const char* kShader = "shader:";

    void Report(WindowDisplaySettings& out) const
    {
        out.backend = "gpu";
        out.gpuAvailable = true;
        out.gpu = true;
        out.crt = _active;
        out.crtProfile = _active ? kShader + _last : "none";
        out.crtProfiles = {"none"};
        for (const std::string& key : _presets)
            out.crtProfiles.push_back(kShader + key);
        out.emulatorId = _emulatorId;
        out.view = PictureView::NameOf(_view);
    }

    std::mutex _mutex;
    const std::vector<std::string> _presets;
    const std::string _emulatorId;
    std::string _last;     // the preset crt=on takes
    bool _active = false;
    PictureView::Mode _view = PictureView::Mode::Full;
};

/// TEMPORARY (D-pad preview): a line of text over the picture, drawn with SDL's built-in 8x8 debug font on a software
/// surface, uploaded as a texture and blitted opaque (SDL_GPU's blit has no blending) for a few seconds
class OsdLabel
{
public:
    static constexpr int kScale = 3;                       // the 8x8 glyphs at 24x24 px: readable on the Deck
    static constexpr int kMargin = 8;                      // px around the text inside the box
    static constexpr uint64_t kShowNs = 3'000'000'000ull;  // how long a label stays up

    void Show(SDL_GPUDevice* device, SDL_GPUCommandBuffer* cmd, const std::string& text, uint64_t nowNs)
    {
        Release(device);
        const int glyph = static_cast<int>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE);
        _w = static_cast<uint32_t>(text.size() * glyph * kScale + 2 * kMargin);
        _h = static_cast<uint32_t>(glyph * kScale + 2 * kMargin);
        SDL_Surface* surface = SDL_CreateSurface(_w, _h, SDL_PIXELFORMAT_RGBA32);
        if (!surface)
            return;
        if (SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface))
        {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            SDL_SetRenderScale(renderer, kScale, kScale);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDebugText(renderer, static_cast<float>(kMargin) / kScale, static_cast<float>(kMargin) / kScale,
                                text.c_str());
            SDL_FlushRenderer(renderer);
            SDL_DestroyRenderer(renderer);
        }
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = _w;
        ti.height = _h;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        _texture = SDL_CreateGPUTexture(device, &ti);
        SDL_GPUTransferBufferCreateInfo bi{};
        bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        bi.size = _w * _h * 4;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &bi);
        if (_texture && transfer)
        {
            auto* mapped = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device, transfer, false));
            for (uint32_t row = 0; mapped && row < _h; ++row)   // the surface's pitch may be wider than w * 4
                std::memcpy(mapped + static_cast<size_t>(row) * _w * 4,
                            static_cast<const uint8_t*>(surface->pixels) + static_cast<size_t>(row) * surface->pitch,
                            static_cast<size_t>(_w) * 4);
            SDL_UnmapGPUTransferBuffer(device, transfer);
            SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = transfer;
            src.pixels_per_row = _w;
            src.rows_per_layer = _h;
            SDL_GPUTextureRegion dst{};
            dst.texture = _texture;
            dst.w = _w;
            dst.h = _h;
            dst.d = 1;
            SDL_UploadToGPUTexture(pass, &src, &dst, false);
            SDL_EndGPUCopyPass(pass);
            _until = nowNs + kShowNs;
        }
        if (transfer)
            SDL_ReleaseGPUTransferBuffer(device, transfer);   // released after the copy is recorded: SDL defers it
        SDL_DestroySurface(surface);
    }

    /// On top of the picture, top left; after the main blit (it clears the swapchain)
    void Draw(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchain, uint64_t nowNs) const
    {
        if (!_texture || nowNs >= _until)
            return;
        SDL_GPUBlitInfo blit{};
        blit.source.texture = _texture;
        blit.source.w = _w;
        blit.source.h = _h;
        blit.destination.texture = swapchain;
        blit.destination.x = kMargin;
        blit.destination.y = kMargin;
        blit.destination.w = _w;
        blit.destination.h = _h;
        blit.load_op = SDL_GPU_LOADOP_LOAD;
        blit.filter = SDL_GPU_FILTER_NEAREST;
        SDL_BlitGPUTexture(cmd, &blit);
    }

    void Release(SDL_GPUDevice* device)
    {
        if (_texture)
            SDL_ReleaseGPUTexture(device, _texture);
        _texture = nullptr;
    }

private:
    SDL_GPUTexture* _texture = nullptr;
    uint32_t _w = 0, _h = 0;
    uint64_t _until = 0;
};

} // namespace

int main(int argc, char** argv)
{
    Options opt;
    auto isFlag = [](const char* arg) {
        return std::strcmp(arg, "--windowed") == 0 || std::strcmp(arg, "--headless") == 0 ||
               std::strcmp(arg, "--boot") == 0 || std::strcmp(arg, "--sound-haptics") == 0;
    };
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--windowed") == 0)
            opt.windowed = true;
        if (std::strcmp(argv[i], "--headless") == 0)
            opt.headless = true;
        if (std::strcmp(argv[i], "--boot") == 0)
            opt.boot = true;
        if (std::strcmp(argv[i], "--sound-haptics") == 0)
            opt.soundHaptics = true;
    }
    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (isFlag(argv[i]))
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
        else if (k == "--view")
        {
            PictureView::Mode parsed;
            if (!PictureView::Parse(v, parsed))
            {
                SDL_Log("--view takes %s, not '%s'", PictureView::NameList().c_str(), v.c_str());
                return 2;
            }
            opt.view = PictureView::NameOf(parsed);
        }
        else if (k == "--seconds") opt.seconds = std::stod(v);
        else if (k == "--autofire") opt.autofireMs = std::stod(v);
        else if (k == "--crt") opt.crt = v;
    }

    const bool hosted = !opt.headless && (opt.boot || !opt.file.empty());   // the controllers library has the pads
    const SDL_InitFlags video = SDL_INIT_VIDEO | SDL_INIT_AUDIO | (hosted ? 0u : SDL_INIT_GAMEPAD);
    if (!SDL_Init(opt.headless ? SDL_INIT_EVENTS : video))
    {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    if (opt.headless && !SDL_InitSubSystem(SDL_INIT_AUDIO))
        SDL_Log("audio: %s", SDL_GetError());   // a container has no sound: run silent

    // Emulator
    deckpoc::EmuHost emu;
    std::string error;
    if (!emu.Create(opt.model, &error))
    {
        SDL_Log("emulator: %s", error.c_str());
        return 1;
    }
    // Before the file loads: its NC_FILE_LOADED picks the game profile
    std::unique_ptr<deckpoc::DeckControllers> controllers;
    if (hosted)
    {
        deckpoc::DeckControllers::Options options;
        options.soundHaptics = opt.soundHaptics;
        controllers = std::make_unique<deckpoc::DeckControllers>(*emu.Get(), options);
    }
    if (!opt.file.empty())
    {
        if (!emu.LoadFile(opt.file, &error))
            SDL_Log("load %s: %s", opt.file.c_str(), error.c_str());
    }
    else if (!opt.boot)
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

    if (opt.headless)
    {
        // SDL turns SIGINT / SIGTERM into SDL_EVENT_QUIT
        const uint64_t begin = deckpoc::NowNs();
        bool done = false;
        while (!done)
        {
            SDL_Event e;
            while (SDL_PollEvent(&e))
                done |= e.type == SDL_EVENT_QUIT;
            done |= opt.seconds > 0 && (deckpoc::NowNs() - begin) / 1e9 >= opt.seconds;
            SDL_Delay(50);
        }
        emu.Stop();
        SDL_Quit();
        return 0;
    }

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

    // CRT presets (crt-convert.py next to the binary): SPIR-V, so Vulkan only
    const char* base = SDL_GetBasePath();
    const std::string crtRoot = std::string(base ? base : "") + "shaders/crt";
    const bool spirv = (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_SPIRV) != 0;
    const std::vector<std::string> presets = spirv ? deckpoc::CrtPreset::Available(crtRoot) : std::vector<std::string>();
    if (!opt.crt.empty() && std::find(presets.begin(), presets.end(), opt.crt) == presets.end())
    {
        SDL_Log("CRT preset %s not in %s", opt.crt.c_str(), crtRoot.c_str());
        opt.crt.clear();
    }
    PictureView::Mode viewMode = PictureView::Mode::Full;
    PictureView::Parse(opt.view, viewMode);
    auto display = std::make_shared<DisplayProvider>(presets, emu.Get()->GetId(), opt.crt, viewMode);
    WindowView::SetProvider(display);
    deckpoc::CrtPreset crt;
    std::string crtKey;   // the loaded preset
    // TEMPORARY (D-pad preview): D-pad left / right (router actions crt.prev / crt.next, see README) or the arrow keys
    constexpr uint64_t kCrtStepThrottleNs = 300'000'000ull;   // at most one preset step per 300 ms
    int crtStep = 0;
    bool viewStep = false;   // View > Picture, one step of full / screen / content (router action view.next, or the Up arrow)
    OsdLabel osd;
    std::string osdText;
    uint64_t osdNs = 0;
    if (controllers)
        controllers->SetActionSink([&crtStep, &viewStep, held = std::array<bool, 3>{}](const std::string& action,
                                                                                bool pressed) mutable {
            const int which = action == "crt.next" ? 0 : action == "crt.prev" ? 1 : action == "view.next" ? 2 : -1;
            if (which < 0)
                return;
            SDL_Log("action %s %s", action.c_str(), pressed ? "down" : "up");
            // One step per physical press: the same press can arrive twice (a binding in more than one profile
            // layer), a step is taken on the edge only
            if (pressed && !held[which])
            {
                if (which == 2)
                    viewStep = true;
                else
                    crtStep = which == 0 ? 1 : -1;
            }
            held[which] = pressed;
        });

    SDL_GPUTexture* frameTexture = nullptr;
    SDL_GPUTransferBuffer* transfer = nullptr;
    uint32_t texW = 0, texH = 0;

    // Logs
    deckpoc::CsvLog frameLog;
    frameLog.Open(deckpoc::LogPath("p01-frames", "csv"), "acquire_wait_us,copy_us,submit_us,emu_frame,emu_step,audio_queued,underruns,swapchain_ok,border_white,swapchain_w,swapchain_h,window_flags");
    deckpoc::CsvLog pressLog;
    pressLog.Open(deckpoc::LogPath("p01-presses", "csv"), "event_ns,first_frame_submit_ns,software_latency_ms,frames_waited");
    pressLog.Row(deckpoc::NowNs(), "# model=%s present=%s fif=%u delay=%d view=%s gpu=%s video=%s audio_rate=%u",
                 opt.model.c_str(), opt.present.c_str(), opt.framesInFlight, opt.presentDelay, opt.view.c_str(),
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
                if (e.key.down && !e.key.repeat && e.key.key == SDLK_RIGHT) crtStep = 1;
                if (e.key.down && !e.key.repeat && e.key.key == SDLK_LEFT) crtStep = -1;
                if (e.key.down && !e.key.repeat && e.key.key == SDLK_UP) viewStep = true;
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
        // Game Mode: the window always has the focus. View + Menu is the debug layer key there (pad-default): the
        // Steam menu's Exit game quits
        if (controllers)
            controllers->Poll((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0 || !opt.windowed);

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
            PictureGeometry frameGeometry;
            const bool copied = mapped && emu.CopyFrameTo(mapped, size, &frameGeometry);
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

            if (crtStep != 0 && deckpoc::NowNs() - osdNs < kCrtStepThrottleNs)
                crtStep = 0;   // too soon after the last step: dropped, not queued
            if (crtStep != 0)
            {
                osdText = display->Step(crtStep);
                crtStep = 0;
                osdNs = deckpoc::NowNs();
                osd.Show(device, cmd, osdText, osdNs);
                SDL_Log("CRT: %s", osdText.c_str());
            }
            const std::string wanted = display->Wanted();
            if (wanted != crtKey)
            {
                crtKey = wanted;
                crt.Release();
                if (!crtKey.empty() && !crt.Load(device, crtRoot + "/" + crtKey, &error))
                {
                    SDL_Log("CRT preset %s: %s", crtKey.c_str(), error.c_str());
                    display->Failed(crtKey);
                    crtKey.clear();
                }
            }

            if (viewStep && deckpoc::NowNs() - osdNs >= kCrtStepThrottleNs)
            {
                osdText = display->StepView();
                osdNs = deckpoc::NowNs();
                osd.Show(device, cmd, osdText, osdNs);
                SDL_Log("%s", osdText.c_str());
            }
            viewStep = false;

            // The part of the frame shown and its shape, from the geometry latched with this frame (PictureView, the same
            // rule as unreal-qt): the whole frame, the working picture, or what the layers draw. Stored pixels are not
            // square (TS-Conf: 720x288): the shape comes from DisplayGeometry. Fitted by height, aspect kept. A CRT
            // preset draws on a whole multiple of the source lines and resamples that to this size (crtpreset.h)
            const PictureView::Cut cut =
                PictureView::CutOf(frameGeometry, static_cast<uint16_t>(texW), static_cast<uint16_t>(texH), display->View());
            const PictureRect& s = cut.source;
            const PictureView::Placement d = PictureView::Fit(cut.aspect, sw, sh);
            SDL_GPUBlitInfo blit{};
            blit.source.texture = frameTexture;
            blit.source.x = s.x; blit.source.y = s.y; blit.source.w = s.width; blit.source.h = s.height;
            if (SDL_GPUTexture* shaded = crt.Loaded() ? crt.Render(cmd, frameTexture, s.x, s.y, s.width, s.height, d.width, d.height) : nullptr)
            {
                // The preset drew at the picture's size: 1:1 onto the screen
                blit.source.texture = shaded;
                blit.source.x = 0; blit.source.y = 0; blit.source.w = d.width; blit.source.h = d.height;
            }
            blit.destination.texture = swapchain;
            blit.destination.x = d.x; blit.destination.y = d.y; blit.destination.w = d.width; blit.destination.h = d.height;
            blit.load_op = SDL_GPU_LOADOP_CLEAR;
            blit.clear_color = SDL_FColor{0, 0, 0, 1};
            blit.filter = SDL_GPU_FILTER_LINEAR;   // a fractional scale: nearest would make uneven pixel columns
            SDL_BlitGPUTexture(cmd, &blit);
            osd.Draw(cmd, swapchain, deckpoc::NowNs());
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

    WindowView::SetProvider(nullptr);
    controllers.reset();   // the motors stop, the Deck's lizard mode comes back
    osd.Release(device);
    crt.Release();
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
