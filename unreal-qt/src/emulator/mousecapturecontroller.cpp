#include "emulator/mousecapturecontroller.h"

#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/io/mouse/mouse.h"
#include "platform/macos/mousecapture_macos.h"

#ifndef Q_OS_MACOS
void* MouseCaptureMacOS::Begin(double, double, MotionFn)
{
    return nullptr;
}

void MouseCaptureMacOS::End(void*) {}
#endif

namespace
{
const char* const kDefaultReleaseKey = "Ctrl+Esc";
}

MouseCaptureController::MouseCaptureController(QObject* parent) : QObject(parent)
{
    // Leaving the application (Cmd+Tab, another window) releases the mouse
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive)
            release();
    });
}

MouseCaptureController::~MouseCaptureController()
{
    release();
    if (_filterInstalled)
        qApp->removeEventFilter(this);
}

void MouseCaptureController::setTargetEmulatorId(const std::string& emulatorId)
{
    if (emulatorId == _targetId)
        return;

    // Never carry a capture (or held buttons) across emulator instances
    release();
    _targetId = emulatorId;
    emit stateChanged();
}

/// region <Gate and state>

void MouseCaptureController::setGateOpen(bool open)
{
    if (open == _gateOpen)
        return;
    if (!open)
        release();
    _gateOpen = open;
    emit stateChanged();
}

MouseCaptureController::State MouseCaptureController::state() const
{
    if (_captured)
        return State::Captured;
    if (_targetId.empty() || !settings().mouseFitted)
        return State::NoDevice;
    return _gateOpen ? State::Ready : State::Gated;
}

QKeySequence MouseCaptureController::releaseKey() const
{
    const std::string text = settings().releaseKey;
    QKeySequence sequence = QKeySequence::fromString(QString::fromStdString(text.empty() ? kDefaultReleaseKey : text),
                                                     QKeySequence::PortableText);
    // One key combination with a key Qt knows, else the default
    if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown || sequence[0].key() == 0)
        sequence = QKeySequence::fromString(kDefaultReleaseKey, QKeySequence::PortableText);
#ifdef Q_OS_MACOS
    // The config names physical keys: "Ctrl" is the Control key. Qt on macOS reports
    // Control as Meta and Command as Ctrl, so swap them in the parsed combination
    const QKeyCombination combination = sequence[0];
    Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const bool control = modifiers.testFlag(Qt::ControlModifier);
    const bool meta = modifiers.testFlag(Qt::MetaModifier);
    modifiers.setFlag(Qt::ControlModifier, meta);
    modifiers.setFlag(Qt::MetaModifier, control);
    sequence = QKeySequence(QKeyCombination(modifiers, combination.key()));
#endif
    return sequence;
}

