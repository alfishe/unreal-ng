#pragma once

#include "emulator/memory/devicememory.h"
#include "emulator/video/sprinter/sprintervideoram.h"

/// The Sprinter's 256 KB video RAM as a device memory region "vram" (devicememory.h; Sprinter
/// automation audit G5). Offsets are video RAM addresses (row x 1024 + column); 16 pages of 16 KB
/// (16 rows each). Writes go through SprinterVideoRam::Write: a palette byte refreshes its pen,
/// a mode byte with the blank + INT pattern moves the frame INT, the beam catches up first.
/// The CPU copy in RAM pages #50-#5F is not touched (the PLD fills both only for CPU writes).
class SprinterVramRegion final : public IDeviceMemoryRegion
{
public:
    explicit SprinterVramRegion(SprinterVideoRam& vram) : _vram(vram) {}

    const char* Name() const override { return "vram"; }
    const char* Description() const override
    {
        return "Sprinter video RAM, 256 rows of 1024 bytes (offset = row x 1024 + column): columns #000-#2FF "
               "screens and fonts, #300-#39F the mode table, #3E0-#3FF the palettes (R, G, B per pen)";
    }
    uint32_t Size() const override { return static_cast<uint32_t>(SprinterVideoRam::kSize); }
    const char* WritePath() const override
    {
        return "SprinterVideoRam::Write: palette and frame INT follow, the beam catches up; RAM pages #50-#5F unchanged";
    }
    uint8_t Read(uint32_t offset) const override { return _vram.Read(offset); }
    void Write(uint32_t offset, uint8_t value) override { _vram.Write(offset, value); }

private:
    SprinterVideoRam& _vram;
};
