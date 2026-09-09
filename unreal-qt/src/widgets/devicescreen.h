#ifndef DEVICESCREEN_H
#define DEVICESCREEN_H

#include <QImage>
#include <QWidget>
#include <functional>
#include <memory>
#include <cstdint>

#include "emulator/video/screen.h"  // For DisplayViewport
#include "framehistory.h"
#include "crtprofiles.h"

class CRTFilter;

class Emulator;  // Forward declaration

namespace Ui
{
class DeviceScreen;
}

class DeviceScreen : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceScreen(QWidget* parent = nullptr);
    ~DeviceScreen() override;

public:
    void init(uint16_t width, uint16_t height, void* buffer);
    void detach();

    /// Copies the latched (tear-free) frame into dst; returns true on success.
    using FrameCopyFn = std::function<bool(uint8_t* dst, size_t dstSize)>;

    /// When set, paintEvent pulls frames into an owned backing image via this
    /// callback (Screen::CopyPresentedFramebuffer) instead of reading the
    /// emulator's live framebuffer, which the emulation thread overwrites
    /// concurrently and causes mid-frame tearing.
    void setFrameSource(FrameCopyFn frameSource)
    {
        _frameSource = std::move(frameSource);
    }

public:
    /// Native display size. Fixed: the widget keeps the standard 352x288 (11:9) framing
    /// whatever the framebuffer or viewport is, so overscan options never resize or
    /// re-proportion the window - the (cropped) framebuffer is scaled into this frame.
    static constexpr int kNativeWidth = 352;
    static constexpr int kNativeHeight = 288;

    QSize sizeHint() const override { return QSize(kNativeWidth, kNativeHeight); }

public slots:
    void refresh();
    void handleExternalKeyPress(QKeyEvent* event);
    void handleExternalKeyRelease(QKeyEvent* event);
    void prepareForShutdown();  // Block refreshes during shutdown

public:
    void setEmulator(std::shared_ptr<Emulator> emulator)
    {
        _emulator = emulator;
    }

    void setDisplayViewport(const DisplayViewport& viewport)
    {
        _displayViewport = viewport;
        _hasViewport = true;

        // Flush temporal cache - old frames had different geometry
        _frameHistory.clear();

        // Aspect ratio is fixed (see sizeHint) - only the source rectangle changes
        update();  // Trigger repaint with new viewport
    }

    /// Copy of the currently presented frame (viewport-cropped), for screenshots.
    /// Null image when no framebuffer is attached.
    QImage grabFramebuffer();

    void clearDisplayViewport()
    {
        _hasViewport = false;

        // Flush temporal cache - geometry changed
        _frameHistory.clear();

        update();
    }

    // Temporal blending (gigascreen flicker smoothing)
    void setTemporalBlendingEnabled(bool enabled);
    bool temporalBlendingEnabled() const { return _temporalEnabled; }
    void setTemporalHistorySize(int frames);
    int temporalHistorySize() const { return _frameHistory.historySize(); }
    void setTemporalWeightMode(int mode);
    int temporalWeightMode() const { return _temporalWeightMode; }

    // CRT effects (SIMD-accelerated)
    void setCRTEffectsEnabled(bool enabled);
    bool crtEffectsEnabled() const { return _crtEnabled; }
    void setCRTProfile(CRTProfile profile);
    void setCRTProfile(const CRTProfileParams& params);
    CRTProfile crtProfile() const { return _crtParams.profile; }
    const CRTProfileParams& crtParams() const { return _crtParams; }

protected:
    void paintEvent(QPaintEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

    void resizeEvent(QResizeEvent* event) override;
    using QWidget::heightForWidth;  // Bring method declaration from QWidget
    int heightForWidth(int width);

private:
    Ui::DeviceScreen* ui = nullptr;

    QRectF devicePixelsRect;
    QImage* devicePixels = nullptr;
    QImage _latchedFrame;           // Owned backing store filled via _frameSource
    FrameCopyFn _frameSource;       // Tear-free frame provider (empty = legacy live-buffer path)

    static constexpr float ratio = static_cast<float>(kNativeWidth) / static_cast<float>(kNativeHeight);

    std::shared_ptr<Emulator> _emulator = nullptr;  // Reference to emulator for UUID tagging
    bool _isShuttingDown = false;  // Flag to block refreshes during shutdown

    // Viewport cropping for overscan mode
    DisplayViewport _displayViewport;
    bool _hasViewport = false;

    // Temporal blending
    FrameHistory _frameHistory;
    QImage _blendedFrame;
    bool _temporalEnabled = false;
    int _temporalWeightMode = 0;

    // CRT effects
    std::unique_ptr<CRTFilter> _crtFilter;
    QImage _crtFrame;
    bool _crtEnabled = false;
    CRTProfileParams _crtParams;
};

#endif  // DEVICESCREEN_H
