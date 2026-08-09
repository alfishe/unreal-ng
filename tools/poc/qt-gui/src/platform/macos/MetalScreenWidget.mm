#include "MetalScreenWidget.h"
#include "ScreenWidget.h"  // For VideoModeInfo

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import <QuartzCore/CAAnimation.h>
#import <QuartzCore/CAMediaTimingFunction.h>
#import <CoreVideo/CVDisplayLink.h>
#import <Cocoa/Cocoa.h>

#include <QResizeEvent>
#include <QTimer>
#include <QImage>
#include <QDateTime>

// Vertex data for fullscreen quad
struct Vertex {
    float position[2];
    float texCoord[2];
};

static const Vertex kQuadVertices[] = {
    {{-1, -1}, {0, 1}},  // bottom-left
    {{ 1, -1}, {1, 1}},  // bottom-right
    {{-1,  1}, {0, 0}},  // top-left
    {{ 1,  1}, {1, 0}},  // top-right
};

static const char* kShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float2 position [[attribute(0)]];
    float2 texCoord [[attribute(1)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
};

vertex VertexOut vertexShader(VertexIn in [[stage_in]]) {
    VertexOut out;
    out.position = float4(in.position, 0.0, 1.0);
    out.texCoord = in.texCoord;
    return out;
}

fragment float4 fragmentShader(VertexOut in [[stage_in]],
                                texture2d<float> tex [[texture(0)]],
                                sampler samp [[sampler(0)]]) {
    return tex.sample(samp, in.texCoord);
}
)";

// DisplayLink helper to call back into the widget
@interface MetalDisplayLinkHelper : NSObject
{
@public
    MetalScreenWidget* _widget;
    CVDisplayLinkRef _displayLink;
    std::mutex _widgetMutex;  // Guards _widget against teardown race with callback thread
}
- (instancetype)initWithWidget:(MetalScreenWidget*)widget;
- (void)start;
- (void)stop;
@end

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static CVReturn displayLinkCallback(CVDisplayLinkRef displayLink,
                                     const CVTimeStamp* now,
                                     const CVTimeStamp* outputTime,
                                     CVOptionFlags flagsIn,
                                     CVOptionFlags* flagsOut,
                                     void* context)
{
    MetalDisplayLinkHelper* helper = (__bridge MetalDisplayLinkHelper*)context;
    // Render directly on the CVDisplayLink thread (VLC/IINA/MoltenVK pattern).
    // This keeps frames flowing even while the main thread is monopolized by
    // AppKit's fullscreen Space transition — the cause of the ~1s render pause
    // when rendering was dispatched to the main queue.
    std::lock_guard<std::mutex> lock(helper->_widgetMutex);
    if (helper->_widget) {
        helper->_widget->displayLinkCallback();
    }
    return kCVReturnSuccess;
}

@implementation MetalDisplayLinkHelper
- (instancetype)initWithWidget:(MetalScreenWidget*)widget
{
    self = [super init];
    if (self) {
        _widget = widget;
        _displayLink = nullptr;
    }
    return self;
}

- (void)start
{
    if (_displayLink)
        return;

    CVDisplayLinkCreateWithActiveCGDisplays(&_displayLink);
    CVDisplayLinkSetOutputCallback(_displayLink, &displayLinkCallback, (__bridge void*)self);
    CVDisplayLinkStart(_displayLink);
}

- (void)stop
{
    {
        // Blocks until any in-flight callback finishes, then detaches the widget
        std::lock_guard<std::mutex> lock(_widgetMutex);
        _widget = nullptr;
    }
    if (_displayLink) {
        CVDisplayLinkStop(_displayLink);
        CVDisplayLinkRelease(_displayLink);
        _displayLink = nullptr;
    }
}

- (void)dealloc
{
    [self stop];
}
@end

#pragma clang diagnostic pop

