// POC 037: the Metal presenter on a CAMetalLayer (the layer of a Qt MetalSurface window or of an SDL Metal view).
//
//   b-vsync   CAMetalDisplayLink at the display's rate: every refresh presents the newest latched frame
//   c-vrr     at:   a present thread presents each frame once, presentDrawable:atTime: latch + one period + delay
//             min:  presentDrawable:afterMinimumDuration: one period, as soon as the frame is latched
//             link: CAMetalDisplayLink with preferredFrameRateRange = the source rate, newest frame per callback
//   d-locked  CAMetalDisplayLink at the display's rate drives the producer: tick, wait for the frame, present it
//
// Every present has a presented handler: MTLDrawable.presentedTime is the time the frame reached the screen (0 when
// the system dropped it), on the base of CACurrentMediaTime = mach_absolute_time = NowNs().

#include "presenter.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>

namespace poc037
{
class MetalPresenter;
}

@interface Poc037LinkTarget : NSObject <CAMetalDisplayLinkDelegate>
@property(nonatomic, assign) poc037::MetalPresenter* owner;
- (void)tick:(CADisplayLink*)link;
@end

namespace poc037
{

static const char* kShader = R"(
#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float2 uv; };
vertex V vs(uint id [[vertex_id]]) {
    float2 p = float2((id << 1) & 2, id & 2);
    V v; v.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); v.uv = p; return v;
}
fragment float4 fs(V v [[stage_in]], texture2d<float> t [[texture(0)]]) {
    constexpr sampler s(filter::nearest);
    return t.sample(s, v.uv);
}
)";

static int64_t SecondsToNs(CFTimeInterval s)
{
    return static_cast<int64_t>(std::llround(s * 1e9));
}

class MetalPresenter final : public NativePresenter
{
public:
    MetalPresenter(CAMetalLayer* layer, FrameRing& ring, Producer& producer, PresentLog& log, const Options& o)
        : _layer(layer), _ring(ring), _producer(producer), _log(log), _o(o)
    {
    }
    ~MetalPresenter() override { Stop(); }

    bool Start() override
    {
        _device = MTLCreateSystemDefaultDevice();
        if (!_device)
            return false;
        _queue = [_device newCommandQueue];
        _layer.device = _device;
        _layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        _layer.framebufferOnly = YES;
        _layer.displaySyncEnabled = YES;
        NSError* err = nil;
        id<MTLLibrary> lib = [_device newLibraryWithSource:[NSString stringWithUTF8String:kShader] options:nil error:&err];
        if (!lib)
        {
            NSLog(@"shader: %@", err);
            return false;
        }
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [lib newFunctionWithName:@"vs"];
        pd.fragmentFunction = [lib newFunctionWithName:@"fs"];
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        _pipeline = [_device newRenderPipelineStateWithDescriptor:pd error:&err];
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:kWidth
                                                                                     height:kHeight
                                                                                  mipmapped:NO];
        td.storageMode = MTLStorageModeShared;
        td.usage = MTLTextureUsageShaderRead;
        _texture = [_device newTextureWithDescriptor:td];
        _frame.pixels.assign(static_cast<size_t>(kWidth) * kHeight, 0);
        _period = 1.0 / _o.rate;

        const bool ticked = _o.variant == "d";
        if (ticked)
            _producer.StartTicked();
        else
            _producer.StartClocked();

        const bool link = _o.variant == "b" || _o.variant == "d" || (_o.variant == "c" && _o.vrr == "link");
        _stop = false;
        if (link)
            _thread = std::thread([this] { RunLink(); });
        else
            _thread = std::thread([this] { RunTimed(); });
        return true;
    }

    void Stop() override
    {
        if (_stop.exchange(true))
            return;
        if (_runLoop)
            CFRunLoopStop(_runLoop);
        if (_thread.joinable())
            _thread.join();
        _producer.Stop();
        // Let the last presented handlers land before the log is written
        id<MTLCommandBuffer> last = [_queue commandBuffer];
        [last commit];
        [last waitUntilCompleted];
        SleepUntilNs(NowNs() + 100000000);
    }

    std::string Describe() const override
    {
        std::string s = "Metal CAMetalLayer, ";
        const std::string link = _o.link == "ca" ? "CADisplayLink + nextDrawable" : "CAMetalDisplayLink";
        if (_o.variant == "b")
            s += link + " at the display rate, newest frame per refresh";
        else if (_o.variant == "d")
            s += link + " at the display rate, ticks the producer";
        else if (_o.vrr == "link")
            s += link + " preferredFrameRateRange = source rate";
        else if (_o.vrr == "min")
            s += "presentDrawable:afterMinimumDuration: one period";
        else
            s += "presentDrawable:atTime: latch + period + delay";
        return s;
    }

    /// The CAMetalDisplayLink's callback (its own thread's run loop): the drawable comes with the update
    void OnLink(CAMetalDisplayLinkUpdate* update)
    {
        OnRefresh(update.drawable, SecondsToNs(update.targetTimestamp), SecondsToNs(update.targetPresentationTimestamp));
    }
    /// The CADisplayLink's callback: take the next drawable, present it at once
    void OnTick(CADisplayLink* link)
    {
        OnRefresh(nil, SecondsToNs(link.targetTimestamp), SecondsToNs(link.targetTimestamp));
    }

