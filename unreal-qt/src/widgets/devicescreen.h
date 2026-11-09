#ifndef DEVICESCREEN_H
#define DEVICESCREEN_H

#include <QImage>
#include <QWidget>
#include <functional>
#include <memory>
#include <mutex>
#include <cstdint>

#include "emulator/video/pictureview.h"
#include "emulator/video/screen.h"  // For DisplayViewport
#include "framehistory.h"
#include "framesource.h"
#include "crtprofiles.h"

class CRTBeamModel;
class CRTRenderer;

class Emulator;  // Forward declaration
class MouseCaptureController;

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
    /// `aspect`: width : height the picture is drawn in; 0 = the CRT frame's 352:288 (every machine raster).
    /// A video card's own picture (the VDAC2's FT812) has square pixels and gives its own
    void init(uint16_t width, uint16_t height, void* buffer, float aspect = 0.0f);
    void detach();

    /// Largest ratio-conforming geometry inside the parent widget, centered.
    /// Grow-capable replacement for the shrink-only conformToAspectRatio when
    /// the framebuffer geometry changes (model/video-mode switch).
    void fitToParent();

    /// When set, paintEvent pulls the frame due now (Screen::CopyFrameAt) into an owned backing image via this
    /// callback instead of reading the emulator's live framebuffer, which the emulation thread overwrites
    /// concurrently and causes mid-frame tearing. A frame due later gets its own repaint
    /// Smooth motion (ScreenFrames::smooth, UNREAL_SMOOTH_MOTION overrides it) with the frames in order: a repaint every
    /// refresh while frames come, each with the picture for its time (smoothmotion.h, the CPU reference)
    void setFrameSource(ScreenFrames source);
    void setFrameSource(FrameSourceFn frameSource) { setFrameSource(ScreenFrames{std::move(frameSource)}); }
    /// The game's picture cadence from its profile (nullptr: none): with Mode::Interpolate, the steps between the
    /// game's own pictures
    void setGameVideo(const smoothmotion::GameVideo* game) { _smooth.SetGame(game); }
    /// Smooth motion's counters for the automation (presenter "software", the CPU reference)
    WindowPresentStats presentStats() const;

    /// Thread-safe cut of every paint path into the emulator's framebuffer
    /// (the tear-free provider captures raw Screen*/EmulatorContext* pointers,
    /// the legacy wrapper borrows the live buffer). Safe to call from the
    /// MessageCenter worker; paints after it simply find no source.
    void clearFrameSource();

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
    /// Also retargets Kempston Mouse input (capture is released on change)
    void setEmulator(std::shared_ptr<Emulator> emulator);

    void setDisplayViewport(const DisplayViewport& viewport)
    {
        _displayViewport = viewport;
        _hasViewport = true;

        // Flush temporal cache - old frames had different geometry
        _frameHistory.clear();

        // Aspect ratio is fixed (see sizeHint) - only the source rectangle changes
        update();  // Trigger repaint with new viewport
    }

    /// The part of the frame shown (the window setting `view`); the picture's shape follows the cut and the widget
    /// re-fits itself to it. Full is the shape init gave
    void setPictureView(PictureView::Mode mode);
    PictureView::Mode pictureView() const { return _pictureView; }

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
    /// Exponential weights: each older frame's share of the newer one (0.1..0.9)
    void setTemporalDecay(float decay);
    float temporalDecay() const { return _temporalDecay; }

    // CRT effects (CRTRenderer / CRTBeamModel: the same picture as the GPU screen, bit for bit)
    void setCRTEffectsEnabled(bool enabled);
    bool crtEffectsEnabled() const { return _crtEnabled; }
    void setCRTProfile(CRTProfile profile);
    /// New values of the profile's parameters, the CRT switch kept
    void setCRTParams(const CRTProfileParams& params);
    void setCRTProfile(const CRTProfileParams& params);
    CRTProfile crtProfile() const { return _crtParams.profile; }
    const CRTProfileParams& crtParams() const { return _crtParams; }

    /// Host mouse capture (owned by the screen wrapper): mouse, wheel, focus and
    /// the release key go there first
    void setMouseCapture(MouseCaptureController* capture) { _mouseCapture = capture; }

    /// Framebuffer area currently drawn into the widget (viewport-cropped), in emulated pixels
    QRectF displaySourceRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;

    bool event(QEvent* event) override;
    /// Tab / Shift+Tab are machine keys here (Flex Navigator switches panels with Tab), not Qt's focus
    /// traversal: QWidget::event offers them to focusNextPrevChild first and calls keyPressEvent only when
    /// that refuses. Refusing keeps the key for the machine and the focus on the screen
    bool focusNextPrevChild(bool) override { return false; }
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

    void resizeEvent(QResizeEvent* event) override;
    void conformToAspectRatio();  // Shrink current size to the aspect ratio (safety net for external resizes)
    using QWidget::heightForWidth;  // Bring method declaration from QWidget
    int heightForWidth(int width);