struct MetalScreenWidget::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    id<MTLRenderPipelineState> pipelineState = nil;
    id<MTLBuffer> vertexBuffer = nil;
    id<MTLTexture> texture = nil;
    id<MTLSamplerState> sampler = nil;
    CAMetalLayer* metalLayer = nil;
    MetalDisplayLinkHelper* displayLinkHelper = nil;

    int textureWidth = 0;
    int textureHeight = 0;

    double targetAspect = 0;  // 0 = normal mode, >0 = fullscreen layout with this aspect
    bool useTestPattern = true;  // false when emulator provides frames

    // Framebuffer from emulator (like DeviceScreen::devicePixels)
    const uint8_t* framebuffer = nullptr;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
};

MetalScreenWidget::MetalScreenWidget(QWidget* parent)
    : QWidget(parent)
    , m_impl(new Impl)
{
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    // Accept keyboard focus (ScreenWidget has this too) — without it,
    // setFocus() after fullscreen transitions may leave the window with no
    // focus widget at all
    setFocusPolicy(Qt::StrongFocus);

    // No timer - rendering is driven by emulator frame events (NC_VIDEO_FRAME_REFRESH)
    // via updateFrame() which calls render()
}

MetalScreenWidget::~MetalScreenWidget()
{
    stopDisplayLink();
    cleanupMetal();
    @autoreleasepool {
        m_impl->displayLinkHelper = nil;
    }
    delete m_impl;
}

void MetalScreenWidget::initMetal()
{
    if (m_metalInitialized)
        return;

    @autoreleasepool {
        // Get Metal device
        m_impl->device = MTLCreateSystemDefaultDevice();
        if (!m_impl->device) {
            qWarning("Metal is not supported on this device");
            return;
        }

        // Create command queue
        m_impl->commandQueue = [m_impl->device newCommandQueue];

        // Create Metal layer
        m_impl->metalLayer = [CAMetalLayer layer];
        m_impl->metalLayer.device = m_impl->device;
        m_impl->metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        m_impl->metalLayer.framebufferOnly = YES;
        m_impl->metalLayer.contentsScale = devicePixelRatioF();
        // Use 3 drawables for triple buffering to avoid stalls during resize
        m_impl->metalLayer.maximumDrawableCount = 3;
        // Enable vsync (PassThrough/CoreAnimation handles frame timing).
        // This is the default and what SDL2/MoltenVK use. Combined with
        // presentDrawable: (non-blocking) the CPU never waits for the GPU.
        m_impl->metalLayer.displaySyncEnabled = YES;
        // presentsWithTransaction is toggled per-frame in render() (SDL2 pattern):
        // YES only for synchronous resize/transition presents, NO for the normal
        // non-blocking path driven by the CVDisplayLink thread.
        m_impl->metalLayer.presentsWithTransaction = NO;

        // Attach to native view
        NSView* view = (__bridge NSView*)(void*)winId();
        [view setWantsLayer:YES];
        [view setLayer:m_impl->metalLayer];

        // Configure layer to auto-resize with view during animations
        m_impl->metalLayer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
        m_impl->metalLayer.needsDisplayOnBoundsChange = YES;

        view.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
        // TopLeft hides edge glitches during resize
        view.layerContentsPlacement = NSViewLayerContentsPlacementTopLeft;

        // Compile shaders
        NSError* error = nil;
        NSString* shaderString = [NSString stringWithUTF8String:kShaderSource];
        id<MTLLibrary> library = [m_impl->device newLibraryWithSource:shaderString
                                                              options:nil
                                                                error:&error];
        if (error) {
            qWarning("Shader compilation error: %s", [[error localizedDescription] UTF8String]);
            return;
        }

        id<MTLFunction> vertexFunc = [library newFunctionWithName:@"vertexShader"];
        id<MTLFunction> fragmentFunc = [library newFunctionWithName:@"fragmentShader"];

        // Vertex descriptor
        MTLVertexDescriptor* vertexDesc = [MTLVertexDescriptor new];
        vertexDesc.attributes[0].format = MTLVertexFormatFloat2;
        vertexDesc.attributes[0].offset = offsetof(Vertex, position);
        vertexDesc.attributes[0].bufferIndex = 0;
        vertexDesc.attributes[1].format = MTLVertexFormatFloat2;
        vertexDesc.attributes[1].offset = offsetof(Vertex, texCoord);
        vertexDesc.attributes[1].bufferIndex = 0;
        vertexDesc.layouts[0].stride = sizeof(Vertex);

        // Pipeline state
        MTLRenderPipelineDescriptor* pipelineDesc = [MTLRenderPipelineDescriptor new];
        pipelineDesc.vertexFunction = vertexFunc;
        pipelineDesc.fragmentFunction = fragmentFunc;
        pipelineDesc.vertexDescriptor = vertexDesc;
        pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        m_impl->pipelineState = [m_impl->device newRenderPipelineStateWithDescriptor:pipelineDesc
                                                                               error:&error];
        if (error) {
            qWarning("Pipeline creation error: %s", [[error localizedDescription] UTF8String]);
            return;
        }

        // Vertex buffer
        m_impl->vertexBuffer = [m_impl->device newBufferWithBytes:kQuadVertices
                                                           length:sizeof(kQuadVertices)
                                                          options:MTLResourceStorageModeShared];

        // Sampler (nearest neighbor for crisp pixels)
        MTLSamplerDescriptor* samplerDesc = [MTLSamplerDescriptor new];
        samplerDesc.minFilter = MTLSamplerMinMagFilterNearest;
        samplerDesc.magFilter = MTLSamplerMinMagFilterNearest;
        samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        m_impl->sampler = [m_impl->device newSamplerStateWithDescriptor:samplerDesc];

        updateDrawableSize();
        m_metalInitialized = true;

        // Generate initial test pattern and render once
        generateTestPattern();
        render();
    }
}

