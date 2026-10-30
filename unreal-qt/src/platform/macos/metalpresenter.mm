#include "metalpresenter.h"

#define GL_SILENCE_DEPRECATION

#import <AppKit/AppKit.h>
#import <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#import <OpenGL/CGLIOSurface.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#import <QuartzCore/QuartzCore.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "platform/presentlead.h"

namespace
{
/// The drawable's pixels from the IOSurface, rows turned over (GL counts them from the bottom). Sizes match: read, not
/// sample, so the copy is exact
const char* kShader = R"(
#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; };
vertex V vs(uint id [[vertex_id]]) {
    float2 p = float2((id << 1) & 2, id & 2);
    V v; v.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); return v;
}
fragment float4 fs(V v [[stage_in]], texture2d<float, access::read> t [[texture(0)]]) {
    uint2 xy = uint2(v.pos.xy);
    uint h = t.get_height();
    if (xy.x >= t.get_width() || xy.y >= h)
        return float4(0, 0, 0, 1);
    return float4(t.read(uint2(xy.x, h - 1 - xy.y)).rgb, 1);
}
)";

int64_t SteadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t SecondsToNs(CFTimeInterval s)
{
    return static_cast<int64_t>(std::llround(s * 1e9));
}

/// Statistics window: the last presents
constexpr size_t kStatsWindow = 600;
/// IOSurfaces in the ring: one being drawn, one waiting for its time, one on screen
constexpr size_t kSlots = 3;
/// The adaptive lead's first margin over a refresh (LeadNs: presentDrawable:atTime: wants the drawable a refresh ahead)
constexpr int64_t kLeadStartNs = 6'000'000;
}  // namespace

struct MetalPresenter::Impl
{
    struct Slot
    {
        IOSurfaceRef surface = nullptr;
        id<MTLTexture> texture = nil;
        GLuint glTexture = 0;
        GLuint framebuffer = 0;
        int width = 0;
        int height = 0;
        std::atomic<bool> busy{false};  // a Metal pass reads it
    };

    CAMetalLayer* layer = nil;
    NSView* view = nil;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    CGLContextObj glContext = nullptr;  // the context the slots' GL objects belong to
    id<NSObject> activity = nil;        // latency critical while the presenter lives: no App Nap, no timer coalescing
    std::array<Slot, kSlots> slots;
    size_t current = 0;
    bool drawing = false;

    mutable std::mutex statsMutex;
    std::vector<double> errors;  // ms, ring
    size_t errorNext = 0;
    std::vector<double> gpuWaits;  // ms, ring
    size_t gpuWaitNext = 0;
    std::vector<double> drawableWaits;  // ms, ring
    size_t drawableWaitNext = 0;
    std::atomic<uint64_t> presented{0};
    std::atomic<uint64_t> shown{0};
    std::atomic<uint64_t> dropped{0};
    std::atomic<int64_t> lastPresentedNs{0};  // the newest presentedTime: a refresh of the display
    PresentLead lead{kLeadStartNs};  // LeadNs: a refresh and this margin
    FILE* trace = nullptr;  // UNREAL_PRESENT_TRACE: one line per present (see environment-variables.md)
    std::string dumpPath;   // UNREAL_PRESENT_DUMP: a drawable as shown, a PPM (from the 200th present, again once deleted)
    std::shared_ptr<std::atomic<bool>> dumpBusy = std::make_shared<std::atomic<bool>>(false);

    ~Impl()
    {
        if (activity)
            [[NSProcessInfo processInfo] endActivity:activity];
        ReleaseSlots();
        if (trace)
            fclose(trace);
    }

    void ReleaseSlot(Slot& s)
    {
        // GL objects only in their own context; a context gone takes them with it
        if (glContext && CGLGetCurrentContext() == glContext)
        {
            if (s.framebuffer)
                glDeleteFramebuffers(1, &s.framebuffer);
            if (s.glTexture)
                glDeleteTextures(1, &s.glTexture);
        }
        s.framebuffer = 0;
        s.glTexture = 0;
        s.texture = nil;
        if (s.surface)
            CFRelease(s.surface);
        s.surface = nullptr;
        s.width = s.height = 0;
    }

