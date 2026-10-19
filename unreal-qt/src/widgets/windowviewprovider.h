#pragma once

#include <QPointer>
#include <functional>
#include <string>

#include "emulator/video/windowview.h"

class DeviceScreenWrapper;

/// unreal-qt's IWindowViewProvider (screenshot source "window", display settings): the main screen grabbed and the
/// View menu's display settings read / applied on the GUI thread.
/// Registered by MainWindow (WindowView::SetProvider) while its screen exists; the core never sees Qt
class WindowViewProvider : public IWindowViewProvider
{
public:
    /// `shownEmulatorId`: the instance the screen shows now, asked on the GUI thread ("" = none)
    using ShownEmulatorId = std::function<std::string()>;
    /// On the GUI thread: apply `change` (none = only read), then fill the settings as they are
    using Display = std::function<bool(const WindowDisplayChange* change, WindowDisplaySettings& out, std::string& error)>;

    WindowViewProvider(DeviceScreenWrapper* screen, ShownEmulatorId shownEmulatorId, Display display);

    bool GrabWindowView(const std::string& emulatorId, WindowViewGrab& out, std::string& error) override;
    bool GetWindowDisplay(WindowDisplaySettings& out, std::string& error) override;
    bool SetWindowDisplay(const WindowDisplayChange& change, WindowDisplaySettings& out, std::string& error) override;

private:
    /// On the GUI thread. Static: a grab queued before a timeout may run after the provider is gone
    static bool GrabHere(const QPointer<DeviceScreenWrapper>& screen, const ShownEmulatorId& shownEmulatorId,
                         const std::string& emulatorId, WindowViewGrab& out, std::string& error);

    QPointer<DeviceScreenWrapper> _screen;
    ShownEmulatorId _shownEmulatorId;
    Display _display;
};
