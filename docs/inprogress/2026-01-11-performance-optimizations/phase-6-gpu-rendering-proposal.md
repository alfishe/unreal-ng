# Phase 6: GPU-Accelerated Rendering Proposal

**Status**: Approved - Using Qt RHI with QRhiWidget  
**Priority**: Medium (implement when scaling to 50+ tiles)  
**Estimated Effort**: 2-3 days  
**Chosen Approach**: Qt RHI with QRhiWidget (Qt 6.7+)  

## Overview

This document proposes GPU-accelerated rendering for EmulatorTile widgets to replace the current CPU-based QPainter approach. GPU rendering would significantly reduce CPU overhead when scaling the framebuffer and compositing multiple emulator tiles.

## Current State

### Current Implementation
- `EmulatorTile` inherits from `QWidget`
- Uses `QPainter::drawImage(targetRect, image, sourceRect)` for rendering
- Software scaling in Qt's raster paint engine

### Current Hotspots (from profiling)

| Qt Function | Samples | Purpose |
|:---|---:|:---|
| `qt_blend_argb32_on_argb32_neon` | 40,828 → 3,879 | Image blending/scaling |
| `fetchTransformed_fetcher` | 19,301 → 1,797 | Texture sampling |
| `convertRGBA8888ToARGB32PM_neon` | 6,356 → 606 | Format conversion |

### Optimization Applied
Setting `painter.setRenderHint(QPainter::SmoothPixmapTransform, false)` reduced Qt scaling overhead by **~90%**, but CPU is still involved in:
- Pixel format conversion (RGBA8888 → ARGB32PM)
- Memory copies for scaling
- Software blending

---

## GPU Rendering Options

### ✅ Option 2: Qt RHI with QRhiWidget (Qt 6.7+) - **CHOSEN**

**Cross-platform** (Windows/Linux/macOS), works with Qt 6.

```cpp
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

class EmulatorTile : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    explicit EmulatorTile(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);
    ~EmulatorTile() override;

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    GLuint _textureId = 0;
    GLuint _vao = 0;
    GLuint _vbo = 0;
    QOpenGLShaderProgram* _shaderProgram = nullptr;
    
    std::shared_ptr<Emulator> _emulator;
};
```

**Implementation Sketch:**

```cpp
void EmulatorTile::initializeGL()
{
    initializeOpenGLFunctions();
    
    // Create texture for framebuffer
    glGenTextures(1, &_textureId);
    glBindTexture(GL_TEXTURE_2D, _textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);  // Crisp pixels
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);  // No bilinear
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Create fullscreen quad VAO/VBO
    // ... vertex data setup ...
    
    // Simple texture shader
    _shaderProgram = new QOpenGLShaderProgram(this);
    _shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec2 position;
        layout(location = 1) in vec2 texCoord;
        out vec2 TexCoord;
        void main() {
            gl_Position = vec4(position, 0.0, 1.0);
            TexCoord = texCoord;
        }
    )");
    _shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D screenTexture;
        void main() {
            FragColor = texture(screenTexture, TexCoord);
        }
    )");
    _shaderProgram->link();
}

void EmulatorTile::paintGL()
{
    if (!_emulator) return;
    
    // Get framebuffer from emulator
    FramebufferDescriptor fb = _emulator->GetFramebuffer();
    if (!fb.memoryBuffer) return;
    
    // Upload framebuffer to texture (fast DMA transfer)
    glBindTexture(GL_TEXTURE_2D, _textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fb.width, fb.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, fb.memoryBuffer);
    
    // Draw textured fullscreen quad
    glClear(GL_COLOR_BUFFER_BIT);
    _shaderProgram->bind();
    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 4, 4);
}
```

**CMakeLists.txt Changes:**
```cmake
find_package(Qt6 COMPONENTS OpenGL OpenGLWidgets REQUIRED)
target_link_libraries(unreal-videowall PRIVATE Qt6::OpenGL Qt6::OpenGLWidgets)
```

**Pros:**
- Cross-platform (Windows/Linux/macOS)
- Mature, well-documented Qt API
- GPU handles all scaling and compositing
- Minimal code changes

