// POC 037, the SDL3 shell.
//
//   --backend gpu     SDL_GPU, present mode VSYNC
//                     a-signal: wait for the producer's next frame, then acquire and present it
//                     b-vsync:  acquire (blocks to the refresh), present the newest frame
//   --backend native  the SDL window's Metal layer (macOS) or HWND (Windows) handed to the native presenter
//
//   poc037-sdl --backend gpu --variant b --rate zx48 --seconds 60 --out <dir>

#include "../common/lab.h"
#include "../common/presenter.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <memory>

using namespace poc037;

namespace
{

void RunGpu(SDL_Window* window, FrameRing& ring, Producer& producer, PresentLog& log, const Options& o)
{
    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_DXIL, false, nullptr);
    if (!device || !SDL_ClaimWindowForGPUDevice(device, window))
    {
        SDL_Log("SDL_GPU: %s", SDL_GetError());
        return;
    }
    log.Meta("presenter", std::string("SDL_GPU ") + SDL_GetGPUDeviceDriver(device) + ", VSYNC, " +
                              (o.variant == "a" ? "present per produced frame" : "newest frame per acquire"));
    SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = kWidth;
    ti.height = kHeight;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &ti);
    SDL_GPUTransferBufferCreateInfo bi{};
    bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    bi.size = kWidth * kHeight * 4;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &bi);

    producer.StartClocked();
    Frame frame;
    uint64_t uploaded = UINT64_MAX;
    const int64_t end = NowNs() + static_cast<int64_t>(o.seconds * 1e9);
    bool quit = false;
    while (!quit && NowNs() < end)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
                quit = true;
        if (o.variant == "a" && !ring.WaitNewer(uploaded, NowNs() + 100000000))
            continue;
        const int64_t cpu0 = ThreadCpuNs();
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUTexture* swap = nullptr;
        uint32_t sw = 0, sh = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &sw, &sh) || !swap)
        {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }
        const int64_t tick = NowNs();
        if (ring.CopyAt(0, frame) && frame.number != uploaded)
        {
            void* p = SDL_MapGPUTransferBuffer(device, transfer, true);
            std::memcpy(p, frame.pixels.data(), bi.size);
            SDL_UnmapGPUTransferBuffer(device, transfer);
            SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = transfer;
            src.pixels_per_row = kWidth;
            src.rows_per_layer = kHeight;
            SDL_GPUTextureRegion dst{};
            dst.texture = tex;
            dst.w = kWidth;
            dst.h = kHeight;
            dst.d = 1;
            SDL_UploadToGPUTexture(cp, &src, &dst, true);
            SDL_EndGPUCopyPass(cp);
            uploaded = frame.number;
        }
        if (uploaded != UINT64_MAX)
        {
            const uint32_t s = std::max<uint32_t>(1, std::min(sw / kWidth, sh / kHeight));
            SDL_GPUBlitInfo blit{};
            blit.source.texture = tex;
            blit.source.w = kWidth;
            blit.source.h = kHeight;
            blit.destination.texture = swap;
            blit.destination.x = (sw - kWidth * s) / 2;
            blit.destination.y = (sh - kHeight * s) / 2;
            blit.destination.w = kWidth * s;
            blit.destination.h = kHeight * s;
            blit.load_op = SDL_GPU_LOADOP_CLEAR;
            blit.clear_color = SDL_FColor{0, 0, 0, 1};
            blit.filter = SDL_GPU_FILTER_NEAREST;
            SDL_BlitGPUTexture(cmd, &blit);
        }
        SDL_SubmitGPUCommandBuffer(cmd);
        if (uploaded != UINT64_MAX)
        {
            PresentRecord r;
            r.frame = frame.number;
            r.latchNs = frame.latchNs;
            r.tickNs = tick;
            r.submitNs = NowNs();
            r.cpuNs = ThreadCpuNs() - cpu0;
            log.Add(r);
        }
    }
    producer.Stop();
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTexture(device, tex);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
}

}  // namespace

int main(int argc, char** argv)
{
    const Options o = ParseOptions(argc, argv);
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return 1;
    }
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE;
    if (o.fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
#ifdef __APPLE__
    if (o.backend == "native")
        flags |= SDL_WINDOW_METAL;
#endif
    SDL_Window* window = SDL_CreateWindow("POC 037 SDL3", kWidth * o.scale, kHeight * o.scale, flags);
    if (!window)
    {
        SDL_Log("window: %s", SDL_GetError());
        return 1;
    }
    SDL_RaiseWindow(window);
    FrameRing ring;
    Producer producer(ring, o.rate, o.workMs);
    PresentLog log;
    const int v = SDL_GetVersion();
    log.Meta("shell", "SDL " + std::to_string(SDL_VERSIONNUM_MAJOR(v)) + "." + std::to_string(SDL_VERSIONNUM_MINOR(v)) + "." +
                          std::to_string(SDL_VERSIONNUM_MICRO(v)));
    log.Meta("backend", o.backend);
    log.Meta("variant", o.variant);
    log.Meta("rate_hz", std::to_string(o.rate));
    log.Meta("delay_ms", std::to_string(o.delayMs));
    log.Meta("fullscreen", o.fullscreen ? "1" : "0");
    if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)))
    {
        log.Meta("display", SDL_GetDisplayName(SDL_GetDisplayForWindow(window)));
        log.Meta("display_hz", std::to_string(m->refresh_rate_numerator ? static_cast<double>(m->refresh_rate_numerator) / m->refresh_rate_denominator : m->refresh_rate));
    }

    if (o.backend == "gpu")
    {
        RunGpu(window, ring, producer, log, o);
    }
    else
    {
        void* handle = nullptr;
#ifdef __APPLE__
        SDL_MetalView view = SDL_Metal_CreateView(window);
        handle = SDL_Metal_GetLayer(view);
#elif defined(_WIN32)
        handle = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#endif
        std::unique_ptr<NativePresenter> presenter = CreateNativePresenter(handle, ring, producer, log, o);
        if (!presenter || !presenter->Start())
        {
            SDL_Log("the native presenter did not start");
            return 1;
        }
        log.Meta("presenter", presenter->Describe());
        const DisplayFacts d = QueryDisplay(handle);
        log.Meta("display_min_hz", std::to_string(d.minHz));
        log.Meta("display_max_hz", std::to_string(d.maxHz));
        const int64_t end = NowNs() + static_cast<int64_t>(o.seconds * 1e9);
        bool quit = false;
        while (!quit && NowNs() < end)
        {
            SDL_Event e;
            while (SDL_WaitEventTimeout(&e, 50))
                if (e.type == SDL_EVENT_QUIT || (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE))
                    quit = true;
        }
        log.Meta("visible_at_end", QueryDisplay(handle).visible ? "1" : "0");
        presenter->Stop();
    }
    producer.Stop();
    log.Meta("frames_produced", std::to_string(producer.Frames()));
    log.Meta("producer_jitter_p99_ms", std::to_string(producer.LatchJitterP99Ms()));
    log.Write(RunPath(o, "sdl"));
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