private:
    Ui::DeviceScreen* ui = nullptr;

    QRectF devicePixelsRect;
    QImage* devicePixels = nullptr;
    QImage _latchedFrame;           // Owned backing store filled via _frameSource
    FrameSourceFn _frameSource;     // Tear-free frame provider (empty = legacy live-buffer path)
    NextFrameSourceFn _nextFrameSource;  // The frames in order, for smooth motion
    bool _repaintScheduled = false; // a repaint waits for the next frame's target
    mutable std::mutex _frameSourceMutex;  // Guards _frameSource, _nextFrameSource + devicePixels across threads

    // Smooth motion (GUI thread)
    bool smoothActive() const;
    smoothmotion::SmoothMotion _smooth;
    DisplayRate _displayRate = DisplayRate::Keep;  // ScreenFrames::displayRate: reported, not done (no presenter)
    // Smooth motion auto (View > Video Synchronization): no VRR and no display switch here, so interpolate unless the
    // display's own rate fits the machine's
    void updateVideoSync();
    bool _smoothAuto = false;
    VideoSync _sync = VideoSync::Off;
    std::string _syncReason;
    FrameRateMeter _frameRate;   // the machine's rate from the shown frames' targets
    FrameClockSource _shownClock = FrameClockSource::None;  // the clock that timed the frame drawn last
    uint64_t _meteredSerial = 0; // the last frame pushed into it
    QImage _nextFrame;    // a frame on its way into _smooth
    QImage _smoothFrame;  // the picture drawn
    smoothmotion::SmoothMotion::Result _smoothShown;  // what _smoothFrame is made of

    static constexpr float kNativeRatio = static_cast<float>(kNativeWidth) / static_cast<float>(kNativeHeight);
    float ratio = kNativeRatio;  // the shape the picture is drawn in (init)

    std::shared_ptr<Emulator> _emulator = nullptr;  // Reference to emulator for UUID tagging
    bool _isShuttingDown = false;  // Flag to block refreshes during shutdown

    float _baseRatio = kNativeRatio;  // the shape init gave: what view "full" draws in
    PictureView::Mode _pictureView = PictureView::Mode::Full;
    PictureGeometry _frameGeometry;   // the geometry latched with the frame last drawn (GUI thread)
    void applyPictureViewRatio();     // ratio from the cut; re-fits the widget when it changed

    // Viewport cropping for overscan mode
    DisplayViewport _displayViewport;
    bool _hasViewport = false;

    // Temporal blending
    FrameHistory _frameHistory;
    QImage _blendedFrame;
    bool _temporalEnabled = false;
    int _temporalWeightMode = 0;
    float _temporalDecay = 0.5f;

    // CRT effects
    std::unique_ptr<CRTRenderer> _crtRenderer;
    std::unique_ptr<CRTBeamModel> _crtModel;  ///< the shader models (crt-lottes, ...)
    QImage _crtFrame;
    bool _crtEnabled = false;
    CRTProfileParams _crtParams;

    MouseCaptureController* _mouseCapture = nullptr;  // host mouse capture (owned by DeviceScreenWrapper)
};

#endif  // DEVICESCREEN_H
