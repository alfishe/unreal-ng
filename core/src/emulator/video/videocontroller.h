#pragma once
#include "stdafx.h"

#include "emulator/video/screen.h"
#include "emulator/video/zx/screenzx.h"
#include <map>

class VideoController
{
public:
    static Screen* GetScreenForMode(VideoModeEnum mode, EmulatorContext* context);
};