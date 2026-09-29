#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include "emulator/video/screen.h"

class VideoController
{
public:
    /// The machine's renderer, a Screen subclass chosen by model family
    /// (PLAN #60(e)): ScreenTSConf for TS-Conf, ScreenZX for every classic
    /// machine (its helpers draw the ATM, Profi and AlCo modes). A family
    /// whose video is not a ZX screen with extra modes (TS-Conf, the
    /// Sprinter) gets its own subclass here. Every screen starts in M_ZX48
    static Screen* CreateScreen(MEM_MODEL model, EmulatorContext* context);
};
