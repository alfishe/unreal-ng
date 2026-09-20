#pragma once

#include "unrealng_embed.h"
#include <cstdint>
#include <cstddef>

class Emulator;

class EmbedVideo
{
public:
    EmbedVideo() = default;

    app_result GetFrameInfo(Emulator* emu, uint16_t* width, uint16_t* height, uint64_t* latchTimestampUs);
    app_result CopyFrame(Emulator* emu, void* dstRgba8, size_t dstSize);
    void SetPresentDelay(Emulator* emu, uint8_t frames);
};
