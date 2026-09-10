#include "displayrefreshrate.h"

#include <QScreen>
#include <algorithm>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

DisplayRefreshInfo DisplayRefreshRate::query(const QScreen* screen)
{
    DisplayRefreshInfo info;
    if (!screen)
        return info;

    info.currentHz = screen->refreshRate();
    info.maxHz = info.currentHz;
    info.source = QStringLiteral("QScreen");

    queryPlatform(screen, info);
    return info;
}

#if defined(Q_OS_WIN)
void DisplayRefreshRate::queryPlatform(const QScreen* screen, DisplayRefreshInfo& info)
{
    // QScreen::name() is the GDI device name ("\\\\.\\DISPLAY1") on the windows platform plugin
    const std::wstring device = screen->name().toStdWString();
    const wchar_t* deviceName = device.empty() ? nullptr : device.c_str();

    DEVMODEW current{};
    current.dmSize = sizeof(current);
    if (!EnumDisplaySettingsW(deviceName, ENUM_CURRENT_SETTINGS, &current))
        return;

    if (current.dmDisplayFrequency > 1)
        info.currentHz = current.dmDisplayFrequency;

    // Highest refresh rate offered at the current resolution; VRR ranges are not public API
    DWORD maxHz = current.dmDisplayFrequency;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    for (DWORD i = 0; EnumDisplaySettingsW(deviceName, i, &mode); ++i)
    {
        if (mode.dmPelsWidth == current.dmPelsWidth && mode.dmPelsHeight == current.dmPelsHeight)
            maxHz = std::max(maxHz, mode.dmDisplayFrequency);
    }
    if (maxHz > 1)
        info.maxHz = maxHz;
    info.source = QStringLiteral("EnumDisplaySettings");
}
#elif !defined(Q_OS_MACOS)
void DisplayRefreshRate::queryPlatform(const QScreen* screen, DisplayRefreshInfo& info)
{
    Q_UNUSED(screen);
    Q_UNUSED(info);  // X11 / Wayland: Qt's current rate is all that is portably available
}
#endif
