#include "emulator/mousemanager.h"

#include <QCursor>
#include <QDebug>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWidget>
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

MouseManager::MouseManager(QWidget* surface, SourceSizeFn sourceSize, QObject* parent)
    : QObject(parent), _surface(surface), _sourceSize(std::move(sourceSize))
{
}

MouseManager::~MouseManager()
{
    release();
}

void MouseManager::setTargetEmulatorId(const std::string& emulatorId)
{
    if (emulatorId == _targetId)
        return;

    // Never carry a capture (or held buttons) across emulator instances
    release();
    _targetId = emulatorId;
}

/// region <Capture>

void MouseManager::capture()
{
    if (_captured || !_surface || _targetId.empty())
        return;

    const HostSettings settings = _hostSettings ? _hostSettings() : HostSettings{};
    if (!settings.mouseFitted)
    {
        // No Kempston Mouse on this machine ([INPUT] Mouse=NONE or the kempstonmouse feature off):
        // grabbing the pointer would only take it away from the user
        return;
    }
    _swapButtons = settings.swapButtons;
    _scale = std::ldexp(1.0, std::clamp(settings.scaleLog2, -3, 3));

    _captured = true;
    _motion.Reset();
    _wheel.Reset();
    _buttonMask = 0xFF;
    _swallowReleaseKey = false;

    _surface->setFocus(Qt::MouseFocusReason);
    _surface->grabMouse();
    _surface->setCursor(Qt::BlankCursor);

    // Without tracking Qt delivers move events only while a button is held
    _surface->setMouseTracking(true);

    const QPoint centreGlobal = _surface->mapToGlobal(_surface->rect().center());

    _nativeCapture = MouseCaptureMacOS::Begin(centreGlobal.x(), centreGlobal.y(),
                                              [this](double dx, double dy) { applyHostMotion(dx, dy); });
    if (!_nativeCapture)
    {
        _warpCentreGlobal = centreGlobal;
        recentreCursor();
    }

    qDebug() << "MouseManager: capture ON  (target" << QString::fromStdString(_targetId) << ", backend"
             << (_nativeCapture ? "macOS native" : "Qt warp") << ")";
    emit captureChanged(true);
}

void MouseManager::release()
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

    if (_surface)
    {
        _surface->setMouseTracking(false);
        _surface->releaseMouse();
        _surface->unsetCursor();
    }

    // A button held at release time must not stay pressed inside the guest
    if (_buttonMask != 0xFF)
    {
        _buttonMask = 0xFF;
        postButtons();
    }

    _motion.Reset();
    _wheel.Reset();

    qDebug() << "MouseManager: capture OFF";
    emit captureChanged(false);
}

void MouseManager::recentreCursor()
{
    QCursor::setPos(_warpCentreGlobal);
    // setPos generates a move event back to the centre - it is not user travel
    _ignoreNextMove = true;
}

/// endregion </Capture>

/// region <Qt event forwarding>

bool MouseManager::handleMousePress(QMouseEvent* event)
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
uint8_t MouseManager::buttonBit(Qt::MouseButton button) const
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

bool MouseManager::handleMouseRelease(QMouseEvent* event)
{
    if (!_captured)
        return false;

    const uint8_t previous = _buttonMask;
    _buttonMask |= buttonBit(event->button());

    if (_buttonMask != previous)
        postButtons();
    return true;
}

bool MouseManager::handleMouseMove(QMouseEvent* event)
{
    if (!_captured)
        return false;

    // Native capture reads deltas from NSEvent; the frozen cursor's Qt events carry no travel
    if (_nativeCapture)
        return true;

    const QPoint position = event->globalPosition().toPoint();
    if (_ignoreNextMove || position == _warpCentreGlobal)
    {
        _ignoreNextMove = false;
        return true;
    }

    applyHostMotion(position.x() - _warpCentreGlobal.x(), position.y() - _warpCentreGlobal.y());
    recentreCursor();
    return true;
}

bool MouseManager::handleWheel(QWheelEvent* event)
{
    if (!_captured)
        return false;

    const int notches = _wheel.Feed(event->angleDelta().y());
    if (notches != 0 && !_targetId.empty())
        MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_WHEEL, MouseEvent::Wheel(notches, _targetId));
    return true;
}

bool MouseManager::handleKeyPress(QKeyEvent* event)
{
    if (event->key() != kReleaseKey)
        return false;

    if (_captured)
    {
        _swallowReleaseKey = true;
        release();
        return true;
    }

    // Auto-repeat, or the same press delivered a second time through the window's key forwarding
    return _swallowReleaseKey;
}

bool MouseManager::handleKeyRelease(QKeyEvent* event)
{
    if (event->key() != kReleaseKey || !_swallowReleaseKey)
        return false;

    if (!event->isAutoRepeat())
        _swallowReleaseKey = false;
    return true;
}

void MouseManager::handleFocusOut(QFocusEvent*)
{
    release();
}

/// endregion </Qt event forwarding>

void MouseManager::applyHostMotion(double dxLogical, double dyLogical)
{
    if (!_captured || !_surface || _targetId.empty())
        return;

    const QSizeF source = _sourceSize ? _sourceSize() : QSizeF();
    if (source.width() <= 0.0 || source.height() <= 0.0)
        return;

    // Work in physical pixels on both sides (design §5.1): host travel and the drawn image size
    const double dpr = _surface->devicePixelRatioF();
    const double physPerEmuX = _surface->width() * dpr / source.width();
    const double physPerEmuY = _surface->height() * dpr / source.height();

    const MouseDeltaAccumulator::Steps steps = _motion.Feed(dxLogical * dpr, dyLogical * dpr, physPerEmuX, physPerEmuY, _scale);
    if (steps.dx == 0 && steps.dy == 0)
        return;

    // Screen Y grows downward, the Kempston Y counter grows upward
    MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_MOVE, MouseEvent::Move(steps.dx, -steps.dy, _targetId));
}

void MouseManager::postButtons()
{
    if (!_targetId.empty())
        MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_BUTTON, MouseEvent::Buttons(_buttonMask, _targetId));
}
