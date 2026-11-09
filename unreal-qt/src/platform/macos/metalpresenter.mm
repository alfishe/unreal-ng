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
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "emulator/video/screenbeat.h"
#include "platform/presentlead.h"
#include "widgets/framesource.h"

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

int64_t SecondsToNs(CFTimeInterval s)
{
    return static_cast<int64_t>(std::llround(s * 1e9));
}

/// IOSurfaces in the ring: one being drawn, one waiting for its time, one on screen
constexpr size_t kSlots = 3;
/// The adaptive lead's first margin over a refresh (LeadNs: presentDrawable:atTime: wants the drawable a refresh ahead)
constexpr int64_t kLeadStartNs = 6'000'000;
/// The GL fence before the Metal pass: polled this long, then waited for (glClientWaitSync with a timeout sleeps in
/// steps of about 10 ms on macOS's GL: the poll serves the usual short waits, p90 0.85 ms), up to the timeout
constexpr int64_t kFencePollNs = 4'000'000;
constexpr int64_t kFenceTimeoutNs = 100'000'000;

/// What the presenter's Metal handlers touch: shared with them, so a handler that runs after the presenter is gone
/// (a window closed, a context lost, a presenter made anew) finds it alive
struct Feedback
{
    std::array<std::atomic<bool>, kSlots> busy{};  // a Metal pass reads the slot
    std::atomic<bool> dumpBusy{false};             // UNREAL_PRESENT_DUMP: a drawable on its way to the file

    mutable std::mutex statsMutex;
    presentstats::Ring errors;         // ms, statsMutex
    presentstats::Ring gpuWaits;       // ms, statsMutex
    presentstats::Ring drawableWaits;  // ms, statsMutex
    ScreenBeat beat;                   // statsMutex
    presentstats::Trace trace;         // UNREAL_PRESENT_TRACE, statsMutex
    std::atomic<uint64_t> presented{0};
    std::atomic<uint64_t> shown{0};
    std::atomic<uint64_t> dropped{0};
    std::atomic<uint64_t> gpuWaitTimeouts{0};
    std::atomic<int64_t> lastPresentedNs{0};  // the newest presentedTime: a refresh of the display
    PresentLead lead{kLeadStartNs};           // LeadNs: a refresh and this margin

    // The window system's state, read on the GUI thread (AppKit's) and cached for any thread
    std::atomic<bool> visible{true};
    std::atomic<int64_t> shortestRefreshNs{0};  // the screen's refresh interval range (0: unknown)
    std::atomic<int64_t> longestRefreshNs{0};
};

/// GUI thread: the window's occlusion and its screen's refresh range into `fb`
void CacheWindowState(NSView* view, Feedback& fb)
{
    NSWindow* window = view.window;
    fb.visible.store(window && (window.occlusionState & NSWindowOcclusionStateVisible) != 0,
                     std::memory_order_relaxed);
    NSScreen* screen = window.screen ?: [NSScreen mainScreen];
    fb.shortestRefreshNs.store(screen.minimumRefreshInterval > 0 ? SecondsToNs(screen.minimumRefreshInterval) : 0,
                               std::memory_order_relaxed);
    fb.longestRefreshNs.store(screen.maximumRefreshInterval > 0 ? SecondsToNs(screen.maximumRefreshInterval) : 0,
                              std::memory_order_relaxed);
}

/// The pipeline that turns the IOSurface over into the drawable: the library compiles from source, once per process
/// (and device)
id<MTLRenderPipelineState> FlipPipeline(id<MTLDevice> device)
{
    static id<MTLDevice> builtFor = nil;
    static id<MTLRenderPipelineState> pipeline = nil;
    if (builtFor == device)
        return pipeline;
    builtFor = device;
    pipeline = nil;
    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:[NSString stringWithUTF8String:kShader] options:nil error:&err];
    if (!lib)
    {
        NSLog(@"MetalPresenter: shader: %@", err);
        return nil;
    }
    MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [lib newFunctionWithName:@"vs"];
    pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!pipeline)
        NSLog(@"MetalPresenter: pipeline: %@", err);
    return pipeline;
}
}  // namespace

