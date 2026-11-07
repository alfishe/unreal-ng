#pragma once

#include <memory>
#include <string>
#include <vector>

class QScreen;

/// [VIDEO] DisplayRate = match (frame presenter design, section 4.7): the display's mode switched to another refresh
/// rate at the same resolution, and back. One display at a time: Set on another display restores the first.
/// - macOS: CoreGraphics display configuration for this application only (kCGConfigureForAppOnly): the system
///   restores the mode when the application ends, however it ends;
/// - Windows: ChangeDisplaySettingsEx with CDS_FULLSCREEN, a temporary mode Windows restores when the application
///   ends;
/// - Linux X11: RandR (XRRSetCrtcConfig) on the CRTC under the window, libXrandr loaded at run time; the destructor
///   restores it (X11 itself keeps a mode a crashed process set);
/// - gamescope (the Steam Deck's Game Mode): gamescope_control's target refresh cycle, the request of Steam's refresh
///   rate slider, gamescope making the panel's mode; Restore asks for the display's own rate back (gamescope keeps
///   the setting after the process ends);
/// - elsewhere none (Create returns null, Unavailable says why): Wayland and XWayland leave the mode to the compositor.
class DisplayModeSwitch
{
public:
    virtual ~DisplayModeSwitch() = default;

    /// This platform's, or null
    static std::unique_ptr<DisplayModeSwitch> Create();
    /// Why Create returned null here
    static std::string Unavailable();

    /// The refresh rates of `screen`'s modes at its current resolution (the current one included) and the current rate
    virtual bool Rates(const QScreen* screen, std::vector<double>& rates, double& currentHz) = 0;
    /// The mode of the current resolution at `hz` on `screen`; `error` says why not
    virtual bool Set(const QScreen* screen, double hz, std::string& error) = 0;
    /// The display switched last gets its own mode back (nothing when none was switched)
    virtual void Restore() = 0;
    /// A display runs a mode Set chose
    virtual bool Switched() const = 0;
};
