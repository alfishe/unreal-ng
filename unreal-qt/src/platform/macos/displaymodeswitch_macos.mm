#include "platform/displaymodeswitch.h"

#include <QRect>
#include <QScreen>
#include <QString>

#include <cmath>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

namespace
{
/// The CoreGraphics display of a QScreen: by name, else by size (as DisplayRefreshRate does), else the main one
CGDirectDisplayID DisplayOf(const QScreen* screen)
{
    NSScreen* target = nil;
    for (NSScreen* candidate in [NSScreen screens])
        if (QString::fromNSString(candidate.localizedName) == screen->name())
        {
            target = candidate;
            break;
        }
    if (!target)
    {
        const QRect geometry = screen->geometry();
        for (NSScreen* candidate in [NSScreen screens])
            if (static_cast<int>(candidate.frame.size.width) == geometry.width() &&
                static_cast<int>(candidate.frame.size.height) == geometry.height())
            {
                target = candidate;
                break;
            }
    }
    if (!target)
        return CGMainDisplayID();
    NSNumber* number = [target.deviceDescription objectForKey:@"NSScreenNumber"];
    return number ? static_cast<CGDirectDisplayID>(number.unsignedIntValue) : CGMainDisplayID();
}

/// The same picture as `current`: points and pixels, usable for the desktop
bool SameResolution(CGDisplayModeRef mode, CGDisplayModeRef current)
{
    return CGDisplayModeGetWidth(mode) == CGDisplayModeGetWidth(current) &&
           CGDisplayModeGetHeight(mode) == CGDisplayModeGetHeight(current) &&
           CGDisplayModeGetPixelWidth(mode) == CGDisplayModeGetPixelWidth(current) &&
           CGDisplayModeGetPixelHeight(mode) == CGDisplayModeGetPixelHeight(current) &&
           CGDisplayModeIsUsableForDesktopGUI(mode);
}

bool Configure(CGDirectDisplayID display, CGDisplayModeRef mode)
{
    CGDisplayConfigRef config = nullptr;
    if (CGBeginDisplayConfiguration(&config) != kCGErrorSuccess)
        return false;
    if (CGConfigureDisplayWithDisplayMode(config, display, mode, nullptr) != kCGErrorSuccess)
    {
        CGCancelDisplayConfiguration(config);
        return false;
    }
    return CGCompleteDisplayConfiguration(config, kCGConfigureForAppOnly) == kCGErrorSuccess;
}

class MacDisplayModeSwitch : public DisplayModeSwitch
{
public:
    ~MacDisplayModeSwitch() override { Restore(); }

    bool Rates(const QScreen* screen, std::vector<double>& rates, double& currentHz) override
    {
        rates.clear();
        const CGDirectDisplayID display = DisplayOf(screen);
        CGDisplayModeRef current = CGDisplayCopyDisplayMode(display);
        if (!current)
            return false;
        currentHz = CGDisplayModeGetRefreshRate(current);
        CFArrayRef modes = CGDisplayCopyAllDisplayModes(display, nullptr);
        if (modes)
        {
            for (CFIndex i = 0; i < CFArrayGetCount(modes); i++)
            {
                auto mode = static_cast<CGDisplayModeRef>(const_cast<void*>(CFArrayGetValueAtIndex(modes, i)));
                const double hz = CGDisplayModeGetRefreshRate(mode);
                if (hz > 0.0 && SameResolution(mode, current))
                    rates.push_back(hz);
            }
            CFRelease(modes);
        }
        CGDisplayModeRelease(current);
        // A built-in panel reports 0 Hz: no rate to compare with
        return currentHz > 0.0 && !rates.empty();
    }

    bool Set(const QScreen* screen, double hz, std::string& error) override
    {
        const CGDirectDisplayID display = DisplayOf(screen);
        if (_original && display != _display)
            Restore();
        CGDisplayModeRef current = CGDisplayCopyDisplayMode(display);
        if (!current)
        {
            error = "the display's mode is unknown";
            return false;
        }
        CGDisplayModeRef chosen = nullptr;
        CFArrayRef modes = CGDisplayCopyAllDisplayModes(display, nullptr);
        if (modes)
        {
            for (CFIndex i = 0; i < CFArrayGetCount(modes) && !chosen; i++)
            {
                auto mode = static_cast<CGDisplayModeRef>(const_cast<void*>(CFArrayGetValueAtIndex(modes, i)));
                if (SameResolution(mode, current) && std::fabs(CGDisplayModeGetRefreshRate(mode) - hz) < 0.01)
                    chosen = CGDisplayModeRetain(mode);
            }
            CFRelease(modes);
        }
        if (!chosen)
        {
            CGDisplayModeRelease(current);
            error = "the display has no such mode";
            return false;
        }
        const bool ok = Configure(display, chosen);
        CGDisplayModeRelease(chosen);
        if (!ok)
        {
            CGDisplayModeRelease(current);
            error = "the display refused the mode";
            return false;
        }
        // The display's own mode, kept from the first switch on
        if (!_original)
        {
            _original = current;
            _display = display;
        }
        else
            CGDisplayModeRelease(current);
        return true;
    }

    void Restore() override
    {
        if (!_original)
            return;
        Configure(_display, _original);
        CGDisplayModeRelease(_original);
        _original = nullptr;
    }

    bool Switched() const override { return _original != nullptr; }

private:
    CGDirectDisplayID _display = 0;
    CGDisplayModeRef _original = nullptr;
};
}  // namespace

std::unique_ptr<DisplayModeSwitch> DisplayModeSwitch::Create()
{
    return std::make_unique<MacDisplayModeSwitch>();
}