void MetalScreenWidget::cleanupMetal()
{
    std::lock_guard<std::mutex> lock(m_renderMutex);
    @autoreleasepool {
        m_impl->texture = nil;
        m_impl->sampler = nil;
        m_impl->vertexBuffer = nil;
        m_impl->pipelineState = nil;
        m_impl->commandQueue = nil;
        m_impl->device = nil;
        m_impl->metalLayer = nil;
    }

    m_metalInitialized = false;
}

void MetalScreenWidget::attachFramebuffer(uint16_t width, uint16_t height, void* buffer)
{
    detachFramebuffer();

    m_impl->framebuffer = static_cast<const uint8_t*>(buffer);
    m_impl->framebufferWidth = width;
    m_impl->framebufferHeight = height;
    m_impl->useTestPattern = false;

    m_framebufferSize = QSize(width, height);
    m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    setMinimumSize(width, height);

    updateGeometry();
}

void MetalScreenWidget::detachFramebuffer()
{
    m_impl->framebuffer = nullptr;
    m_impl->framebufferWidth = 0;
    m_impl->framebufferHeight = 0;

    if (m_metalInitialized)
        render();
}

void MetalScreenWidget::refresh()
{
    if (!m_metalInitialized)
        return;

    // Upload texture data from emulator framebuffer
    if (m_impl->framebuffer && m_impl->framebufferWidth > 0 && m_impl->framebufferHeight > 0) {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        @autoreleasepool {
            int width = m_impl->framebufferWidth;
            int height = m_impl->framebufferHeight;

            // Recreate texture if size changed
            if (m_impl->textureWidth != width || m_impl->textureHeight != height) {
                MTLTextureDescriptor* texDesc = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                 width:width
                                                height:height
                                             mipmapped:NO];
                texDesc.usage = MTLTextureUsageShaderRead;
                m_impl->texture = [m_impl->device newTextureWithDescriptor:texDesc];
                m_impl->textureWidth = width;
                m_impl->textureHeight = height;
            }

            // Upload pixel data
            MTLRegion region = MTLRegionMake2D(0, 0, width, height);
            [m_impl->texture replaceRegion:region
                               mipmapLevel:0
                                 withBytes:m_impl->framebuffer
                               bytesPerRow:width * 4];
        }
    }

    // When CVDisplayLink is active, it handles rendering at display refresh rate.
    // Otherwise render immediately for responsive updates.
    if (!m_animating) {
        render();
    }
}

