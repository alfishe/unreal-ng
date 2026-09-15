#pragma once

#ifndef MOUSEMANAGER_H
#define MOUSEMANAGER_H

#include <QObject>
#include <QPoint>
#include <QSizeF>
#include <functional>
#include <string>

#include "emulator/io/mouse/mousedeltaaccumulator.h"

class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QWidget;

/// Host-side Kempston Mouse input for one emulator screen widget.
///
/// Owns everything that is a property of the host, not of the emulated machine:
/// pointer capture and release, cursor locking (native relative mode on macOS,
/// re-centre warping elsewhere), DPI / upscale mapping with sub-pixel carry,
/// the button mask and wheel notch accumulation. What leaves this class is a
/// clean MouseEvent (whole emulated pixels, active-low mask, whole notches)
/// posted to MessageCenter and tagged with the target emulator's id.
///
/// The screen widget only forwards its Qt events; every handler returns true
/// when the event was consumed by mouse capture and must not be processed
/// further (e.g. the release key must not also reach the ZX keyboard).
class MouseManager : public QObject
{
    Q_OBJECT

public:
    /// Supplies the emulated-pixel size of the image currently drawn in the
    /// surface (framebuffer, or the cropped overscan viewport)
    using SourceSizeFn = std::function<QSizeF()>;

    /// Machine-side settings read at capture time ([INPUT] config + kempstonmouse feature)
    struct HostSettings
    {
        bool mouseFitted = true;  // a Kempston Mouse is on the bus - otherwise clicks are not captured
        bool swapButtons = false; // [INPUT] SwapMouse
        int scaleLog2 = 0;        // [INPUT] MouseScale: sensitivity 2^scale, -3..3
    };
    using HostSettingsFn = std::function<HostSettings()>;

    MouseManager(QWidget* surface, SourceSizeFn sourceSize, QObject* parent = nullptr);

    /// Provider of the target machine's mouse settings (unset: fitted, no swap, scale 0)
    void setHostSettingsProvider(HostSettingsFn provider) { _hostSettings = std::move(provider); }
    ~MouseManager() override;

    /// Emulator that receives the events; empty detaches (releases capture, posts nothing)
    void setTargetEmulatorId(const std::string& emulatorId);


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
    void handleFocusOut(QFocusEvent* event);

    /// Host pointer travel in logical (device-independent) pixels, screen axes.
    /// Called by the capture backend: Qt warp deltas or native macOS deltas.
    void applyHostMotion(double dxLogical, double dyLogical);

    /// Key that releases the capture
    static constexpr int kReleaseKey = Qt::Key_Escape;

signals:
    void captureChanged(bool captured);

private:
    void postButtons();
    void recentreCursor();
    uint8_t buttonBit(Qt::MouseButton button) const;

    QWidget* _surface = nullptr;
    SourceSizeFn _sourceSize;
    HostSettingsFn _hostSettings;
    std::string _targetId;
    double _scale = 1.0;         // 2^MouseScale, latched at capture
    bool _swapButtons = false;   // SwapMouse, latched at capture

    bool _captured = false;
    uint8_t _buttonMask = 0xFF;  // Active-low: D0 = Left, D1 = Right, D2 = Middle
    MouseDeltaAccumulator _motion;
    MouseWheelAccumulator _wheel;

    // The release key press is consumed together with its auto-repeats and its
    // release, and whichever path delivers it (the widget's own key handler or
    // the main window's key forwarding) - otherwise ESC also taps ZX BREAK
    bool _swallowReleaseKey = false;

    // Generic backend (non-macOS): re-centre warping
    QPoint _warpCentreGlobal;
    bool _ignoreNextMove = false;

    // macOS backend: native relative mode (see platform/macos/mousecapture_macos.mm)
    void* _nativeCapture = nullptr;
};

#endif  // MOUSEMANAGER_H
