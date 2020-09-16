#include "stdafx.h"

#include "common/logger.h"

#include "videocontroller.h"
#include "common/collectionhelper.h"

// Defining static collection
map<VideoModeEnum, std::shared_ptr<Screen*>> VideoController::_screens;

Screen* VideoController::GetScreenForMode(VideoModeEnum mode, EmulatorContext* context)
{
    Screen* result = nullptr;

    if (key_exists(_screens, mode))
    {
        // Video mode screen was pre-created and cached
        result = *_screens[mode].get();
    }
    else
    {
        // Create new video mode screen
        switch (mode)
        {
            case M_NUL:
                break;
            case M_ZX48:
                result = new ScreenZX(context);
                break;
            default:
                LOGWARNING("Unknown video mode: %d", mode);
                break;
        }

        // Cache screen for future use
        if (result != nullptr)
            _screens.insert({ mode, std::make_shared<Screen*>(result) });
    }

    return result;
}