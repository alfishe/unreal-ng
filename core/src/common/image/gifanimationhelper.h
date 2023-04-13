#pragma once
#include "stdafx.h"

#include "3rdparty/gif/gif.h"

class GIFAnimationHelper
{
    /// region <Fields>
protected:
    GifWriter _gifWriter;
    bool _started = false;

    unsigned _width = 0;
    unsigned _height = 0;
    unsigned _delayMs = 20;
    /// endregion </Fields>

    /// region <Methods>
public:
    void StartAnimation(std::string filename, unsigned width, unsigned height, unsigned delayMs = 20);
    void StopAnimation();

    void WriteFrame(uint32_t* buffer, size_t size);
    /// endregion </Methods>
};
