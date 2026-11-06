/// @file mainwindow_display.cpp
/// @brief The main window following the host's displays and GPU without a restart, as the sound follows its devices:
/// a remote desktop session has no hardware OpenGL (the software screen), the console it moves to has one (the GPU
/// screen again); a monitor or adapter added or removed may change the GPU under the window. Each such change asks
/// the GPU again a moment later and rebuilds the screen when the answer differs from what runs. View > GPU
/// Acceleration stays the user's wish (View/Gpu) whatever the session can do.

#include "mainwindow.h"

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wtsapi32.h>
#endif

namespace
{
// The driver takes a moment after a session or display change: the first ask after it, then a few more while the
// GPU wanted is not there yet
constexpr int kFirstRecheckMs = 1000;
constexpr int kNextRecheckMs = 2000;
constexpr int kRechecks = 5;

#ifdef Q_OS_WIN
/// The display adapters on the desktop and which one is primary: WM_DISPLAYCHANGE also comes for a mode change (the
/// window's own display rate switch, [VIDEO] DisplayRate = match, on every focus change), which brings no other GPU
QString DisplayAdapters()
{
    QString adapters;
    DISPLAY_DEVICEW device{};
    device.cb = sizeof device;
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); i++)
    {
        if (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)
            adapters += QString::fromWCharArray(device.DeviceName) + QLatin1Char('=') +
                        QString::fromWCharArray(device.DeviceString) +
                        ((device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? QStringLiteral("*;")
                                                                             : QStringLiteral(";"));
        device = {};
        device.cb = sizeof device;
    }
    return adapters;
}

/// DisplayAdapters as the last WM_DISPLAYCHANGE left them (one main window)
QString gDisplayAdapters;
#endif
}  // namespace

void MainWindow::watchDisplayChanges()
{
    if (_gpuRecheckTimer)
        return;
    _gpuRecheckTimer = new QTimer(this);
    _gpuRecheckTimer->setSingleShot(true);
    connect(_gpuRecheckTimer, &QTimer::timeout, this, &MainWindow::recheckGpu);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen*) { scheduleGpuRecheck("a display was added"); });
    connect(qApp, &QGuiApplication::screenRemoved, this,
            [this](QScreen*) { scheduleGpuRecheck("a display was removed"); });
    connect(qApp, &QGuiApplication::primaryScreenChanged, this,
            [this](QScreen*) { scheduleGpuRecheck("the primary display changed"); });
#ifdef Q_OS_WIN
    // WM_WTSSESSION_CHANGE: the session moved between a remote desktop and the console
    HWND window = reinterpret_cast<HWND>(winId());
    if (WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION))
        _sessionNotifyWindow = window;
    gDisplayAdapters = DisplayAdapters();
#endif
}

void MainWindow::unwatchDisplayChanges()
{
#ifdef Q_OS_WIN
    if (_sessionNotifyWindow)
        WTSUnRegisterSessionNotification(static_cast<HWND>(_sessionNotifyWindow));
#endif
    _sessionNotifyWindow = nullptr;
    if (_gpuRecheckTimer)
        _gpuRecheckTimer->stop();
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg && msg->message == WM_DISPLAYCHANGE)
    {
        // A mode change (another rate or resolution) leaves the adapters as they were: no GPU to ask for (the probe
        // costs ~80 ms on the GUI thread)
        const QString adapters = DisplayAdapters();
        if (adapters != gDisplayAdapters)
        {
            gDisplayAdapters = adapters;
            scheduleGpuRecheck("the display configuration changed");
        }
    }
    else if (msg && msg->message == WM_WTSSESSION_CHANGE)
    {
        if (msg->wParam == WTS_CONSOLE_CONNECT)
            scheduleGpuRecheck("the session moved to the console");
        else if (msg->wParam == WTS_REMOTE_CONNECT)
            scheduleGpuRecheck("the session moved to a remote desktop");
        else if (msg->wParam == WTS_SESSION_UNLOCK)
            scheduleGpuRecheck("the session was unlocked");
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::scheduleGpuRecheck(const char* why)
{
    if (!_gpuRecheckTimer)
        return;
    qInfo() << "Display change:" << why << "- the GPU is asked again";
    _gpuRechecksLeft = kRechecks;
    _gpuRecheckTimer->start(kFirstRecheckMs);  // a burst of changes asks once, after the last
}

void MainWindow::recheckGpu()
{
    if (!_screenWrapper)
        return;
    const bool changed = DeviceScreenWrapper::recheckGPUAvailable();
    const bool available = DeviceScreenWrapper::isGPUAvailable();
    _menuManager->setGpuAccelerationAvailable(available);
    const bool gpu = _gpuWanted && available;
    const bool running = _screenWrapper->isGPUAccelerated();
    if (gpu != running || (gpu && changed))
    {
        qInfo() << "Display change: the screen moves to" << (gpu ? "the GPU" : "the software screen")
                << (gpu == running ? "(the GPU changed)" : "");
        rebuildScreen(gpu, true);
        schedulePublishWindowDisplay();
    }
    else if (_gpuWanted && !available && --_gpuRechecksLeft > 0)
        _gpuRecheckTimer->start(kNextRecheckMs);
    applyDisplayRefreshRate();  // the turbo cap follows the display now shown
}
