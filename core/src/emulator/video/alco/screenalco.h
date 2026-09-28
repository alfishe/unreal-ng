#pragma once
#include "emulator/video/screen.h"
#include "emulator/video/zx/tstatecoordlut.h"
#include "stdafx.h"

class Memory;

/// Pentagon 1024 / ZX-Evo BaseConf EFF7 z-mode renderer over the ZX raster:
///   M_P16 (AlCo 16-color 256x192) and M_PMC (hardware multicolor, 8x1
///   attributes). Reached on Pentagon and on ATM3 / ZX-Evo, so it is a video
///   family renderer, not a machine. Owned and driven by ScreenZX, which stays
///   the single Screen* per machine; it reuses ScreenZX's T-state LUT.
class ScreenAlco final
{
public:
    ScreenAlco(EmulatorContext* context, Memory* memory);

    /// Render the inclusive frame T-state range [from, to]
    /// @param lut ScreenZX's per-T coordinate table for the current raster
    /// @param fbWidth framebuffer row stride in pixels
    /// @param flash current 16-frame flash phase (PMC attribute bit 7)
    void DrawRange(uint32_t from, uint32_t to, VideoModeEnum mode, const TstateCoordLUT* lut, uint16_t fbWidth,
                   FramebufferDescriptor& framebuffer, bool flash);

private:
    EmulatorContext* _context;
    Memory* _memory;
};