private:
    void OnRefresh(id<CAMetalDrawable> drawable, int64_t deadlineNs, int64_t target)
    {
        const int64_t tick = NowNs();
        const int64_t cpu0 = ThreadCpuNs();
        if (_o.variant == "d")
        {
            // One emulated frame per refresh: start it now, present it at this refresh if it is ready in time
            _producer.Tick();
            _ring.WaitNewer(_shown, deadlineNs - 1000000);
        }
        if (_ring.CopyAt(0, _frame))
            Upload();
        if (!_haveFrame)
            return;
        if (!drawable)
            drawable = [_layer nextDrawable];
        Encode(drawable, tick, target, cpu0, ^(id<MTLCommandBuffer> cb, id<CAMetalDrawable> d) {
          [cb presentDrawable:d];
        });
    }

    void Upload()
    {
        _haveFrame = true;
        if (_frame.number == _uploaded)
            return;
        [_texture replaceRegion:MTLRegionMake2D(0, 0, kWidth, kHeight)
                    mipmapLevel:0
                      withBytes:_frame.pixels.data()
                    bytesPerRow:kWidth * 4];
        _uploaded = _frame.number;
        _shown = _frame.number;
    }

    void Encode(id<CAMetalDrawable> drawable, int64_t tick, int64_t target, int64_t cpu0,
                void (^present)(id<MTLCommandBuffer>, id<CAMetalDrawable>))
    {
        if (!drawable)
            return;
        id<MTLCommandBuffer> cb = [_queue commandBuffer];
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = drawable.texture;
        rp.colorAttachments[0].loadAction = MTLLoadActionClear;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
        // Letterbox at the source aspect, integer scale when it fits
        const double w = static_cast<double>(drawable.texture.width), h = static_cast<double>(drawable.texture.height);
        double s = std::min(w / kWidth, h / kHeight);
        if (s >= 1)
            s = std::floor(s);
        const double vw = kWidth * s, vh = kHeight * s;
        [enc setViewport:(MTLViewport){(w - vw) / 2, (h - vh) / 2, vw, vh, 0, 1}];
        [enc setRenderPipelineState:_pipeline];
        [enc setFragmentTexture:_texture atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];

        PresentRecord r;
        r.frame = _frame.number;
        r.latchNs = _frame.latchNs;
        r.tickNs = tick;
        r.targetNs = target;
        const size_t rid = _log.Add(r);
        PresentLog* log = &_log;
        [drawable addPresentedHandler:^(id<MTLDrawable> d) {
          log->SetPresented(rid, d.presentedTime > 0 ? SecondsToNs(d.presentedTime) : 0);
        }];
        present(cb, drawable);
        [cb commit];
        _log.SetTiming(rid, NowNs(), ThreadCpuNs() - cpu0);
    }

    void UpdateDrawableSize()
    {
        const CGSize b = _layer.bounds.size;
        const CGFloat sc = _layer.contentsScale;
        const CGSize want = CGSizeMake(std::max<CGFloat>(1, b.width * sc), std::max<CGFloat>(1, b.height * sc));
        if (!CGSizeEqualToSize(_layer.drawableSize, want))
            _layer.drawableSize = want;
    }

    void RunLink()
    {
        RaiseThreadPriority(false);
        @autoreleasepool
        {
            UpdateDrawableSize();
            Poc037LinkTarget* target = [Poc037LinkTarget new];
            target.owner = this;
            const float r = static_cast<float>(_o.rate);
            // c-vrr link: ask for the source rate; b / d: the display's own rate
            const CAFrameRateRange range = _o.variant == "c" ? CAFrameRateRangeMake(r, r, r) : CAFrameRateRangeMake(0, 0, 0);
            CAMetalDisplayLink* metalLink = nil;
            CADisplayLink* caLink = nil;
            if (_o.link == "ca")
            {
                NSScreen* screen = [NSScreen mainScreen];
                caLink = [screen displayLinkWithTarget:target selector:@selector(tick:)];
                caLink.preferredFrameRateRange = range;
                [caLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
            }
            else
            {
                metalLink = [[CAMetalDisplayLink alloc] initWithMetalLayer:_layer];
                metalLink.delegate = target;
                metalLink.preferredFrameLatency = static_cast<float>(_o.frameLatency);
                metalLink.preferredFrameRateRange = range;
                _log.Meta("frame_latency", std::to_string(metalLink.preferredFrameLatency));
                [metalLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
            }
            _runLoop = CFRunLoopGetCurrent();
            while (!_stop)
            {
                @autoreleasepool
                {
                    UpdateDrawableSize();
                    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                             beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.25]];
                }
            }
            [metalLink invalidate];
            [caLink invalidate];
        }
    }

    void RunTimed()
    {
        RaiseThreadPriority(false);
        const int64_t period = static_cast<int64_t>(1e9 / _o.rate);
        uint64_t last = UINT64_MAX;
        while (!_stop)
        {
            @autoreleasepool
            {
                if (!_ring.WaitNewer(last, NowNs() + 100000000))
                    continue;
                const int64_t cpu0 = ThreadCpuNs();
                if (!_ring.CopyAt(0, _frame))
                    continue;
                last = _frame.number;
                Upload();
                UpdateDrawableSize();
                id<CAMetalDrawable> drawable = [_layer nextDrawable];
                const int64_t tick = NowNs();
                if (_o.vrr == "min")
                {
                    const CFTimeInterval p = _period;
                    Encode(drawable, tick, 0, cpu0, ^(id<MTLCommandBuffer> cb, id<CAMetalDrawable> d) {
                      [cb presentDrawable:d afterMinimumDuration:p];
                    });
                }
                else
                {
                    const int64_t t = _frame.latchNs + period + static_cast<int64_t>(_o.delayMs * 1e6);
                    const CFTimeInterval at = static_cast<double>(t) / 1e9;
                    Encode(drawable, tick, t, cpu0, ^(id<MTLCommandBuffer> cb, id<CAMetalDrawable> d) {
                      [cb presentDrawable:d atTime:at];
                    });
                }
            }
        }
    }

    CAMetalLayer* _layer;
    FrameRing& _ring;
    Producer& _producer;
    PresentLog& _log;
    Options _o;
    id<MTLDevice> _device = nil;
    id<MTLCommandQueue> _queue = nil;
    id<MTLRenderPipelineState> _pipeline = nil;
    id<MTLTexture> _texture = nil;
    Frame _frame;
    uint64_t _uploaded = UINT64_MAX;
    uint64_t _shown = UINT64_MAX;
    bool _haveFrame = false;
    double _period = 0.02;
    std::thread _thread;
    std::atomic<bool> _stop{true};
    CFRunLoopRef _runLoop = nullptr;
};