void MetalScreenWidget::setFramebufferSize(int width, int height)
{
    if (m_framebufferSize.width() == width && m_framebufferSize.height() == height)
        return;

    m_framebufferSize = QSize(width, height);
    m_aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    setMinimumSize(width, height);

    // Recreate texture with new size
    if (m_metalInitialized) {
        @autoreleasepool {
            m_impl->texture = nil; // Release old texture
            m_impl->textureWidth = 0;
            m_impl->textureHeight = 0;
        }
    }

    updateGeometry();
}

void MetalScreenWidget::updateFrame(const uint8_t* data, int width, int height)
{
    if (!m_metalInitialized || !data)
        return;

    // Disable test pattern when receiving real frames
    m_impl->useTestPattern = false;

    {
        std::lock_guard<std::mutex> lock(m_renderMutex);
        @autoreleasepool {
            // Recreate texture if size changed
            if (m_impl->textureWidth != width || m_impl->textureHeight != height) {
                MTLTextureDescriptor* texDesc = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                 width:width
                                                height:height
                                             mipmapped:NO];
                texDesc.usage = MTLTextureUsageShaderRead;
                m_impl->texture = [m_impl->device newTextureWithDescriptor:texDesc];
                m_impl->textureWidth = width;
                m_impl->textureHeight = height;
            }

            // Upload pixel data
            MTLRegion region = MTLRegionMake2D(0, 0, width, height);
            [m_impl->texture replaceRegion:region
                               mipmapLevel:0
                                 withBytes:data
                               bytesPerRow:width * 4];
        }
    }

    // When the display link is running it presents this texture on its next
    // tick; render here only if it isn't
    if (!m_animating) {
        render();
    }
}

void MetalScreenWidget::generateTestPattern()
{
    int w = m_framebufferSize.width();
    int h = m_framebufferSize.height();

    std::vector<uint8_t> pixels(w * h * 4);

    // Animated test pattern
    static int frame = 0;
    frame++;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int idx = (y * w + x) * 4;
            // Classic ZX Spectrum color bars style
            int bar = (x * 8) / w;
            bool bright = (y + frame) % 32 < 16;

            uint8_t r = 0, g = 0, b = 0;
            switch (bar) {
                case 0: r = 255; g = 255; b = 255; break; // white
                case 1: r = 255; g = 255; b = 0;   break; // yellow
                case 2: r = 0;   g = 255; b = 255; break; // cyan
                case 3: r = 0;   g = 255; b = 0;   break; // green
                case 4: r = 255; g = 0;   b = 255; break; // magenta
                case 5: r = 255; g = 0;   b = 0;   break; // red
                case 6: r = 0;   g = 0;   b = 255; break; // blue
                case 7: r = 0;   g = 0;   b = 0;   break; // black
            }

            if (!bright) {
                r = r * 3 / 4;
                g = g * 3 / 4;
                b = b * 3 / 4;
            }

            pixels[idx + 0] = r;
            pixels[idx + 1] = g;
            pixels[idx + 2] = b;
            pixels[idx + 3] = 255;
        }
    }

    updateFrame(pixels.data(), w, h);
}

