#pragma once

#include <QString>

class QScreen;

/// Refresh characteristics of the display a window is shown on
struct DisplayRefreshInfo
{
    double currentHz = 0.0;  // Current nominal refresh rate
    double maxHz = 0.0;      // Highest rate the display can present (top of the VRR range, or max mode)
    double minHz = 0.0;      // Bottom of the VRR range when known (0 otherwise)
    bool variable = false;   // Variable refresh rate (ProMotion, G-Sync/FreeSync) range known
    QString source;          // Which API produced the numbers (diagnostics)

    /// Rate to cap rendered frames at: the top of the VRR range, else the max mode, else current
    double renderCapHz() const { return maxHz > 0.0 ? maxHz : currentHz; }
};

/// @brief Cross-platform display refresh rate query.
///
/// Baseline is Qt's QScreen::refreshRate() everywhere. On macOS the NSScreen refresh
/// interval API (macOS 12+) adds the VRR range of ProMotion panels; on Windows the
/// display mode list adds the highest mode rate. Linux (X11 / Wayland) reports the
/// current rate only - VRR ranges are not exposed through a portable API there.
class DisplayRefreshRate
{
public:
    static DisplayRefreshInfo query(const QScreen* screen);

private:
    static void queryPlatform(const QScreen* screen, DisplayRefreshInfo& info);
};
