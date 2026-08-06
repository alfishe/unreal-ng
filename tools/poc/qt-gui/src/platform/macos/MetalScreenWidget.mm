#include "MetalScreenWidget.h"
#include "ScreenWidget.h"  // For VideoModeInfo

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
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

struct MetalScreenWidget::Impl {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> commandQueue = nil;
    id<MTLRenderPipelineState> pipelineState = nil;
    id<MTLBuffer> vertexBuffer = nil;
    id<MTLTexture> texture = nil;
    id<MTLSamplerState> sampler = nil;
    CAMetalLayer* metalLayer = nil;

    QTimer* renderTimer = nullptr;
    int textureWidth = 0;
    int textureHeight = 0;

    double targetAspect = 0;  // 0 = normal mode, >0 = fullscreen layout with this aspect
};

MetalScreenWidget::MetalScreenWidget(QWidget* parent)
    : QWidget(parent)
    , m_impl(new Impl)
{
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);

    // Render timer for continuous updates (50fps)
    m_impl->renderTimer = new QTimer(this);
    connect(m_impl->renderTimer, &QTimer::timeout, this, [this]() {
        if (m_metalInitialized)
            render();
    });
}

MetalScreenWidget::~MetalScreenWidget()
{
    cleanupMetal();
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

        // Attach to native view
        NSView* view = (__bridge NSView*)(void*)winId();
        [view setWantsLayer:YES];
        [view setLayer:m_impl->metalLayer];

        // Keep layer live during fullscreen transitions (don't use snapshot)
        m_impl->metalLayer.presentsWithTransaction = NO;
        view.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;

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

        // Start rendering
        m_impl->renderTimer->start(20); // ~50fps

        // Generate initial test pattern
        generateTestPattern();
    }
}

void MetalScreenWidget::cleanupMetal()
{
    if (m_impl->renderTimer) {
        m_impl->renderTimer->stop();
    }

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

void MetalScreenWidget::setFramebufferSize(int width, int height)
{
    if (m_framebufferSize.width() == width && m_framebufferSize.height() == height)
        return;

    m_framebufferSize = QSize(width, height);

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

void MetalScreenWidget::render()
{
    if (!m_metalInitialized || !m_impl->texture)
        return;

    @autoreleasepool {
        // =====================================================================
        // Step 1: Update texture content
        // Always generate animated test pattern for debugging transitions
        // =====================================================================
        generateTestPattern();

        // =====================================================================
        // Step 2: Acquire next drawable from Metal layer
        // This is the render target we'll draw into.
        // =====================================================================
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
        } else {
            // Normal mode: stretch to fill entire drawable
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
        // Step 6: Submit to GPU
        // =====================================================================
        [encoder endEncoding];
        [cmdBuffer presentDrawable:drawable];
        [cmdBuffer commit];
    }
}

void MetalScreenWidget::updateDrawableSize()
{
    if (!m_impl->metalLayer)
        return;

    CGFloat scale = devicePixelRatioF();
    CGSize size = CGSizeMake(width() * scale, height() * scale);
    m_impl->metalLayer.drawableSize = size;
    m_impl->metalLayer.contentsScale = scale;
}

QSize MetalScreenWidget::sizeHint() const
{
    return m_framebufferSize;
}

void MetalScreenWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateDrawableSize();
    emit resized(event->size());
}

void MetalScreenWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_metalInitialized) {
        // Delay init slightly to ensure window is ready
        QTimer::singleShot(0, this, &MetalScreenWidget::initMetal);
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

    // Update framebuffer size
    m_framebufferSize = QSize(mode.width, mode.height);
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
