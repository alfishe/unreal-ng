#pragma once
#include "emulator/video/screen.h"
#include "stdafx.h"

class Memory;

/// Profi 512x240 hi-res video mode renderer (DFFD.7).
/// Handles M_PROFIHR only. Not a Screen subclass - it never stands alone as
/// an active renderer, it is owned and driven by ScreenZX (which remains the
/// single Screen* registered per machine) and only allocated for machines
/// whose detected video mode actually becomes M_PROFIHR.
class ScreenProfi
{
    /// region <Constructors / Destructors>
public:
    ScreenProfi(EmulatorContext* context, Memory* memory);
    /// endregion </Constructors / Destructors>

    /// region <Methods>
public:
    /// Render a single T-state for the Profi hi-res video mode
    /// @param tstate T-state timing position
    /// @param rd Raster descriptor for M_PROFIHR (geometry/timing)
    /// @param framebuffer Target framebuffer (ARGB)
    /// @param borderColor Current border color latch (bits 0-2), as tracked by
    ///        ScreenZX::_borderColor - passed in rather than read back from
    ///        EmulatorState so the render sees the exact pre-flush value
    ///        Screen::SetBorderColor uses when it replays the pending T-range
    ///        with the OLD color before latching the new one.
    void Draw(uint32_t tstate, const RasterDescriptor& rd, FramebufferDescriptor& framebuffer, uint8_t borderColor);
    /// endregion </Methods>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    Memory* _memory;
    /// endregion </Fields>
};
