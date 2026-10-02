#pragma once

#include <QKeySequence>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QSizeF>
#include <QElapsedTimer>
#include <QTimer>
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
///   - a click on the screen captures when a program reads the machine's mouse and
///     the gate is open; that click is not passed to the machine;
///   - the release key (default Ctrl+Esc: the physical Control key on every
///     platform, macOS included) and focus loss release; a plain Esc reaches the
///     machine. While captured an application-wide event filter catches the
///     release key in any window, whatever has the focus or a shortcut claims;
///   - a click captures only while a program reads the mouse (the 128K ROM does not;
///     TR-DOS owns the ports): the machine's "mouse in use" answer decides;
///   - captured, the mouse stops being in use: released once it has stayed unused
///     for kUnreachableReleaseMs, so a pause in polling does not flap the capture.
///     Worked example: unused at 10.0 s, polled again at 11.5 s: kept; unused at
///     10.0 s and still at 13.0 s: released. No re-capture by itself
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
        NoDevice,  ///< no emulator shown, or no program reads its mouse (no device, TR-DOS, a ROM that ignores it)
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
        /// Tests: no native relative mode, and the cursor warp replaced (the real cursor stays put)
        bool allowNativeCapture = true;
        std::function<void(QPoint global)> warpCursor;  ///< empty = QCursor::setPos
    };

    /// Machine-side settings read at capture time
    struct HostSettings
    {
        bool mouseFitted = false;  // a program is reading the machine's mouse now (MouseManager::IsMouseInUse)
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

    /// region <Speed>
    /// On (default): the captured mouse moves the guest exactly as far as the host
    /// pointer would move over the picture (host travel / size of one emulated
    /// pixel on screen, whatever the window size, zoom or DPI; the host's own
    /// pointer speed and acceleration are in the travel). Off: one host pixel of
    /// travel is one mouse count, independent of the window. Either way times
    /// 2^[INPUT] MouseScale. Switchable at any time
    void setMatchHostPointer(bool match) { _matchHostPointer = match; _motion.Reset(); }
    bool matchesHostPointer() const { return _matchHostPointer; }
    /// endregion

    /// Where motion, buttons and wheel go (default: the message center, for the
    /// target emulator's MouseManager). Tests replace it
    struct Poster
    {
        std::function<void(int dx, int dy)> move;  ///< emulated pixels, +y up
        std::function<void(uint8_t activeLowMask)> buttons;
        std::function<void(int steps)> wheel;
    };
    void setPoster(Poster poster) { _poster = std::move(poster); }

    /// Unreachable this long (ms) while captured releases the capture
    static constexpr qint64 kUnreachableReleaseMs = 3000;
    /// Milliseconds clock for that rule (default: a monotonic timer). Tests replace it
    using ClockFn = std::function<qint64()>;
    void setClock(ClockFn clock) { _clock = std::move(clock); }
    /// While captured: release when the machine's mouse has been unreachable long enough.
    /// Runs from a timer; callable directly (tests)
    void checkReachable();

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

    /// The release key of the shown machine's config, as Qt sees the physical keys
    /// (on macOS "Ctrl" in the config is the Control key, Qt's Meta modifier)
    QKeySequence releaseKey() const;
    /// The release key for people to read, naming the physical keys ("Ctrl+Esc" on every
    /// platform: not Qt's native macOS symbols, where the Control key is a hard-to-see glyph)
    QString releaseKeyText() const;

    /// While captured: the release key anywhere in the application releases
    bool eventFilter(QObject* watched, QEvent* event) override;

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
    bool _matchHostPointer = true;
    Poster _poster;
    double _scale = 1.0;         // 2^MouseScale, latched at capture
    bool _swapButtons = false;   // SwapMouse, latched at capture
    QKeySequence _releaseKey;    // latched at capture

    qint64 now() const { return _clock ? _clock() : _elapsed.elapsed(); }

    QTimer _reachTimer;                 // runs only while captured
    QElapsedTimer _elapsed;
    ClockFn _clock;
    qint64 _unreachableSince = -1;      // clock value when the mouse became unreachable; -1 = reachable

    bool _captured = false;
    bool _filterInstalled = false;
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