**Cons:**
- OpenGL deprecated on macOS (but still works via ANGLE in Qt 6)
- Requires shader code maintenance

---

### Option 2: Qt RHI with QRhiWidget (Qt 6.7+) - **CHOSEN**

Qt 6's Rendering Hardware Interface provides native backends:
- **Metal** on macOS (best performance on Apple Silicon)
- **Vulkan** on Linux
- **D3D12** on Windows
- **OpenGL** fallback

**Header (EmulatorTileRhi.h):**
```cpp
#pragma once

#include <QRhiWidget>
#include <rhi/qrhi.h>
#include <memory>

class Emulator;

class EmulatorTileRhi : public QRhiWidget
{
    Q_OBJECT

public:
    explicit EmulatorTileRhi(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);
    ~EmulatorTileRhi() override;

    std::shared_ptr<Emulator> emulator() const { return _emulator; }

protected:
    // QRhiWidget interface
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;

    // Widget events
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void createPipeline();
    void updateTexture();

    std::shared_ptr<Emulator> _emulator;
    std::string _emulatorId;

    // RHI resources
    std::unique_ptr<QRhiTexture> _texture;
    std::unique_ptr<QRhiSampler> _sampler;
    std::unique_ptr<QRhiBuffer> _vertexBuffer;
    std::unique_ptr<QRhiBuffer> _uniformBuffer;
    std::unique_ptr<QRhiShaderResourceBindings> _srb;
    std::unique_ptr<QRhiGraphicsPipeline> _pipeline;

    bool _hasTileFocus = false;
    bool _isDragHovering = false;

    static constexpr int TILE_WIDTH = 512;
    static constexpr int TILE_HEIGHT = 384;
};
```

