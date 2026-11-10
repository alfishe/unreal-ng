#include "displaymodeswitch.h"

// macOS (platform/macos/displaymodeswitch_macos.mm), Windows (platform/windows/displaymodeswitch_windows.cpp) and
// Linux X11 (platform/linux/displaymodeswitch_x11.cpp) have their own
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(__linux__)
std::unique_ptr<DisplayModeSwitch> DisplayModeSwitch::Create()
{
    return nullptr;
}
#endif

#ifndef __linux__
std::string DisplayModeSwitch::Unavailable()
{
    return "this platform cannot change the display's mode";
}
#endif
