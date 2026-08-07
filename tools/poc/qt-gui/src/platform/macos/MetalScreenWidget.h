#pragma once

#include <QWidget>
#include <QSize>
#include <QImage>
#include <functional>

// Forward declarations - Metal types defined in .mm
#ifdef __OBJC__
@protocol MTLDevice;
@protocol MTLCommandQueue;
@protocol MTLRenderPipelineState;
@protocol MTLTexture;
@class CAMetalLayer;
#else
typedef void* id;
#endif

class MetalScreenWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MetalScreenWidget(QWidget* parent = nullptr);
    ~MetalScreenWidget() override;

    // Attach/detach emulator framebuffer (like ScreenWidget)
    void attachFramebuffer(uint16_t width, uint16_t height, void* buffer);
    void detachFramebuffer();

    // Set the emulator framebuffer size (e.g., 352x288)
    void setFramebufferSize(int width, int height);

    // Upload new frame data (RGBA8, 4 bytes per pixel)
    void updateFrame(const uint8_t* data, int width, int height);

    // For demo: generate test pattern
    void generateTestPattern();

    // Compatibility with ScreenWidget interface
    void loadTestPattern(int modeIndex);
    QSize nativeSize() const { return m_framebufferSize; }
    void refresh();  // Called on each emulator frame

    // Pre-fullscreen: render with target aspect ratio (pillarbox/letterbox)
    // Pass 0 to disable, or the target screen aspect ratio to enable
    void setFullscreenLayout(double targetAspect);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return m_framebufferSize; }
    QPaintEngine* paintEngine() const override { return nullptr; }

signals:
    void resized(QSize newSize);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool event(QEvent* event) override;

private:
    void initMetal();
    void cleanupMetal();
    void render();
    void updateDrawableSize();
    void uploadImage(const QImage& image);

    struct Impl;
    Impl* m_impl = nullptr;

    QSize m_framebufferSize{1, 1};  // Set by setFramebufferSize() or loadTestPattern()
    float m_aspectRatio = 1.0f;     // Updated when framebuffer size changes
    bool m_metalInitialized = false;
    bool m_hasTestPattern = false;
};