bool MouseCaptureController::eventFilter(QObject* watched, QEvent* event)
{
    // The release key reaches whichever window or widget Qt delivers it to (the
    // GL window, the main window's key forwarding, a dock): release on the first
    // sight of it, before shortcuts or the machine see it, and swallow the rest
    const QEvent::Type type = event->type();
    if (type == QEvent::ShortcutOverride || type == QEvent::KeyPress || type == QEvent::KeyRelease)
    {
        auto* key = static_cast<QKeyEvent*>(event);
        if (type != QEvent::KeyRelease && _captured && isReleaseKey(key))
        {
            _swallowKey = key->key();
            release();
            event->accept();
            return true;
        }
        if (_swallowKey != 0 && key->key() == _swallowKey)
        {
            if (type == QEvent::KeyRelease && !key->isAutoRepeat())
                _swallowKey = 0;
            event->accept();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

/// endregion </Gate and state>

/// region <Capture>

void MouseCaptureController::capture()
{
    if (_captured || !_gateOpen || _targetId.empty() || !_surface.centerGlobal)
        return;

    const HostSettings host = settings();
    if (!host.mouseFitted)
    {
        // No mouse device on this machine: grabbing the pointer would only take it away from the user
        return;
    }
    _swapButtons = host.swapButtons;
    _scale = std::ldexp(1.0, std::clamp(host.scaleLog2, -3, 3));
    _releaseKey = releaseKey();

    _captured = true;
    _motion.Reset();
    _wheel.Reset();
    _buttonMask = 0xFF;
    _swallowKey = 0;

    if (!_filterInstalled)
    {
        qApp->installEventFilter(this);
        _filterInstalled = true;
    }

    if (_surface.takeFocus)
        _surface.takeFocus();
    if (_surface.setCursorHidden)
        _surface.setCursorHidden(true);
    // Without tracking a QWidget gets move events only while a button is held
    if (_surface.setMouseTracking)
        _surface.setMouseTracking(true);

    const QPoint centerGlobal = _surface.centerGlobal();
    _nativeCapture = _surface.allowNativeCapture
                         ? MouseCaptureMacOS::Begin(centerGlobal.x(), centerGlobal.y(),
                                                    [this](double dx, double dy) { applyHostMotion(dx, dy); })
                         : nullptr;
    if (!_nativeCapture)
    {
        _warpCenterGlobal = centerGlobal;
        recenterCursor();
    }

    qDebug() << "MouseCaptureController: capture ON  (target" << QString::fromStdString(_targetId) << ", backend"
             << (_nativeCapture ? "macOS native" : "Qt warp") << ", release" << _releaseKey.toString() << ")";
    emit stateChanged();
}

void MouseCaptureController::release()
{
    if (!_captured)
        return;

    _captured = false;

    if (_nativeCapture)
    {
        MouseCaptureMacOS::End(_nativeCapture);
        _nativeCapture = nullptr;
    }
    _ignoreNextMove = false;

    if (_surface.setMouseTracking)
        _surface.setMouseTracking(false);
    if (_surface.setCursorHidden)
        _surface.setCursorHidden(false);

    // A button held at release time must not stay pressed inside the guest
    if (_buttonMask != 0xFF)
    {
        _buttonMask = 0xFF;
        postButtons();
    }

    _motion.Reset();
    _wheel.Reset();

    qDebug() << "MouseCaptureController: capture OFF";
    emit stateChanged();
}

void MouseCaptureController::recenterCursor()
{
    if (_surface.warpCursor)
        _surface.warpCursor(_warpCenterGlobal);
    else
        QCursor::setPos(_warpCenterGlobal);
    // setPos generates a move event back to the center - it is not user travel
    _ignoreNextMove = true;
}

/// endregion </Capture>

/// region <Qt event forwarding>

bool MouseCaptureController::handleMousePress(QMouseEvent* event)
{
    if (!_captured)
    {
        // The capturing click itself is not forwarded to the guest
        capture();
        return _captured;
    }

    const uint8_t previous = _buttonMask;
    _buttonMask &= static_cast<uint8_t>(~buttonBit(event->button()));

    if (_buttonMask != previous)
        postButtons();
    return true;
}

/// Active-low bit for a host button: D0 = Left, D1 = Right, D2 = Middle (left/right swapped by SwapMouse)
uint8_t MouseCaptureController::buttonBit(Qt::MouseButton button) const
{
    switch (button)
    {
        case Qt::LeftButton:
            return _swapButtons ? 0x02 : 0x01;
        case Qt::RightButton:
            return _swapButtons ? 0x01 : 0x02;
        case Qt::MiddleButton:
            return 0x04;
        default:
            return 0x00;
    }
}

bool MouseCaptureController::handleMouseRelease(QMouseEvent* event)
{
    if (!_captured)
        return false;

    const uint8_t previous = _buttonMask;
    _buttonMask |= buttonBit(event->button());

    if (_buttonMask != previous)
        postButtons();
    return true;
}

bool MouseCaptureController::handleMouseMove(QMouseEvent* event)
{
    if (!_captured)
        return false;

    // Native capture reads deltas from NSEvent; the frozen cursor's Qt events carry no travel
    if (_nativeCapture)
        return true;

    const QPoint position = event->globalPosition().toPoint();
    if (_ignoreNextMove || position == _warpCenterGlobal)
    {
        _ignoreNextMove = false;
        return true;
    }

    applyHostMotion(position.x() - _warpCenterGlobal.x(), position.y() - _warpCenterGlobal.y());
    recenterCursor();
    return true;
}

bool MouseCaptureController::handleWheel(QWheelEvent* event)
{
    if (!_captured)
        return false;

    const int notches = _wheel.Feed(event->angleDelta().y());
    if (notches != 0 && !_targetId.empty())
        MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_WHEEL, MouseEvent::Wheel(notches, _targetId));
    return true;
}

bool MouseCaptureController::isReleaseKey(const QKeyEvent* event) const
{
    if (_releaseKey.isEmpty())
        return false;
    // The key and exactly its modifiers (keypad flag aside)
    const QKeyCombination wanted = _releaseKey[0];
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    return event->key() == wanted.key() && modifiers == wanted.keyboardModifiers();
}

bool MouseCaptureController::handleKeyPress(QKeyEvent* event)
{
    if (_captured && isReleaseKey(event))
    {
        _swallowKey = event->key();
        release();
        return true;
    }

    // Auto-repeat, or the same press delivered a second time through the window's key forwarding
    return _swallowKey != 0 && event->key() == _swallowKey;
}

bool MouseCaptureController::handleKeyRelease(QKeyEvent* event)
{
    if (_swallowKey == 0 || event->key() != _swallowKey)
        return false;

    if (!event->isAutoRepeat())
        _swallowKey = 0;
    return true;
}

void MouseCaptureController::handleFocusOut()
{
    release();
}

/// endregion </Qt event forwarding>

void MouseCaptureController::applyHostMotion(double dxLogical, double dyLogical)
{
    if (!_captured || _targetId.empty() || !_surface.logicalSize)
        return;

    const QSizeF source = _sourceSize ? _sourceSize() : QSizeF();
    const QSize size = _surface.logicalSize();
    if (source.width() <= 0.0 || source.height() <= 0.0 || size.isEmpty())
        return;

    // Work in physical pixels on both sides (Kempston design §5.1): host travel and the drawn image size
    const double dpr = _surface.devicePixelRatio ? _surface.devicePixelRatio() : 1.0;
    const double physPerEmuX = size.width() * dpr / source.width();
    const double physPerEmuY = size.height() * dpr / source.height();

    const MouseDeltaAccumulator::Steps steps = _motion.Feed(dxLogical * dpr, dyLogical * dpr, physPerEmuX, physPerEmuY, _scale);
    if (steps.dx == 0 && steps.dy == 0)
        return;

    // Screen Y grows downward, the mouse's Y grows upward
    MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_MOVE, MouseEvent::Move(steps.dx, -steps.dy, _targetId));
}

void MouseCaptureController::postButtons()
{
    if (!_targetId.empty())
        MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_BUTTON, MouseEvent::Buttons(_buttonMask, _targetId));
}
