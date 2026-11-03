#pragma once

#include <memory>
#include <string>
#include <vector>

class QScreen;

/// [VIDEO] DisplayRate = match (frame presenter design, section 4.6): the display's mode switched to another refresh
/// rate at the same resolution, and back. One display at a time: Set on another display restores the first.
/// - macOS: CoreGraphics display configuration for this application only (kCGConfigureForAppOnly): the system
///   restores the mode when the application ends, however it ends;
/// - Windows: ChangeDisplaySettingsEx with CDS_FULLSCREEN, a temporary mode Windows restores when the application
///   ends;
/// - elsewhere none (Create returns null): X11 / Wayland have no portable way.
class DisplayModeSwitch
{
public:
    virtual ~DisplayModeSwitch() = default;

    /// This platform's, or null
    static std::unique_ptr<DisplayModeSwitch> Create();

    /// The refresh rates of `screen`'s modes at its current resolution (the current one included) and the current rate
    virtual bool Rates(const QScreen* screen, std::vector<double>& rates, double& currentHz) = 0;
    /// The mode of the current resolution at `hz` on `screen`; `error` says why not
    virtual bool Set(const QScreen* screen, double hz, std::string& error) = 0;
    /// The display switched last gets its own mode back (nothing when none was switched)
    virtual void Restore() = 0;
    /// A display runs a mode Set chose
    virtual bool Switched() const = 0;
};