void MetalScreenWidget::render(bool syncPresent)
{
    if (!m_metalInitialized || !m_impl->texture || !m_renderingEnabled)
        return;

    std::lock_guard<std::mutex> lock(m_renderMutex);

    @autoreleasepool {
        // =====================================================================
        // Step 1: Update texture content
        // Only generate test pattern if emulator isn't providing frames
        // =====================================================================
        if (m_impl->useTestPattern) {
            generateTestPattern();
        }

        // =====================================================================
        // Step 2: Acquire next drawable from Metal layer
        // This is the render target we'll draw into.
        // Use non-blocking approach to avoid resize deadlocks.
        // =====================================================================
        m_impl->metalLayer.allowsNextDrawableTimeout = YES;
        id<CAMetalDrawable> drawable = [m_impl->metalLayer nextDrawable];
        if (!drawable)
            return;

        // =====================================================================
        // Step 3: Configure render pass
        // Clear to black - this provides the letterbox/pillarbox background.
        // =====================================================================
        MTLRenderPassDescriptor* passDesc = [MTLRenderPassDescriptor new];
        passDesc.colorAttachments[0].texture = drawable.texture;
        passDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        passDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
        passDesc.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);

        id<MTLCommandBuffer> cmdBuffer = [m_impl->commandQueue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [cmdBuffer renderCommandEncoderWithDescriptor:passDesc];

        // =====================================================================
        // Step 4: Calculate viewport for aspect-ratio-preserved rendering
        // In fullscreen layout mode, we center the content and preserve aspect
        // ratio, creating black bars (letterbox/pillarbox) as needed.
        // This ensures the pre-fullscreen snapshot matches the final layout.
        // =====================================================================
        CGFloat drawableW = drawable.texture.width;
        CGFloat drawableH = drawable.texture.height;

        MTLViewport viewport;
        if (m_impl->targetAspect > 0 && m_impl->textureWidth > 0 && m_impl->textureHeight > 0) {
            // Fullscreen layout: render within target aspect ratio frame
            CGFloat targetAspect = m_impl->targetAspect;
            CGFloat contentAspect = (CGFloat)m_impl->textureWidth / (CGFloat)m_impl->textureHeight;

            // Calculate frame size matching target aspect within drawable
            CGFloat frameW, frameH;
            if (drawableW / drawableH > targetAspect) {
                frameH = drawableH;
                frameW = drawableH * targetAspect;
            } else {
                frameW = drawableW;
                frameH = drawableW / targetAspect;
            }

            // Place content within that frame
            CGFloat vpW, vpH;
            if (contentAspect > targetAspect) {
                vpW = frameW;
                vpH = frameW / contentAspect;
            } else {
                vpH = frameH;
                vpW = frameH * contentAspect;
            }

            // Center in drawable
            CGFloat vpX = (drawableW - vpW) / 2;
            CGFloat vpY = (drawableH - vpH) / 2;

            viewport = {vpX, vpY, vpW, vpH, 0.0, 1.0};
        } else if (m_impl->textureWidth > 0 && m_impl->textureHeight > 0) {
            // Normal mode: preserve aspect ratio via viewport, center content
            CGFloat contentAspect = (CGFloat)m_impl->textureWidth / (CGFloat)m_impl->textureHeight;
            CGFloat drawableAspect = drawableW / drawableH;

            CGFloat vpW, vpH;
            if (contentAspect > drawableAspect) {
                // Content is wider - fit to width, letterbox top/bottom
                vpW = drawableW;
                vpH = drawableW / contentAspect;
            } else {
                // Content is taller - fit to height, pillarbox left/right
                vpH = drawableH;
                vpW = drawableH * contentAspect;
            }

            CGFloat vpX = (drawableW - vpW) / 2;
            CGFloat vpY = (drawableH - vpH) / 2;
            viewport = {vpX, vpY, vpW, vpH, 0.0, 1.0};
        } else {
            // No texture yet - fill drawable
            viewport = {0, 0, drawableW, drawableH, 0.0, 1.0};
        }

        // =====================================================================
        // Step 5: Draw the textured quad
        // The quad covers the viewport, texture is sampled with nearest filter.
        // =====================================================================
        [encoder setViewport:viewport];
        [encoder setRenderPipelineState:m_impl->pipelineState];
        [encoder setVertexBuffer:m_impl->vertexBuffer offset:0 atIndex:0];
        [encoder setFragmentTexture:m_impl->texture atIndex:0];
        [encoder setFragmentSamplerState:m_impl->sampler atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];

        // =====================================================================
        // Step 6: Submit to GPU (SDL2 pattern — two present paths)
        //
        // Sync path (resize/transition, main thread): presentsWithTransaction=YES,
        // commit, waitUntilScheduled, then present the drawable directly. The
        // present is tied to the current CATransaction so the new frame appears
        // in the same commit as the window geometry change — no stretching or
        // stale-content flash at the edges.
        //
        // Async path (normal, CVDisplayLink thread): presentsWithTransaction=NO,
        // presentDrawable BEFORE commit — fully non-blocking, never stalls the
        // calling thread.
        // =====================================================================
        [encoder endEncoding];
        if (syncPresent) {
            m_impl->metalLayer.presentsWithTransaction = YES;
            [cmdBuffer commit];
            [cmdBuffer waitUntilScheduled];
            [drawable present];
        } else {
            m_impl->metalLayer.presentsWithTransaction = NO;
            [cmdBuffer presentDrawable:drawable];
            [cmdBuffer commit];
        }
    }
}

