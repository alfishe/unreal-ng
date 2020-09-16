#include "stdafx.h"

#include "common/logger.h"

#include "videocontroller.h"
#include "common/collectionhelper.h"
#include "common/stringhelper.h"

Screen* VideoController::GetScreenForMode(VideoModeEnum mode, EmulatorContext* context)
{
    Screen* result = nullptr;

    // Create new video mode screen
    switch (mode)
    {
        case M_NUL:
            break;
        case M_ZX48:
            result = new ScreenZX(context);
            break;
        default:
            {
                std::string error = StringHelper::Format("Unknown video mode: %d", mode);
                LOGERROR(error.c_str());
                throw std::logic_error(error);
            }
            break;
    }

    return result;
}