    void ReleaseSlots()
    {
        for (Slot& s : slots)
            ReleaseSlot(s);
    }

    bool AllocateSlot(Slot& s, int width, int height)
    {
        ReleaseSlot(s);
        NSDictionary* props = @{
            (id)kIOSurfaceWidth : @(width),
            (id)kIOSurfaceHeight : @(height),
            (id)kIOSurfaceBytesPerElement : @4,
            (id)kIOSurfacePixelFormat : @((uint32_t)'BGRA'),
        };
        s.surface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
        if (!s.surface)
            return false;

        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:static_cast<NSUInteger>(width)
                                                                                     height:static_cast<NSUInteger>(height)
                                                                                  mipmapped:NO];
        td.usage = MTLTextureUsageShaderRead;
        td.storageMode = MTLStorageModeManaged;
        s.texture = [device newTextureWithDescriptor:td iosurface:s.surface plane:0];
        if (!s.texture)
            return false;

        glGenTextures(1, &s.glTexture);
        glBindTexture(GL_TEXTURE_RECTANGLE, s.glTexture);
        const CGLError err = CGLTexImageIOSurface2D(glContext, GL_TEXTURE_RECTANGLE, GL_RGBA, width, height, GL_BGRA,
                                                    GL_UNSIGNED_INT_8_8_8_8_REV, s.surface, 0);
        glBindTexture(GL_TEXTURE_RECTANGLE, 0);
        if (err != kCGLNoError)
            return false;
        glGenFramebuffers(1, &s.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, s.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, s.glTexture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return false;
        s.width = width;
        s.height = height;
        return true;
    }

    void AddSample(std::vector<double>& ring, size_t& next, double value)
    {
        if (ring.size() < kStatsWindow)
            ring.push_back(value);
        else
            ring[next] = value;
        next = (next + 1) % kStatsWindow;
    }
};

MetalPresenter::MetalPresenter() : _impl(std::make_unique<Impl>())
{
}

MetalPresenter::~MetalPresenter()
{
    if (_impl && _impl->queue)
    {
        // The passes in flight read the slots
        id<MTLCommandBuffer> last = [_impl->queue commandBuffer];
        [last commit];
        [last waitUntilCompleted];
    }
}

std::unique_ptr<MetalPresenter> MetalPresenter::Create(void* nsView)
{
    NSView* view = (__bridge NSView*)nsView;
    if (!view || ![view.layer isKindOfClass:[CAMetalLayer class]])
        return nullptr;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
        return nullptr;

    std::unique_ptr<MetalPresenter> presenter(new MetalPresenter());
    Impl& p = *presenter->_impl;
    p.view = view;
    p.layer = (CAMetalLayer*)view.layer;
    p.device = device;
    p.queue = [device newCommandQueue];
    p.layer.device = device;
    p.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    if (const char* dumpPath = getenv("UNREAL_PRESENT_DUMP"); dumpPath && *dumpPath)
        p.dumpPath = dumpPath;
    p.layer.framebufferOnly = p.dumpPath.empty();  // the dump reads the drawable back
    p.layer.displaySyncEnabled = YES;
    p.layer.opaque = YES;
    p.layer.maximumDrawableCount = 3;
    // The present timer wakes a few ms before each target: App Nap or timer coalescing would make it late
    p.activity = [[NSProcessInfo processInfo]
        beginActivityWithOptions:NSActivityLatencyCritical | NSActivityUserInitiatedAllowingIdleSystemSleep
                          reason:@"Presenting emulator frames at their audio time"];
    if (const char* tracePath = getenv("UNREAL_PRESENT_TRACE"); tracePath && *tracePath)
        p.trace = fopen(tracePath, "w");

    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:[NSString stringWithUTF8String:kShader] options:nil error:&err];
    if (!lib)
    {
        NSLog(@"MetalPresenter: shader: %@", err);
        return nullptr;
    }
    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vs"];
    pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    p.pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!p.pipeline)
    {
        NSLog(@"MetalPresenter: pipeline: %@", err);
        return nullptr;
    }
    return presenter;
}

