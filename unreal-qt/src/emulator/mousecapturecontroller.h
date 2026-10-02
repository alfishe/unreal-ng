#pragma once

#include <QKeySequence>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QSizeF>
#include <functional>
#include <string>

#include "emulator/io/mouse/mousedeltaaccumulator.h"

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

/// Host mouse capture for the emulator screen: one per screen wrapper, used by
/// both renderers (software DeviceScreen, GPU DeviceScreenGLWindow)
/// (docs/inprogress/2026-10-02-mouse-manager/design.md §3.3).
///
/// Owns everything that is a property of the host, not of the emulated machine:
/// pointer capture and release, cursor locking (native relative mode on macOS,
/// re-center warping elsewhere), DPI / upscale mapping with sub-pixel carry,
/// the host's buttons and wheel notch accumulation. What leaves this class is a
/// clean MouseEvent (whole emulated pixels, active-low mask, whole notches)
/// posted to the message center for the shown emulator, whose MouseManager
/// hands it to every mouse device of the machine.
///
/// Policy:
///   - a click on the screen captures when the machine has a mouse device and
///     the gate is open; that click is not passed to the machine;
///   - the release key (default Ctrl+Esc; Cmd+Esc on macOS) and focus loss release;
///     a plain Esc reaches the machine;
///   - the gate (toolbar button) closed: never captures, posts nothing.
///     Automation and TTD replay do not come through here and are not gated.
/// No grabMouse(): the menus stay usable while the mouse is captured.
///
/// The screen only forwards its Qt events; every handler returns true when the
/// event was consumed by capture and must not be processed further.
class MouseCaptureController : public QObject
{
    Q_OBJECT

public:
    /// What the toolbar button shows
    enum class State
    {
        NoDevice,  ///< no emulator shown, or its machine has no mouse device
        Ready,     ///< gate open, not captured: a click on the screen captures
        Captured,  ///< the host mouse drives the machine
        Gated,     ///< gate closed: never captured
    };

    /// The window the screen is drawn in, whatever its kind (QWidget or QWindow)
    struct Surface
    {
        std::function<QPoint()> centerGlobal;           ///< center of the screen area, global logical coordinates
        std::function<QSize()> logicalSize;             ///< size of the screen area, logical pixels
        std::function<qreal()> devicePixelRatio;
        std::function<void(bool hidden)> setCursorHidden;
        std::function<void()> takeFocus;
        std::function<void(bool on)> setMouseTracking; ///< QWidget only; a QWindow always gets moves
    };

    /// Machine-side settings read at capture time
    struct HostSettings
    {
        bool mouseFitted = false;  // the machine has a mouse device (MouseManager::HasMouseDevice)
        bool swapButtons = false;  // [INPUT] SwapMouse
        int scaleLog2 = 0;         // [INPUT] MouseScale: sensitivity 2^scale, -3..3
        std::string releaseKey;    // [INPUT] MouseReleaseKey (QKeySequence text); empty = Ctrl+Esc
    };
    using HostSettingsFn = std::function<HostSettings()>;
    /// The emulated-pixel size of the image drawn right now (framebuffer or cropped viewport)
    using SourceSizeFn = std::function<QSizeF()>;

    explicit MouseCaptureController(QObject* parent = nullptr);
    ~MouseCaptureController() override;

    void setSurface(Surface surface) { _surface = std::move(surface); }
    void setSourceSizeProvider(SourceSizeFn provider) { _sourceSize = std::move(provider); }
    void setHostSettingsProvider(HostSettingsFn provider) { _hostSettings = std::move(provider); }

    /// Emulator that receives the events; empty detaches (releases capture, posts nothing)
    void setTargetEmulatorId(const std::string& emulatorId);

    /// region <Gate (toolbar button)>
    void setGateOpen(bool open);
    bool isGateOpen() const { return _gateOpen; }
    /// endregion

    State state() const;
    bool isCaptured() const { return _captured; }
    void capture();
    void release();

    // Qt event forwarding - return true when consumed
    bool handleMousePress(QMouseEvent* event);
    bool handleMouseRelease(QMouseEvent* event);
    bool handleMouseMove(QMouseEvent* event);
    bool handleWheel(QWheelEvent* event);
    bool handleKeyPress(QKeyEvent* event);
    bool handleKeyRelease(QKeyEvent* event);
    void handleFocusOut();

    /// Host pointer travel in logical (device-independent) pixels, screen axes.
    /// Called by the capture backend: Qt warp deltas or native macOS deltas.
    void applyHostMotion(double dxLogical, double dyLogical);

    /// The release key of the shown machine's config
    QKeySequence releaseKey() const;

signals:
    /// Captured, gate or target changed: the toolbar button refreshes from state()
    void stateChanged();

private:
    bool isReleaseKey(const QKeyEvent* event) const;
    void postButtons();
    void recenterCursor();
    uint8_t buttonBit(Qt::MouseButton button) const;
    HostSettings settings() const { return _hostSettings ? _hostSettings() : HostSettings{}; }

    Surface _surface;
    SourceSizeFn _sourceSize;
    HostSettingsFn _hostSettings;
    std::string _targetId;
    bool _gateOpen = true;
    double _scale = 1.0;         // 2^MouseScale, latched at capture
    bool _swapButtons = false;   // SwapMouse, latched at capture
    QKeySequence _releaseKey;    // latched at capture

    bool _captured = false;
    uint8_t _buttonMask = 0xFF;  // Active-low: D0 = Left, D1 = Right, D2 = Middle
    MouseDeltaAccumulator _motion;
    MouseWheelAccumulator _wheel;

    // The release key press is consumed together with its auto-repeats and its
    // release, whichever path delivers it (the screen's own key handler or the
    // main window's key forwarding) - otherwise Esc also reaches the machine
    int _swallowKey = 0;

    // Generic backend (non-macOS): re-center warping
    QPoint _warpCenterGlobal;
    bool _ignoreNextMove = false;

    // macOS backend: native relative mode (see platform/macos/mousecapture_macos.mm)
    void* _nativeCapture = nullptr;
};
