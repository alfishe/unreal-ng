#include "platform/displaymodeswitch.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QScreen>
#include <QString>

#include <cmath>

namespace
{
/// The same picture as `current`: size, depth, progressive
bool SameResolution(const DEVMODEW& mode, const DEVMODEW& current)
{
    return mode.dmPelsWidth == current.dmPelsWidth && mode.dmPelsHeight == current.dmPelsHeight &&
           mode.dmBitsPerPel == current.dmBitsPerPel && (mode.dmDisplayFlags & DM_INTERLACED) == 0;
}

class WindowsDisplayModeSwitch : public DisplayModeSwitch
{
public:
    ~WindowsDisplayModeSwitch() override { Restore(); }

    bool Rates(const QScreen* screen, std::vector<double>& rates, double& currentHz) override
    {
        rates.clear();
        // QScreen::name() is the GDI device name ("\\.\DISPLAY1") on the windows platform plugin
        const std::wstring device = screen->name().toStdWString();
        DEVMODEW current{};
        current.dmSize = sizeof(current);
        if (!EnumDisplaySettingsW(device.c_str(), ENUM_CURRENT_SETTINGS, &current))
            return false;
        currentHz = current.dmDisplayFrequency > 1 ? current.dmDisplayFrequency : 0.0;
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        for (DWORD i = 0; EnumDisplaySettingsW(device.c_str(), i, &mode); ++i)
            if (SameResolution(mode, current) && mode.dmDisplayFrequency > 1)
                rates.push_back(mode.dmDisplayFrequency);
        return currentHz > 0.0 && !rates.empty();
    }

    bool Set(const QScreen* screen, double hz, std::string& error) override
    {
        const std::wstring device = screen->name().toStdWString();
        if (_switched && device != _device)
            Restore();
        DEVMODEW current{};
        current.dmSize = sizeof(current);
        if (!EnumDisplaySettingsW(device.c_str(), ENUM_CURRENT_SETTINGS, &current))
        {
            error = "the display's mode is unknown";
            return false;
        }
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        bool found = false;
        for (DWORD i = 0; !found && EnumDisplaySettingsW(device.c_str(), i, &mode); ++i)
            found = SameResolution(mode, current) && std::fabs(mode.dmDisplayFrequency - hz) < 0.5;
        if (!found)
        {
            error = "the display has no such mode";
            return false;
        }
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
        // CDS_FULLSCREEN: a temporary mode, not written to the registry; Windows restores it when the process ends
        const LONG result = ChangeDisplaySettingsExW(device.c_str(), &mode, nullptr, CDS_FULLSCREEN, nullptr);
        if (result != DISP_CHANGE_SUCCESSFUL)
        {
            error = "the display refused the mode (" + std::to_string(result) + ")";
            return false;
        }
        _switched = true;
        _device = device;
        return true;
    }

    void Restore() override
    {
        if (!_switched)
            return;
        // No mode: back to the one in the registry
        ChangeDisplaySettingsExW(_device.c_str(), nullptr, nullptr, 0, nullptr);
        _switched = false;
    }

    bool Switched() const override { return _switched; }

private:
    std::wstring _device;
    bool _switched = false;
};
}  // namespace

std::unique_ptr<DisplayModeSwitch> DisplayModeSwitch::Create()
{
    return std::make_unique<WindowsDisplayModeSwitch>();
}

#endif  // _WIN32