std::unique_ptr<NativePresenter> NativePresenter::Create(void* windowHandle)
{
    return MetalPresenter::Create(windowHandle);
}

unsigned MetalPresenter::BeginFrame(int width, int height)
{
    Impl& p = *_impl;
    if (width <= 0 || height <= 0)
        return 0;
    const CGLContextObj context = CGLGetCurrentContext();
    if (!context)
        return 0;
    if (context != p.glContext)
    {
        // A new context: the old one's objects are gone with it
        for (Impl::Slot& s : p.slots)
        {
            s.framebuffer = 0;
            s.glTexture = 0;
        }
        p.ReleaseSlots();
        p.glContext = context;
    }

    // The next slot no pass reads; with all three busy, the oldest pass is waited for
    size_t index = (p.current + 1) % kSlots;
    for (size_t i = 0; i < kSlots && p.slots[index].busy.load(std::memory_order_acquire); i++)
        index = (index + 1) % kSlots;
    Impl::Slot& slot = p.slots[index];
    if (slot.busy.load(std::memory_order_acquire))
    {
        id<MTLCommandBuffer> fence = [p.queue commandBuffer];
        [fence commit];
        [fence waitUntilCompleted];
    }
    if (slot.width != width || slot.height != height || !slot.framebuffer)
    {
        if (!p.AllocateSlot(slot, width, height))
        {
            p.ReleaseSlot(slot);
            return 0;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, slot.framebuffer);
    p.current = index;
    p.drawing = true;
    return slot.framebuffer;
}

namespace
{
/// The drawable's pixels as they go to the screen, row 0 = the top, written to `path` as a binary PPM once the pass is
/// done; `busy` cleared then (UNREAL_PRESENT_DUMP: the check that the picture is upright and its colours right without a screen capture)
void EncodeDump(id<MTLCommandBuffer> cb, id<MTLTexture> texture, const std::string& path,
                std::shared_ptr<std::atomic<bool>> busy)
{
    const NSUInteger width = texture.width;
    const NSUInteger height = texture.height;
    const NSUInteger rowBytes = width * 4;
    id<MTLBuffer> buffer = [cb.device newBufferWithLength:rowBytes * height options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    [blit copyFromTexture:texture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(width, height, 1)
                        toBuffer:buffer
               destinationOffset:0
          destinationBytesPerRow:rowBytes
        destinationBytesPerImage:rowBytes * height];
    [blit endEncoding];
    const std::string file = path;
    [cb addCompletedHandler:^(id<MTLCommandBuffer>) {
      FILE* f = fopen(file.c_str(), "wb");
      if (!f)
      {
          busy->store(false, std::memory_order_release);
          return;
      }
      fprintf(f, "P6\n%lu %lu\n255\n", static_cast<unsigned long>(width), static_cast<unsigned long>(height));
      const auto* bgra = static_cast<const uint8_t*>(buffer.contents);
      std::vector<uint8_t> row(width * 3);
      for (NSUInteger y = 0; y < height; y++)
      {
          for (NSUInteger x = 0; x < width; x++)
          {
              const uint8_t* px = bgra + y * rowBytes + x * 4;
              row[x * 3 + 0] = px[2];
              row[x * 3 + 1] = px[1];
              row[x * 3 + 2] = px[0];
          }
          fwrite(row.data(), 1, row.size(), f);
      }
      fclose(f);
      busy->store(false, std::memory_order_release);
    }];
}
}  // namespace

void MetalPresenter::PresentFrame(int64_t atNs, const TraceInfo& info)
{
    Impl& p = *_impl;
    if (!p.drawing)
        return;
    p.drawing = false;
    Impl::Slot& slot = p.slots[p.current];

    // The GL commands done before Metal reads the surface: IOSurface sharing has no cross-API fence
    const int64_t wait0 = SteadyNs();
    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 100'000'000);
    glDeleteSync(sync);
    const double waitMs = static_cast<double>(SteadyNs() - wait0) / 1e6;

    int64_t drawableWaitNs = 0;
    @autoreleasepool
    {
        const CGSize want = CGSizeMake(slot.width, slot.height);
        if (!CGSizeEqualToSize(p.layer.drawableSize, want))
            p.layer.drawableSize = want;
        const int64_t drawable0 = SteadyNs();
        id<CAMetalDrawable> drawable = [p.layer nextDrawable];
        drawableWaitNs = SteadyNs() - drawable0;
        if (!drawable)
            return;

        id<MTLCommandBuffer> cb = [p.queue commandBuffer];
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = drawable.texture;
        rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
        [enc setRenderPipelineState:p.pipeline];
        [enc setFragmentTexture:slot.texture atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
        if (!p.dumpPath.empty() && p.presented.load(std::memory_order_relaxed) >= 200 &&
            !p.dumpBusy->load(std::memory_order_acquire) && access(p.dumpPath.c_str(), F_OK) != 0)
        {
            p.dumpBusy->store(true, std::memory_order_release);
            EncodeDump(cb, drawable.texture, p.dumpPath, p.dumpBusy);
        }

        slot.busy.store(true, std::memory_order_release);
        Impl::Slot* slotPtr = &slot;
        [cb addCompletedHandler:^(id<MTLCommandBuffer>) {
          slotPtr->busy.store(false, std::memory_order_release);
        }];

        const int64_t now = SteadyNs();
        const int64_t asked = std::max(atNs, now);
        Impl* impl = &p;
        const TraceInfo traced = info;
        [drawable addPresentedHandler:^(id<MTLDrawable> d) {
          const CFTimeInterval t = d.presentedTime;
          if (impl->trace)
          {
              std::lock_guard<std::mutex> lock(impl->statsMutex);
              fprintf(impl->trace, "%llu %lld %lld %lld %lld %lld %lld %lld\n",
                      static_cast<unsigned long long>(traced.serial), static_cast<long long>(atNs),
                      static_cast<long long>(now), static_cast<long long>(t > 0 ? SecondsToNs(t) : 0),
                      static_cast<long long>(traced.latchNs), static_cast<long long>(traced.plannedNs),
                      static_cast<long long>(traced.wokeNs), static_cast<long long>(drawableWaitNs));
          }
          if (t <= 0)
          {
              impl->dropped.fetch_add(1, std::memory_order_relaxed);
              return;
          }
          impl->shown.fetch_add(1, std::memory_order_relaxed);
          impl->lastPresentedNs.store(SecondsToNs(t), std::memory_order_relaxed);
          // The lead from the frame's own time: a window that handed it over late was late too
          impl->lead.Shown(SecondsToNs(t) - (atNs > 0 ? atNs : asked));
          std::lock_guard<std::mutex> lock(impl->statsMutex);
          impl->AddSample(impl->errors, impl->errorNext, static_cast<double>(SecondsToNs(t) - asked) / 1e6);
        }];
        if (atNs > now)
            [cb presentDrawable:drawable atTime:static_cast<CFTimeInterval>(atNs) / 1e9];
        else
            [cb presentDrawable:drawable];
        [cb commit];
    }
    p.presented.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(p.statsMutex);
    p.AddSample(p.gpuWaits, p.gpuWaitNext, waitMs);
    p.AddSample(p.drawableWaits, p.drawableWaitNext, static_cast<double>(drawableWaitNs) / 1e6);
}

int64_t MetalPresenter::LeadNs(int64_t refreshPeriodNs) const
{
    return refreshPeriodNs + _impl->lead.Margin(refreshPeriodNs);
}

bool MetalPresenter::IsVisible() const
{
    NSWindow* window = _impl->view.window;
    return window && (window.occlusionState & NSWindowOcclusionStateVisible) != 0;
}

bool MetalPresenter::RefreshGrid(int64_t& refreshNs, int64_t& periodNs) const
{
    const int64_t last = _impl->lastPresentedNs.load(std::memory_order_relaxed);
    NSScreen* screen = _impl->view.window.screen ?: [NSScreen mainScreen];
    const CFTimeInterval shortest = screen.minimumRefreshInterval;
    const CFTimeInterval longest = screen.maximumRefreshInterval;
    // A refresh range (ProMotion, adaptive sync) shows each present at its time
    if (last == 0 || shortest <= 0 || longest - shortest > shortest * 0.01)
        return false;
    refreshNs = last;
    periodNs = SecondsToNs(shortest);
    return periodNs > 0;
}

MetalPresenter::Stats MetalPresenter::GetStats() const
{
    const Impl& p = *_impl;
    Stats s;
    s.presented = p.presented.load(std::memory_order_relaxed);
    s.shown = p.shown.load(std::memory_order_relaxed);
    s.dropped = p.dropped.load(std::memory_order_relaxed);
    s.late = p.lead.Late();
    s.leadMs = static_cast<double>(p.lead.PeriodNs() + p.lead.MarginNs()) / 1e6;
    std::vector<double> errors, waits, drawableWaits;
    {
        std::lock_guard<std::mutex> lock(p.statsMutex);
        errors = p.errors;
        waits = p.gpuWaits;
        drawableWaits = p.drawableWaits;
    }
    auto at = [](std::vector<double>& v, double q) {
        const size_t i = static_cast<size_t>(q * static_cast<double>(v.size() - 1) + 0.5);
        return v[std::min(i, v.size() - 1)];
    };
    s.window = static_cast<uint32_t>(errors.size());
    if (!errors.empty())
    {
        std::sort(errors.begin(), errors.end());
        s.errorP50 = at(errors, 0.50);
        s.errorP99 = at(errors, 0.99);
        s.errorMax = errors.back();
    }
    if (!waits.empty())
    {
        std::sort(waits.begin(), waits.end());
        s.gpuWaitP50 = at(waits, 0.50);
        s.gpuWaitMax = waits.back();
    }
    if (!drawableWaits.empty())
    {
        std::sort(drawableWaits.begin(), drawableWaits.end());
        s.drawableWaitP50 = at(drawableWaits, 0.50);
        s.drawableWaitMax = drawableWaits.back();
    }
    s.visible = IsVisible();
    NSScreen* screen = p.view.window.screen ?: [NSScreen mainScreen];
    s.maxRefreshHz = screen.minimumRefreshInterval > 0 ? 1.0 / screen.minimumRefreshInterval : 0.0;
    s.minRefreshHz = screen.maximumRefreshInterval > 0 ? 1.0 / screen.maximumRefreshInterval : 0.0;
    return s;
}

std::string MetalPresenter::Describe() const
{
    const Stats s = GetStats();
    char line[384];
    std::snprintf(line, sizeof(line),
                  "Metal present-at-time: %llu presented, %llu shown, %llu dropped, %llu a refresh late (lead %.1f ms); "
                  "error p50 %.2f p99 %.2f max %.2f ms; "
                  "GL wait p50 %.2f max %.2f ms; drawable wait p50 %.2f max %.2f ms; display %.1f-%.1f Hz%s%s",
                  static_cast<unsigned long long>(s.presented), static_cast<unsigned long long>(s.shown),
                  static_cast<unsigned long long>(s.dropped), static_cast<unsigned long long>(s.late), s.leadMs,
                  s.errorP50, s.errorP99, s.errorMax, s.gpuWaitP50,
                  s.gpuWaitMax, s.drawableWaitP50, s.drawableWaitMax, s.minRefreshHz, s.maxRefreshHz,
                  s.maxRefreshHz - s.minRefreshHz > 1.0 ? " (VRR)" : " (fixed)", s.visible ? "" : "; occluded");
    return line;
}
