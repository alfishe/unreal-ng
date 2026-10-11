#pragma once

/// @file nextpixelinspector.h
/// @brief The pixel inspector of the Next picture (design-layer-capture.md section 3.5): for one pixel of the last captured frame, which layer
/// won, the colour, and per layer whether it painted, the colour, why it lost or did not paint ("transparent (index 15)", "clipped (NR #1A
/// window)", "layer order LSU: layer2 above", "below the ULA (tile attribute bit 0)") and where its pixel came from (the ULA bitmap and
/// attribute byte, the tile number and its definition byte, the Layer 2 byte, the sprite number and its pattern byte).
///
/// The winner, the colours and the opacity are read from the record (exact, whatever the copper did). The source addresses are recomputed from
/// the pixel position and the register state the line segment was drawn with (the segment table of the frame); the bytes shown are those of
/// the memory NOW, so on a running machine they can be a frame newer than the record, on a paused one they are exact.

#include <cstdint>
#include <string>

#include "emulator/io/z80n/nextsprites.h"
#include "emulator/io/z80n/nextvideoregs.h"
#include "emulator/state/statenode.h"
#include "emulator/video/next/nextlayerframe.h"

namespace NextLayer
{
/// What the inspector reads besides the frame
struct InspectEnv
{
    const uint8_t* ram = nullptr;  ///< 16K page p at ram + p * 16K (NextVideoInputs::ram)
    unsigned ramPages = 0;
    const NextVideoRegs* regs = nullptr;  ///< palettes (the palette entry behind a recomputed index)
    const NextSprites* sprites = nullptr;
};

/// The report for the grid pixel (x 0-319, y 0-255) and its sub-pixel sx (0 or 1). A pixel whose line has no segment (a frame that was
/// not completed) is `ok: false`
StateNode InspectPixel(const Frame& frame, const InspectEnv& env, unsigned x, unsigned y, unsigned sx);

/// The composed colour (9 bits) of a pixel of the frame, recomputed from the record: the colour of the winning layer, the stencil / blend
/// formula of its inputs, or the fallback colour. Equal to what the compositor drew
uint16_t ComposedColour(const Frame& frame, unsigned y, unsigned subpixel);
}  // namespace NextLayer