**Implementation (EmulatorTileRhi.cpp):**
```cpp
#include "EmulatorTileRhi.h"
#include "emulator.h"
#include <rhi/qshader.h>

// Fullscreen quad vertices: position (x,y) + texcoord (u,v)
static const float quadVertices[] = {
    // Position    TexCoord
    -1.0f, -1.0f,  0.0f, 1.0f,  // Bottom-left
     1.0f, -1.0f,  1.0f, 1.0f,  // Bottom-right
    -1.0f,  1.0f,  0.0f, 0.0f,  // Top-left
     1.0f,  1.0f,  1.0f, 0.0f,  // Top-right
};

EmulatorTileRhi::EmulatorTileRhi(std::shared_ptr<Emulator> emulator, QWidget* parent)
    : QRhiWidget(parent), _emulator(emulator)
{
    setFixedSize(TILE_WIDTH, TILE_HEIGHT);
    _emulatorId = _emulator ? _emulator->GetUUID() : "";
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
}

void EmulatorTileRhi::initialize(QRhiCommandBuffer* cb)
{
    QRhi* rhi = this->rhi();
    if (!rhi) return;

    // Create texture for framebuffer (352x288 RGBA)
    _texture.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(352, 288), 1,
                                   QRhiTexture::UsedAsTransferDestination));
    _texture->create();

    // Sampler with nearest-neighbor filtering (crisp pixels)
    _sampler.reset(rhi->newSampler(
        QRhiSampler::Nearest, QRhiSampler::Nearest,  // Min/Mag filter
        QRhiSampler::None,                           // Mipmap
        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    _sampler->create();

    // Vertex buffer for fullscreen quad
    _vertexBuffer.reset(rhi->newBuffer(QRhiBuffer::Immutable,
                                       QRhiBuffer::VertexBuffer,
                                       sizeof(quadVertices)));
    _vertexBuffer->create();

    // Upload vertex data
    QRhiResourceUpdateBatch* resourceUpdates = rhi->nextResourceUpdateBatch();
    resourceUpdates->uploadStaticBuffer(_vertexBuffer.get(), quadVertices);
    cb->resourceUpdate(resourceUpdates);

    createPipeline();
}

void EmulatorTileRhi::createPipeline()
{
    QRhi* rhi = this->rhi();

    // Shader resource bindings (texture + sampler)
    _srb.reset(rhi->newShaderResourceBindings());
    _srb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(
            0, QRhiShaderResourceBinding::FragmentStage,
            _texture.get(), _sampler.get())
    });
    _srb->create();

    // Graphics pipeline
    _pipeline.reset(rhi->newGraphicsPipeline());

    // Vertex input layout: 2 floats position + 2 floats texcoord
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({
        {4 * sizeof(float)}  // stride
    });
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},                      // position
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}       // texcoord
    });

    _pipeline->setVertexInputLayout(inputLayout);
    _pipeline->setShaderResourceBindings(_srb.get());
    _pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    _pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);

    // Load shaders (would be compiled .qsb files)
    // _pipeline->setShaderStages({ vertexShader, fragmentShader });

    _pipeline->create();
}

void EmulatorTileRhi::render(QRhiCommandBuffer* cb)
{
    if (!_emulator) return;

    QRhi* rhi = this->rhi();

    // Update texture with current framebuffer
    FramebufferDescriptor fb = _emulator->GetFramebuffer();
    if (fb.memoryBuffer && fb.width > 0 && fb.height > 0)
    {
        QRhiResourceUpdateBatch* resourceUpdates = rhi->nextResourceUpdateBatch();
        QRhiTextureSubresourceUploadDescription subresDesc(
            static_cast<const uchar*>(fb.memoryBuffer),
            fb.width * fb.height * 4);  // RGBA
        subresDesc.setSourceSize(QSize(fb.width, fb.height));
        resourceUpdates->uploadTexture(_texture.get(), QRhiTextureUploadEntry(0, 0, subresDesc));
        cb->resourceUpdate(resourceUpdates);
    }

    // Begin render pass
    const QColor clearColor = Qt::black;
    cb->beginPass(renderTarget(), clearColor, {1.0f, 0}, nullptr);

    // Draw fullscreen quad
    cb->setGraphicsPipeline(_pipeline.get());
    cb->setViewport({0, 0, float(TILE_WIDTH), float(TILE_HEIGHT)});
    cb->setShaderResources();

    const QRhiCommandBuffer::VertexInput vbufBinding(_vertexBuffer.get(), 0);
    cb->setVertexInput(0, 1, &vbufBinding);
    cb->draw(4);

    cb->endPass();
}

void EmulatorTileRhi::releaseResources()
{
    _pipeline.reset();
    _srb.reset();
    _uniformBuffer.reset();
    _vertexBuffer.reset();
    _sampler.reset();
    _texture.reset();
}
```

**CMakeLists.txt Changes:**
```cmake
find_package(Qt6 COMPONENTS Gui Widgets REQUIRED)
# QRhiWidget is part of Qt6::Gui in Qt 6.7+
target_link_libraries(unreal-videowall PRIVATE Qt6::Gui Qt6::Widgets)
```

**Shader Files (compile with `qsb` tool):**

`texture.vert`:
```glsl
#version 440
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texCoord;
layout(location = 0) out vec2 v_texCoord;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    v_texCoord = texCoord;
}
```

`texture.frag`:
```glsl
#version 440
layout(location = 0) in vec2 v_texCoord;
layout(location = 0) out vec4 fragColor;
layout(binding = 0) uniform sampler2D tex;

void main()
{
    // Sample with nearest-neighbor (set in sampler)
    // Extract 256x192 region from 352x288 (offset 48,48)
    vec2 screenCoord = v_texCoord * vec2(256.0/352.0, 192.0/288.0) + vec2(48.0/352.0, 48.0/288.0);
    fragColor = texture(tex, screenCoord);
}
```

**Compile shaders:**
```bash
qsb --glsl 100es,120,150 --hlsl 50 --msl 12 -o texture.vert.qsb texture.vert
qsb --glsl 100es,120,150 --hlsl 50 --msl 12 -o texture.frag.qsb texture.frag
```