/// The display link's target: the link retains it, it holds the presenter's tick by a plain pointer the presenter
/// clears before the link is invalidated (both on the GUI thread, where the link ticks)
@interface UnrealDisplayLinkTarget : NSObject
@property(nonatomic, assign) void* owner;  // MetalPresenter::Impl, nullptr once it is gone
- (void)tick:(id)link;  // a CADisplayLink (macOS 14+; untyped for macOS 12-13 builds)
@end

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
    };

    CAMetalLayer* layer = nil;
    NSView* view = nil;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLRenderPipelineState> pipeline = nil;
    CGLContextObj glContext = nullptr;  // the context the slots' GL objects belong to
    id<NSObject> activity = nil;        // latency critical while the presenter lives: no App Nap, no timer coalescing
    std::array<id<NSObject>, 3> observers{};  // the window system's changes into fb (CacheWindowState)
    std::array<Slot, kSlots> slots;
    size_t current = 0;
    bool drawing = false;
    std::atomic<uint8_t> policy{0};  // a PresentPolicy
    std::string dumpPath;  // UNREAL_PRESENT_DUMP: a drawable as shown, a PPM (from the 200th present, again once deleted)
    std::shared_ptr<Feedback> fb = std::make_shared<Feedback>();

    // b-vsync on the display link (GUI thread)
    id link = nil;  // a CADisplayLink (macOS 14+), made with the first handler
    UnrealDisplayLinkTarget* linkTarget = nil;
    RefreshHandler refreshHandler = nullptr;
    void* refreshContext = nullptr;
    bool wantRefreshes = false;
    std::function<bool()> vrr;  // MetalPresenter::Vrr

    bool Paces() const { return link && refreshHandler && !vrr(); }

    /// The link runs while the window wants refreshes and the presenter paces
    void UpdateLink()
    {
        if (@available(macOS 14.0, *))
        {
            if (link)
                static_cast<CADisplayLink*>(link).paused = !(wantRefreshes && Paces());
        }
    }

    void Tick(id tick)
    {
        if (!refreshHandler || vrr())
        {
            UpdateLink();
            return;
        }
        if (@available(macOS 14.0, *))
        {
            CADisplayLink* l = static_cast<CADisplayLink*>(tick);
            const int64_t refreshNs = SecondsToNs(l.targetTimestamp);
            const int64_t periodNs = SecondsToNs(l.targetTimestamp - l.timestamp);
            refreshHandler(refreshContext, refreshNs, periodNs > 0 ? periodNs : SecondsToNs(l.duration));
        }
    }

    ~Impl()
    {
        if (link)
        {
            linkTarget.owner = nullptr;
            [link invalidate];
            link = nil;
        }
        for (id<NSObject> observer : observers)
        {
            if (observer)
                [[NSNotificationCenter defaultCenter] removeObserver:observer];
        }
        if (activity)
            [[NSProcessInfo processInfo] endActivity:activity];
        ReleaseSlots();
    }

    /// GUI thread: the window system's state cached now and on every change of it
    void WatchWindow()
    {
        CacheWindowState(view, *fb);
        __weak NSView* weakView = view;
        std::shared_ptr<Feedback> shared = fb;
        void (^update)(NSNotification*) = ^(NSNotification*) {
          if (NSView* v = weakView)
              CacheWindowState(v, *shared);
        };
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        NSOperationQueue* main = [NSOperationQueue mainQueue];
        observers[0] = [center addObserverForName:NSWindowDidChangeOcclusionStateNotification
                                           object:nil
                                            queue:main
                                       usingBlock:update];
        observers[1] = [center addObserverForName:NSWindowDidChangeScreenNotification
                                           object:nil
                                            queue:main
                                       usingBlock:update];
        observers[2] = [center addObserverForName:NSApplicationDidChangeScreenParametersNotification
                                           object:nil
                                            queue:main
                                       usingBlock:update];
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
};

MetalPresenter::MetalPresenter() : _impl(std::make_unique<Impl>())
{
}

