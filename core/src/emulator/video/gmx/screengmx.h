#pragma once
#include "emulator/video/screen.h"
#include "stdafx.h"

class Memory;

/// Scorpion GMX 640x200 extended video mode renderer (#7EFD bit 3, GMX design section 5).
/// Handles M_GMX only. Like ScreenProfi it is not a Screen subclass: ScreenZX owns and drives it and allocates it
/// only when the detected mode becomes M_GMX.
class ScreenGmx final
{
    /// region <Constructors / Destructors>
public:
    ScreenGmx(EmulatorContext* context, Memory* memory);
    /// endregion </Constructors / Destructors>

    /// region <Methods>
public:
    /// Render one T-state of the extended mode
    /// @param tstate T-state in the frame
    /// @param rd Raster descriptor for M_GMX
    /// @param framebuffer Target framebuffer (ABGR)
    /// @param borderColor The border latch (bits 0-2) as ScreenZX tracks it
    /// @param flashPhase The flash phase of this frame (Screen::_vid.flash)
    void Draw(uint32_t tstate, const RasterDescriptor& rd, FramebufferDescriptor& framebuffer, uint8_t borderColor,
              bool flashPhase);

    /// Render the inclusive frame T-state range [from, to]
    void DrawRange(uint32_t from, uint32_t to, const RasterDescriptor& rd, FramebufferDescriptor& framebuffer,
                   uint8_t borderColor, bool flashPhase);
    /// endregion </Methods>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    Memory* _memory;
    /// endregion </Fields>
};