void MetalScreenWidget::updateDrawableSize()
{
    if (!m_impl->metalLayer)
        return;

    // While the zoom owns the layer nobody else touches its geometry
    if (m_zoomActive.load(std::memory_order_relaxed))
        return;

    CGFloat scale = devicePixelRatioF();
    CGFloat w = width();
    CGFloat h = height();

    // Update layer frame to match widget bounds - this keeps the layer
    // properly positioned/sized during resize animations
    m_impl->metalLayer.frame = CGRectMake(0, 0, w, h);
    m_impl->metalLayer.drawableSize = CGSizeMake(w * scale, h * scale);
    m_impl->metalLayer.contentsScale = scale;
}

QSize MetalScreenWidget::sizeHint() const
{
    return m_framebufferSize;
}

void MetalScreenWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    // During the zoom the layer is driven by the CA animation only; AppKit
    // reframes the window several times around the transition and rendering
    // those would fight the animation.
    if (m_zoomActive.load(std::memory_order_relaxed)) {
        emit resized(event->size());
        return;
    }
    updateDrawableSize();
    // Synchronous transaction-tied present: the freshly-sized frame lands in
    // the SAME CATransaction as the geometry change, so content stays glued to
    // the window edges during live resize and fullscreen transitions.
    render(true);
    emit resized(event->size());
}

// Letterbox rect of the content inside a box — identical math to render()'s
// viewport, so a transform built from these maps content exactly onto content.
static CGRect zoomContentRect(CGFloat boxW, CGFloat boxH,
                              double targetAspect, double contentAspect)
{
    if (boxW <= 0 || boxH <= 0 || contentAspect <= 0)
        return CGRectMake(0, 0, boxW, boxH);

    CGFloat frameW = boxW, frameH = boxH;
    if (targetAspect > 0) {
        if (boxW / boxH > targetAspect) { frameH = boxH; frameW = boxH * targetAspect; }
        else                            { frameW = boxW; frameH = boxW / targetAspect; }
    }
    CGFloat w, h;
    if (contentAspect > (frameW / frameH)) { w = frameW; h = frameW / contentAspect; }
    else                                   { h = frameH; w = frameH * contentAspect; }
    return CGRectMake((boxW - w) / 2, (boxH - h) / 2, w, h);
}

CATransform3D MetalScreenWidget::zoomTransformFor(const QRect& rect) const
{
    const double contentAspect = (m_impl->textureWidth > 0 && m_impl->textureHeight > 0)
        ? (double)m_impl->textureWidth / (double)m_impl->textureHeight : 4.0 / 3.0;

    CGRect lb = m_impl->metalLayer.bounds;
    CGRect cLayer = zoomContentRect(lb.size.width, lb.size.height,
                                    m_impl->targetAspect, contentAspect);
    CGRect cSmall = zoomContentRect(rect.width(), rect.height(), 0.0, contentAspect);

    // UNIFORM scale: content-to-content, so the picture never gets squashed
    CGFloat k = (cLayer.size.width > 0) ? (cSmall.size.width / cLayer.size.width) : 1.0;
    CGFloat tx = rect.x() + cSmall.origin.x - k * cLayer.origin.x;
    CGFloat ty = rect.y() + cSmall.origin.y - k * cLayer.origin.y;

    return CATransform3DConcat(CATransform3DMakeScale(k, k, 1),
                               CATransform3DMakeTranslation(tx, ty, 0));
}

