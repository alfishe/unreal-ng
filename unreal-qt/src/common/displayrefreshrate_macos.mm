#include "displayrefreshrate.h"

#include <QRect>
#include <QScreen>

#import <AppKit/AppKit.h>

/// macOS: NSScreen exposes the refresh interval range (macOS 12+), which is the
/// VRR range on ProMotion panels (e.g. 48..120 Hz) and a single value elsewhere.
void DisplayRefreshRate::queryPlatform(const QScreen* screen, DisplayRefreshInfo& info)
{
    NSScreen* target = nil;
    const QString wanted = screen->name();
    for (NSScreen* candidate in [NSScreen screens])
    {
        if (@available(macOS 10.15, *))
        {
            if (QString::fromNSString(candidate.localizedName) == wanted)
            {
                target = candidate;
                break;
            }
        }
    }
    if (!target)
    {
        // Fall back to geometry: the screen whose size matches
        const QRect geometry = screen->geometry();
        for (NSScreen* candidate in [NSScreen screens])
        {
            const NSRect frame = candidate.frame;
            if (static_cast<int>(frame.size.width) == geometry.width() &&
                static_cast<int>(frame.size.height) == geometry.height())
            {
                target = candidate;
                break;
            }
        }
    }
    if (!target)
        target = [NSScreen mainScreen];
    if (!target)
        return;

    if (@available(macOS 12.0, *))
    {
        const double maxFps = target.maximumFramesPerSecond;
        const double minInterval = target.minimumRefreshInterval;  // seconds at the fastest rate
        const double maxInterval = target.maximumRefreshInterval;  // seconds at the slowest rate

        if (maxFps > 0.0)
            info.maxHz = maxFps;
        if (minInterval > 0.0 && maxInterval > 0.0)
        {
            info.maxHz = 1.0 / minInterval;
            if (maxInterval > minInterval * 1.01)
            {
                info.variable = true;
                info.minHz = 1.0 / maxInterval;
            }
        }
        if (info.currentHz <= 0.0)
            info.currentHz = info.maxHz;
        info.source = QStringLiteral("NSScreen");
    }
}