MetalPresenter::~MetalPresenter()
{
    if (_impl && _impl->queue)
    {
        // The passes in flight read the slots; their handlers hold what they touch (Feedback)
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
    id<MTLRenderPipelineState> pipeline = FlipPipeline(device);
    if (!pipeline)
        return nullptr;

    std::unique_ptr<MetalPresenter> presenter(new MetalPresenter());
    Impl& p = *presenter->_impl;
    p.view = view;
    p.layer = (CAMetalLayer*)view.layer;
    p.device = device;
    p.pipeline = pipeline;
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
    p.WatchWindow();
    MetalPresenter* raw = presenter.get();
    p.vrr = [raw]() { return raw->Vrr(); };
    return presenter;
}

@implementation UnrealDisplayLinkTarget
- (void)tick:(id)link
{
    if (_owner)
        static_cast<MetalPresenter::Impl*>(_owner)->Tick(link);
}
@end

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
    // A window moved to another window or screen without a notification for this view
    CacheWindowState(p.view, *p.fb);
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
    Feedback& fb = *p.fb;
    size_t index = (p.current + 1) % kSlots;
    for (size_t i = 0; i < kSlots && fb.busy[index].load(std::memory_order_acquire); i++)
        index = (index + 1) % kSlots;
    Impl::Slot& slot = p.slots[index];
    if (fb.busy[index].load(std::memory_order_acquire))
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
/// done; fb.dumpBusy cleared then (UNREAL_PRESENT_DUMP: the check that the picture is upright and its colours right
/// without a screen capture)
void EncodeDump(id<MTLCommandBuffer> cb, id<MTLTexture> texture, const std::string& path, std::shared_ptr<Feedback> fb)
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
          fb->dumpBusy.store(false, std::memory_order_release);
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
      fb->dumpBusy.store(false, std::memory_order_release);
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
    const size_t slotIndex = p.current;
    const std::shared_ptr<Feedback> fb = p.fb;

    // The GL commands done before Metal reads the surface: IOSurface sharing has no cross-API fence. Flushed once,
    // polled 0.1 ms apart for the usual short wait, then waited for with a timeout (kFencePollNs)
    const int64_t wait0 = SteadyNowNs();
    GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    GLenum state = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    while (state == GL_TIMEOUT_EXPIRED && SteadyNowNs() - wait0 < kFencePollNs)
    {
        usleep(100);
        state = glClientWaitSync(sync, 0, 0);
    }
    if (state == GL_TIMEOUT_EXPIRED)
    {
        const int64_t left = kFenceTimeoutNs - (SteadyNowNs() - wait0);
        if (left > 0)
            state = glClientWaitSync(sync, 0, static_cast<GLuint64>(left));
    }
    glDeleteSync(sync);
    const double waitMs = static_cast<double>(SteadyNowNs() - wait0) / 1e6;
    if (state != GL_ALREADY_SIGNALED && state != GL_CONDITION_SATISFIED)
    {
        // GL's draw not done (a GPU stalled or gone): Metal would show it half drawn; the picture before stays
        fb->gpuWaitTimeouts.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(fb->statsMutex);
        fb->gpuWaits.Add(waitMs);
        return;
    }

    int64_t drawableWaitNs = 0;
    @autoreleasepool
    {
        const CGSize want = CGSizeMake(slot.width, slot.height);
        if (!CGSizeEqualToSize(p.layer.drawableSize, want))
            p.layer.drawableSize = want;
        const int64_t drawable0 = SteadyNowNs();
        id<CAMetalDrawable> drawable = [p.layer nextDrawable];
        drawableWaitNs = SteadyNowNs() - drawable0;
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
        if (!p.dumpPath.empty() && fb->presented.load(std::memory_order_relaxed) >= 200 &&
            !fb->dumpBusy.load(std::memory_order_acquire) && access(p.dumpPath.c_str(), F_OK) != 0)
        {
            fb->dumpBusy.store(true, std::memory_order_release);
            EncodeDump(cb, drawable.texture, p.dumpPath, fb);
        }

        fb->busy[slotIndex].store(true, std::memory_order_release);
        [cb addCompletedHandler:^(id<MTLCommandBuffer>) {
          fb->busy[slotIndex].store(false, std::memory_order_release);
        }];

        const int64_t now = SteadyNowNs();
        const int64_t asked = std::max(atNs, now);
        const uint64_t number = fb->presented.load(std::memory_order_relaxed) + 1;
        const TraceInfo traced = info;
        // The display's shortest refresh: the beat's unit
        const int64_t refreshNs = fb->shortestRefreshNs.load(std::memory_order_relaxed);
        [drawable addPresentedHandler:^(id<MTLDrawable> d) {
          const CFTimeInterval t = d.presentedTime;
          const int64_t screenNs = t > 0 ? SecondsToNs(t) : 0;
          if (fb->trace.Active())
          {
              std::lock_guard<std::mutex> lock(fb->statsMutex);
              fb->trace.Write(traced, atNs, now, screenNs, drawableWaitNs);
          }
          if (screenNs == 0)
          {
              fb->dropped.fetch_add(1, std::memory_order_relaxed);
              return;
          }
          fb->shown.fetch_add(1, std::memory_order_relaxed);
          fb->lastPresentedNs.store(screenNs, std::memory_order_relaxed);
          // The lead from the frame's own time: a window that handed it over late was late too
          fb->lead.Shown(screenNs - (atNs > 0 ? atNs : asked), number, asked - now);
          std::lock_guard<std::mutex> lock(fb->statsMutex);
          fb->errors.Add(static_cast<double>(screenNs - asked) / 1e6);
          fb->beat.Shown(traced.picture, traced.targetNs, screenNs, refreshNs);
        }];
        if (atNs > now)
            [cb presentDrawable:drawable atTime:static_cast<CFTimeInterval>(atNs) / 1e9];
        else
            [cb presentDrawable:drawable];
        [cb commit];
    }
    fb->presented.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(fb->statsMutex);
    fb->gpuWaits.Add(waitMs);
    fb->drawableWaits.Add(static_cast<double>(drawableWaitNs) / 1e6);
}