void MetalScreenWidget::prepareZoom(const QSize& layerSize, const QRect& contentBox)
{
    if (!m_metalInitialized || layerSize.isEmpty())
        return;

    @autoreleasepool {
        CGFloat scale = devicePixelRatioF();
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        m_impl->metalLayer.frame = CGRectMake(0, 0, layerSize.width(), layerSize.height());
        m_impl->metalLayer.drawableSize =
            CGSizeMake(layerSize.width() * scale, layerSize.height() * scale);
        // Static transform: the oversized layer already appears exactly where
        // the content is now, so switching to the zoom layout is invisible.
        m_impl->metalLayer.transform =
            contentBox.isEmpty() ? CATransform3DIdentity : zoomTransformFor(contentBox);
        [CATransaction commit];
    }

    m_zoomActive.store(true, std::memory_order_relaxed);
    render(false);   // one frame at the new layout, plain async present
}

void MetalScreenWidget::animateZoom(const QRect& fromRect, double duration, bool reverse)
{
    if (!m_metalInitialized || fromRect.isEmpty())
        return;

    @autoreleasepool {
        CATransform3D small = zoomTransformFor(fromRect);

        // Model goes to its final value immediately; the animation only drives
        // presentation. One animator (the render server) — nothing to desync.
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        m_impl->metalLayer.transform = reverse ? small : CATransform3DIdentity;
        [CATransaction commit];

        CABasicAnimation* a = [CABasicAnimation animationWithKeyPath:@"transform"];
        a.fromValue = [NSValue valueWithCATransform3D:(reverse ? CATransform3DIdentity : small)];
        a.toValue = [NSValue valueWithCATransform3D:(reverse ? small : CATransform3DIdentity)];
        a.duration = duration > 0 ? duration : 0.4;
        a.timingFunction =
            [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
        a.removedOnCompletion = NO;
        a.fillMode = kCAFillModeForwards;
        [m_impl->metalLayer addAnimation:a forKey:@"fsZoom"];
    }
}

void MetalScreenWidget::endZoom()
{
    if (!m_metalInitialized)
        return;

    m_zoomActive.store(false, std::memory_order_relaxed);

    @autoreleasepool {
        NSView* view = (__bridge NSView*)(void*)winId();
        NSRect vb = [view bounds];           // Qt's size still lags here
        CGFloat scale = devicePixelRatioF();

        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        [m_impl->metalLayer removeAnimationForKey:@"fsZoom"];
        m_impl->metalLayer.transform = CATransform3DIdentity;
        if (vb.size.width > 0 && vb.size.height > 0) {
            m_impl->metalLayer.frame = CGRectMake(0, 0, vb.size.width, vb.size.height);
            m_impl->metalLayer.drawableSize =
                CGSizeMake(vb.size.width * scale, vb.size.height * scale);
        }

        // Draw the first frame at the new size INSIDE this transaction, with a
        // transaction-tied present. Rendering after the commit left the layer
        // already small while its contents were still the old full-screen
        // drawable: TopLeft placement showed that as a garbage crop in the top
        // left corner, and the real frame arriving a moment later read as a
        // jump there and back.
        render(true);

        [CATransaction commit];
    }
}

void MetalScreenWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_metalInitialized) {
        // Delay init slightly to ensure window is ready
        QTimer::singleShot(0, this, [this]() {
            initMetal();
            // Start permanent always-on rendering (SDL2 pattern).
            // CVDisplayLink drives rendering at display refresh rate for
            // the lifetime of the widget. Emulator frame events only update
            // texture data; the display link handles presentation.
            if (m_metalInitialized) {
                setContinuousRendering(true);
            }
        });
    }
}

bool MetalScreenWidget::event(QEvent* event)
{
    if (event->type() == QEvent::UpdateRequest && m_metalInitialized) {
        render();
        return true;
    }
    return QWidget::event(event);
}