**Pros:**
- **Native Metal on macOS** (best performance on Apple Silicon)
- **Native Vulkan on Linux** (optimal for AMD/NVIDIA)
- **Native D3D12 on Windows** (best for modern Windows)
- Future-proof (Qt's recommended rendering path)
- Single codebase for all backends

**Cons:**
- Requires Qt 6.7+
- More complex API than QOpenGLWidget
- Need to compile shaders for each backend

---

### Option 3: Qt Quick / QML

Use QML's GPU-accelerated scene graph:

```qml
// EmulatorTile.qml
Item {
    width: 512
    height: 384
    
    Image {
        anchors.fill: parent
        source: "image://emulator/" + emulatorId
        smooth: false  // Nearest-neighbor scaling
        cache: false   // Always fetch fresh frame
    }
}
```

**C++ Image Provider:**
```cpp
class EmulatorImageProvider : public QQuickImageProvider
{
public:
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        auto* emulator = EmulatorManager::instance()->getById(id);
        if (!emulator) return QImage();
        
        auto fb = emulator->GetFramebuffer();
        return QImage(fb.memoryBuffer, fb.width, fb.height, QImage::Format_RGBA8888);
    }
};
```

**Pros:**
- Declarative, easy to maintain
- GPU-accelerated by default
- Good for dynamic layouts

**Cons:**
- Requires QML integration
- Image provider polling overhead
- Mixed QWidget/QML complexity

---

### Option 4: Native Metal (macOS Only)

Direct Metal rendering via `CAMetalLayer`:

```objc
// MetalEmulatorTile.mm
@interface MetalTileView : NSView
@property (nonatomic) CAMetalLayer* metalLayer;
@property (nonatomic) id<MTLDevice> device;
@property (nonatomic) id<MTLTexture> framebufferTexture;
@end
```

**Pros:**
- Maximum macOS performance
- Zero-copy texture upload possible

**Cons:**
- macOS only
- Significant platform-specific code
- Objective-C++ mixing

---

## Recommendation

### ✅ Chosen: Qt RHI with QRhiWidget

Based on project requirements:
- **Qt 6.x already in use** - upgrade to 6.7+ is feasible
- **Cross-platform** - macOS, Linux, Windows all supported
- **Native performance** - Metal on macOS is optimal for Apple Silicon
- **Future-proof** - RHI is Qt's strategic direction

### When to Implement

Implement when:
- Scaling to 50+ emulator tiles
- CPU becomes bottleneck again after current optimizations
- Need consistent 60fps across all tiles

---

## Performance Expectations

| Approach | CPU Scaling | GPU Scaling | Compositing | Overhead |
|:---|:---:|:---:|:---:|:---|
| **Current (QWidget + QPainter)** | ✅ | ❌ | CPU | ~6K samples |
| **QOpenGLWidget** | ❌ | ✅ | GPU | Near-zero |
| **Qt RHI** | ❌ | ✅ | GPU | Near-zero |
| **Qt Quick/QML** | ❌ | ✅ | GPU | Image provider overhead |

---

## Implementation Checklist

When ready to implement:

- [ ] Ensure Qt 6.7+ is available in build environment
- [ ] Create `EmulatorTileRhi.h` and `EmulatorTileRhi.cpp`
- [ ] Compile shaders with `qsb` tool for all backends
- [ ] Add shader .qsb files to Qt resources
- [ ] Implement `initialize()` with texture, sampler, pipeline setup
- [ ] Implement `render()` with texture upload and quad draw
- [ ] Handle focus/keyboard/drag-drop events
- [ ] Add border overlay for focus/drag visual feedback
- [ ] Update `VideoWallWindow` to use `EmulatorTileRhi`
- [ ] Test on macOS (Metal), Linux (Vulkan), Windows (D3D12)
- [ ] Benchmark vs current QPainter implementation
- [ ] Consider PBO/staging buffer for async texture upload

---

## References

- [QRhiWidget Documentation (Qt 6.7+)](https://doc.qt.io/qt-6/qrhiwidget.html)
- [Qt RHI Overview](https://doc.qt.io/qt-6/qtgui-index.html#rendering-hardware-interface)
- [QShader and qsb Tool](https://doc.qt.io/qt-6/qtshadertools-index.html)
- [Qt RHI Examples](https://doc.qt.io/qt-6/qtgui-rhiwindow-example.html)
- [Metal Best Practices](https://developer.apple.com/metal/Metal-Best-Practices-Guide.pdf)
