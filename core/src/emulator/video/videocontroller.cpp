#include "stdafx.h"

#include "videocontroller.h"

#include "emulator/video/sprinter/screensprinter.h"
#include "emulator/video/tsconf/screentsconf.h"
#include "emulator/video/zx/screenzx.h"

Screen* VideoController::CreateScreen(MEM_MODEL model, EmulatorContext* context)
{
    switch (model)
    {
        case MM_TSL:
            return new ScreenTSConf(context);
        case MM_SPRINTER:
            return new ScreenSprinter(context);
        default:
            return new ScreenZX(context);
    }
}