void MetalScreenWidget::loadTestPattern(int modeIndex)
{
    int count = ScreenWidget::videoModeCount();
    if (modeIndex < 0 || modeIndex >= count)
        modeIndex = 2;

    const auto& mode = ScreenWidget::videoModes()[modeIndex];

    // Update framebuffer size and aspect ratio
    m_framebufferSize = QSize(mode.width, mode.height);
    m_aspectRatio = static_cast<float>(mode.width) / static_cast<float>(mode.height);
    setMinimumSize(mode.width, mode.height);

    // Load pattern image
    QImage image(QString::fromLatin1(mode.patternFile));
    if (image.isNull()) {
        image = QImage(mode.width, mode.height, QImage::Format_RGBA8888);
        image.fill(QColor(32, 32, 96));
    }

    if (image.format() != QImage::Format_RGBA8888)
        image = image.convertToFormat(QImage::Format_RGBA8888);

    m_hasTestPattern = true;
    uploadImage(image);
    updateGeometry();
}

void MetalScreenWidget::uploadImage(const QImage& image)
{
    if (!m_metalInitialized)
        return;

    updateFrame(image.constBits(), image.width(), image.height());
}

void MetalScreenWidget::setFullscreenLayout(double targetAspect)
{
    // Set target aspect ratio for fullscreen layout.
    // When > 0, render() centers content within that aspect ratio frame.
    // When 0, content stretches to fill the widget.
    m_impl->targetAspect = targetAspect;
}

void MetalScreenWidget::setContinuousRendering(bool enabled)
{
    m_continuousRendering = enabled;
    if (enabled && !m_animating) {
        m_animating = true;
        startDisplayLink();
    }
}

void MetalScreenWidget::setAnimating(bool animating)
{
    // Transition flag always tracked — resize renders during a transition use
    // the synchronous transaction path
    m_inTransition = animating;

    // Safety net for animation frames BETWEEN our synced presents: scale the
    // last presented (letterboxed) frame proportionally instead of pinning it
    // top-left. TopLeft returns after the transition (best for interactive
    // live resize).
    if (m_metalInitialized) {
        NSView* view = (__bridge NSView*)(void*)winId();
        view.layerContentsPlacement = animating
            ? NSViewLayerContentsPlacementScaleProportionallyToFit
            : NSViewLayerContentsPlacementTopLeft;
    }

    // When continuous rendering is active, the display link runs permanently.
    // Transition code can call setAnimating() freely — nothing to start/stop.
    if (m_continuousRendering)
        return;

    if (m_animating == animating)
        return;

    m_animating = animating;
    if (animating) {
        startDisplayLink();
    } else {
        stopDisplayLink();
    }
}

void MetalScreenWidget::startDisplayLink()
{
    @autoreleasepool {
        if (!m_impl->displayLinkHelper) {
            m_impl->displayLinkHelper = [[MetalDisplayLinkHelper alloc] initWithWidget:this];
        }
        [m_impl->displayLinkHelper start];
    }
}

void MetalScreenWidget::stopDisplayLink()
{
    @autoreleasepool {
        if (m_impl->displayLinkHelper) {
            [m_impl->displayLinkHelper stop];
        }
    }
}

void MetalScreenWidget::displayLinkCallback()
{
    // Runs on the CVDisplayLink thread. No Qt geometry access, no layer.frame
    // changes here — resizeEvent (main thread) owns drawable size updates.
    if (!m_animating || !m_metalInitialized)
        return;

    // SILENT during fullscreen transitions. Async presents here land outside
    // the transactions that carry the geometry, so during the zoom the
    // compositor shows stale surfaces — measured as jerky playback of old
    // frames, visibly worse and slower than staying silent. The only renders
    // in a transition are the single transaction-tied ones from prepareZoom()
    // and endZoom(); the transform animation supplies the motion.
    //
    // TRIED AND REVERTED TWICE: removing this guard. It does remove a short
    // freeze in the dead zones before/after the zoom, but the stale-frame
    // jerking during the zoom itself is a much worse trade.
    if (m_inTransition)
        return;

    render(false);
}
