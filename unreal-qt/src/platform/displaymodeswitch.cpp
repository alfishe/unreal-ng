#include "displaymodeswitch.h"

// macOS (platform/macos/displaymodeswitch_macos.mm) and Windows (platform/windows/displaymodeswitch_windows.cpp) have
// their own; X11 / Wayland have no portable way to change the mode
#if !defined(__APPLE__) && !defined(_WIN32)
std::unique_ptr<DisplayModeSwitch> DisplayModeSwitch::Create()
{
    return nullptr;
}
#endif
