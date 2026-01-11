#pragma once
#include "3rdparty/gif/gif.h"
#include "stdafx.h"

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

    /// Write frame using auto-calculated palette (original behavior)
    void WriteFrame(uint32_t* buffer, size_t size);

    /// Write frame using pre-built palette (fast path - skips palette calculation)
    void WriteFrameWithPalette(uint32_t* buffer, size_t size, GifPalette* palette, bool dither = false);
    /// endregion </Methods>
};
