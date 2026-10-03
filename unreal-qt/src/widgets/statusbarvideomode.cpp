#include "statusbarvideomode.h"

#include "emulator/video/screen.h"

StatusBarVideoMode StatusBarVideoMode::From(const ScreenState& state)
{
    StatusBarVideoMode mode;
    if (state.videoModeBrief.empty())
        return mode;
    mode.text = QString::fromStdString(state.videoModeBrief);
    mode.toolTip = QString::fromStdString(state.videoMode);
    mode.visible = true;
    return mode;
}