uint64_t MetalPresenter::Presented() const
{
    return _impl->fb->presented.load(std::memory_order_relaxed);
}

uint64_t MetalPresenter::LastLatePresent() const
{
    return _impl->fb->lead.LastLate();
}

uint32_t MetalPresenter::LastLateLag() const
{
    return _impl->fb->lead.LastLateLag();
}

uint64_t MetalPresenter::LastOnTimePresent() const
{
    return _impl->fb->lead.LastOnTime();
}

int64_t MetalPresenter::LeadNs(int64_t refreshPeriodNs) const
{
    return refreshPeriodNs + _impl->fb->lead.Margin(refreshPeriodNs);
}

bool MetalPresenter::IsVisible() const
{
    return _impl->fb->visible.load(std::memory_order_relaxed);
}

void MetalPresenter::SetPolicy(PresentPolicy policy)
{
    _impl->policy.store(static_cast<uint8_t>(policy), std::memory_order_relaxed);
    _impl->UpdateLink();
}

bool MetalPresenter::SetRefreshHandler(RefreshHandler handler, void* context)
{
    Impl& p = *_impl;
    p.refreshHandler = handler;
    p.refreshContext = context;
    if (!handler)
    {
        p.UpdateLink();
        return false;
    }
    if (!p.link)
    {
        if (const char* off = getenv("UNREAL_PRESENT_DISPLAY_LINK"); off && std::strcmp(off, "0") == 0)
            return false;
        if (@available(macOS 14.0, *))
        {
            p.linkTarget = [UnrealDisplayLinkTarget new];
            p.linkTarget.owner = &p;
            CADisplayLink* link = [p.view displayLinkWithTarget:p.linkTarget selector:@selector(tick:)];
            if (!link)
            {
                p.linkTarget = nil;
                return false;
            }
            link.paused = YES;
            [link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
            p.link = link;
        }
        else
        {
            return false;
        }
    }
    p.UpdateLink();
    return true;
}

void MetalPresenter::WantRefreshes(bool want)
{
    _impl->wantRefreshes = want;
    _impl->UpdateLink();
}

bool MetalPresenter::PacesRefreshes() const
{
    return _impl->Paces();
}

bool MetalPresenter::Vrr() const
{
    const auto policy = static_cast<PresentPolicy>(_impl->policy.load(std::memory_order_relaxed));
    if (policy != PresentPolicy::Auto)
        return policy == PresentPolicy::Vrr;
    // A refresh range (ProMotion, adaptive sync) shows each present at its time
    const int64_t shortest = _impl->fb->shortestRefreshNs.load(std::memory_order_relaxed);
    const int64_t longest = _impl->fb->longestRefreshNs.load(std::memory_order_relaxed);
    return shortest > 0 && longest - shortest > shortest / 100;
}

bool MetalPresenter::RefreshGrid(int64_t& refreshNs, int64_t& periodNs) const
{
    const int64_t last = _impl->fb->lastPresentedNs.load(std::memory_order_relaxed);
    // vsync on a refresh range: its top rate
    const int64_t shortest = _impl->fb->shortestRefreshNs.load(std::memory_order_relaxed);
    if (last == 0 || shortest <= 0 || Vrr())
        return false;
    refreshNs = last;
    periodNs = shortest;
    return true;
}

MetalPresenter::Stats MetalPresenter::GetStats() const
{
    const Feedback& fb = *_impl->fb;
    Stats s;
    s.presented = fb.presented.load(std::memory_order_relaxed);
    s.shown = fb.shown.load(std::memory_order_relaxed);
    s.dropped = fb.dropped.load(std::memory_order_relaxed);
    s.gpuWaitTimeouts = fb.gpuWaitTimeouts.load(std::memory_order_relaxed);
    s.late = fb.lead.Late();
    s.leadMs = static_cast<double>(fb.lead.PeriodNs() + fb.lead.MarginNs()) / 1e6;
    std::vector<double> errors, waits, drawableWaits;
    {
        std::lock_guard<std::mutex> lock(fb.statsMutex);
        errors = fb.errors.Values();
        waits = fb.gpuWaits.Values();
        drawableWaits = fb.drawableWaits.Values();
        const ScreenBeat::Stats beat = fb.beat.GetStats();
        s.beatFrames = beat.frames;
        s.offBeat = beat.offBeat;
        s.soundOffsetP50 = beat.offsetP50Ms;
        s.soundOffsetP99 = beat.offsetP99Ms;
    }
    presentstats::Fill(s, std::move(errors), std::move(waits), std::move(drawableWaits));
    s.visible = IsVisible();
    const int64_t shortest = fb.shortestRefreshNs.load(std::memory_order_relaxed);
    const int64_t longest = fb.longestRefreshNs.load(std::memory_order_relaxed);
    s.maxRefreshHz = shortest > 0 ? 1e9 / static_cast<double>(shortest) : 0.0;
    s.minRefreshHz = longest > 0 ? 1e9 / static_cast<double>(longest) : 0.0;
    s.vrr = Vrr();
    return s;
}

std::string MetalPresenter::Describe() const
{
    const Stats s = GetStats();
    char line[448];
    std::snprintf(line, sizeof(line),
                  "Metal %s: %llu presented, %llu shown, %llu dropped, %llu a refresh late (lead %.1f ms); "
                  "error p50 %.2f p99 %.2f max %.2f ms; "
                  "GL wait p50 %.2f max %.2f ms, %llu timed out; drawable wait p50 %.2f max %.2f ms; display "
                  "%.1f-%.1f Hz%s, %s%s",
                  PacesRefreshes() ? "display link" : "present-at-time", static_cast<unsigned long long>(s.presented),
                  static_cast<unsigned long long>(s.shown), static_cast<unsigned long long>(s.dropped),
                  static_cast<unsigned long long>(s.late), s.leadMs,
                  s.errorP50, s.errorP99, s.errorMax, s.gpuWaitP50, s.gpuWaitMax,
                  static_cast<unsigned long long>(s.gpuWaitTimeouts), s.drawableWaitP50, s.drawableWaitMax,
                  s.minRefreshHz, s.maxRefreshHz, s.maxRefreshHz - s.minRefreshHz > 1.0 ? " (VRR)" : " (fixed)",
                  s.vrr ? "each frame at its time" : "on the refresh grid", s.visible ? "" : "; occluded");
    return line;
}