std::unique_ptr<NativePresenter> CreateNativePresenter(void* handle, FrameRing& ring, Producer& producer, PresentLog& log,
                                                       const Options& options)
{
    CAMetalLayer* layer = (__bridge CAMetalLayer*)handle;
    if (![layer isKindOfClass:[CAMetalLayer class]])
        return nullptr;
    return std::make_unique<MetalPresenter>(layer, ring, producer, log, options);
}

void* MetalLayerOfView(void* nsView)
{
    NSView* view = (__bridge NSView*)nsView;
    return [view.layer isKindOfClass:[CAMetalLayer class]] ? (__bridge void*)view.layer : nullptr;
}

DisplayFacts QueryDisplay(void* handle)
{
    DisplayFacts f;
    CAMetalLayer* layer = (__bridge CAMetalLayer*)handle;
    NSScreen* screen = nil;
    for (NSWindow* w in [NSApp windows])
        if (w.contentView.layer == layer || [w.contentView.layer.sublayers containsObject:layer] ||
            (w.contentView.subviews.count && w.contentView.subviews[0].layer == layer))
        {
            screen = w.screen;
            f.visible = (w.occlusionState & NSWindowOcclusionStateVisible) != 0;
        }
    if (!screen)
        screen = [NSScreen mainScreen];
    f.name = screen.localizedName.UTF8String;
    f.maxHz = static_cast<double>(screen.maximumFramesPerSecond);
    f.minHz = screen.maximumRefreshInterval > 0 ? 1.0 / screen.maximumRefreshInterval : f.maxHz;
    f.refreshHz = screen.minimumRefreshInterval > 0 ? 1.0 / screen.minimumRefreshInterval : f.maxHz;
    return f;
}

}  // namespace poc037

@implementation Poc037LinkTarget
- (void)metalDisplayLink:(CAMetalDisplayLink*)link needsUpdate:(CAMetalDisplayLinkUpdate*)update
{
    (void)link;
    self.owner->OnLink(update);
}
- (void)tick:(CADisplayLink*)link
{
    self.owner->OnTick(link);
}
@end
