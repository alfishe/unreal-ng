#pragma once

#include <QWidget>
#include <QSize>
#include <QImage>
#include <functional>
#include <mutex>
#include <atomic>

// Forward declarations - Metal types defined in .mm
#ifdef __OBJC__
#import <QuartzCore/CATransform3D.h>
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

    // Enable/disable continuous rendering for window animations
    void setAnimating(bool animating);

    // Permanent always-on rendering (SDL2 pattern: CVDisplayLink runs at display
    // refresh rate for the lifetime of the widget). When enabled, setAnimating()
    // becomes a no-op so transition code can't accidentally stop rendering.
    void setContinuousRendering(bool enabled);

    // Block all rendering during fullscreen transitions
    void setRenderingEnabled(bool enabled) { m_renderingEnabled = enabled; }

    // ---- Fullscreen zoom: the window teleports, only this layer animates ----
    // Freeze the drawable at layerSize and place the layer so the content
    // appears inside contentBox of the CURRENT window (no animation yet).
    void prepareZoom(const QSize& layerSize, const QRect& contentBox);
    // Animate the layer transform. fromRect is where the content sits at the
    // small end, in the coordinates of the window as it is DURING the zoom.
    void animateZoom(const QRect& fromRect, double duration, bool reverse);
    // Drop the animation and settle on the real view geometry (one transaction).
    void endZoom();
    bool zoomActive() const { return m_zoomActive.load(std::memory_order_relaxed); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return m_framebufferSize; }
    QPaintEngine* paintEngine() const override { return nullptr; }

signals:
    void resized(QSize newSize);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool event(QEvent* event) override;

public:
    void displayLinkCallback();  // Called by CVDisplayLink from Obj-C

private:
    void initMetal();
    void cleanupMetal();
    // syncPresent=true: commit + waitUntilScheduled + present within the current
    // CATransaction (glitch-free resize, main thread only).
    // syncPresent=false: non-blocking presentDrawable + commit (normal path,
    // safe from the CVDisplayLink thread).
    void render(bool syncPresent = false);
    void updateDrawableSize();
#ifdef __OBJC__
    CATransform3D zoomTransformFor(const QRect& rect) const;
#endif
    void uploadImage(const QImage& image);
    void startDisplayLink();
    void stopDisplayLink();

    struct Impl;
    Impl* m_impl = nullptr;

    QSize m_framebufferSize{1, 1};  // Set by setFramebufferSize() or loadTestPattern()
    float m_aspectRatio = 1.0f;     // Updated when framebuffer size changes
    bool m_metalInitialized = false;
    bool m_hasTestPattern = false;
    std::atomic<bool> m_animating{false};
    bool m_continuousRendering = false;
    std::atomic<bool> m_renderingEnabled{true};
    // True during fullscreen enter/exit transitions — resize renders use the
    // synchronous transaction path to stay glued to the animated window frame
    std::atomic<bool> m_inTransition{false};
    // Serializes render()/texture upload between main and CVDisplayLink threads
    std::mutex m_renderMutex;
    // True from prepareZoom() until endZoom(): the layer geometry belongs to
    // the zoom, nobody else may write it
    std::atomic<bool> m_zoomActive{false};
};
