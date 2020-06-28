#pragma once
#include "stdafx.h"

#include "emulator/video/screen.h"
#include "emulator/video/zx/screenzx.h"
#include <map>

class VideoController
{
protected:
    // std::unique_ptr is managing Screen object release and destroy once global _screens map destroyed
    static std::map<VideoModeEnum, std::unique_ptr<Screen*>> _screens;

public:
    static Screen* GetScreenForMode(VideoModeEnum mode, EmulatorContext* context);
};