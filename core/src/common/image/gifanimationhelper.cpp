#include "stdafx.h"

#include "gifanimationhelper.h"

#include "common/logger.h"

void GIFAnimationHelper::StartAnimation(std::string filename, unsigned width, unsigned height, unsigned delayMs)
{
    if (_started)
        StopAnimation();

    GifBegin(&_gifWriter, filename.c_str(), width, height, delayMs / 10);

    _width = width;
    _height = height;
    _delayMs = delayMs;

    _started = true;
}

void GIFAnimationHelper::StopAnimation()
{
    GifEnd(&_gifWriter);

    _started = false;
}

void GIFAnimationHelper::WriteFrame(uint32_t* buffer, [[maybe_unused]] size_t size)
{
    (void)size; // Mark as intentionally unused

    if (_started)
    {
        GifWriteFrame(&_gifWriter, (uint8_t *)buffer, _width, _height, _delayMs / 10);
    }
    else
    {
        LOGWARNING("GIFAnimationHelper::WriteFrame - Unable to write frame. Animation not started properly.");
    }
}
