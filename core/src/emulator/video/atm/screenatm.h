#pragma once
#include "emulator/video/screen.h"
#include "stdafx.h"

class Memory;

/// ATM Turbo 2+/3/710 extended video mode renderer.
/// Handles M_ATM16 (EGA 320x200 16-color), M_ATMHR (Hardware Multicolor
/// 640x200 hires), M_ATMTX (Text 80x25, 640x200) and M_ATMTL (ZX-Evo Text
/// Linear, 640x200). Not a Screen subclass - it never stands alone as an
/// active renderer, it is owned and driven by ScreenZX (which remains the
/// single Screen* registered per machine) and only allocated for ATM
/// machine models.
class ScreenAtm
{
    /// region <Constructors / Destructors>
public:
    ScreenAtm(EmulatorContext* context, Memory* memory);
    /// endregion </Constructors / Destructors>

    /// region <Constants>
public:
    /// Screen T-window inside a 224T line: the 320-dot ATM window starts 16T
    /// before the ZX paper (T=24) and ends 16T after it (ZX-Evo BaseConf
    /// video_sync_h.v HPIX_BEG_ATM=108 / HPIX_END_ATM=428 vs Pentagon 140/396,
    /// in 2-dot T units; Xpeccy vid_atm_org).
    static constexpr uint32_t SCREEN_START_T = 8;
    static constexpr uint32_t SCREEN_END_T = SCREEN_START_T + 160;
    /// endregion </Constants>

    /// region <Methods>
public:
    /// Render a single T-state for the current ATM extended video mode
    void Draw(uint32_t tstate, VideoModeEnum mode, const RasterDescriptor& rd, FramebufferDescriptor& framebuffer);
    /// endregion </Methods>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    Memory* _memory;
    /// endregion </Fields>
};
