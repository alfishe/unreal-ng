#pragma once

/// @file nextvideorenderer.h
/// @brief The ZX Spectrum Next picture, one raster line at a time (design-video-timing.md, research-fpga-vhdl.md
/// sections 8-9 and 16-17): the ULA (standard, Timex hi-colour / hi-res, ULANext) or LoRes, Layer 2 in its three
/// resolutions, the 9-bit palettes, the global transparency, the layer order and the fallback colour.
///
/// The frame is 640 x 256 sub-pixels: the 320 x 256 visible grid of the 7 MHz pixel clock (a 32-pixel border
/// around the 256 x 192 paper), every normal pixel doubled, so the 512-pixel Timex hi-res mode and Layer 2's 640 x 256
/// mode have a sub-pixel each. The tilemap and the sprites are drawn here; the blend modes are N7; a transparent line stands in for them.
/// The renderer reads state only: the caller builds NextVideoInputs for the moment the line is drawn.

#include <cstdint>

#include "emulator/io/z80n/nextsprites.h"
#include "emulator/io/z80n/nextvideoregs.h"
#include "emulator/video/next/nextlayerframe.h"

struct NextVideoInputs
{
    /// RAM as the video logic sees it: 16K page p (the numbering of the Layer 2 banks, NR #12) at ram + p * 16K
    const uint8_t* ram = nullptr;
    uint16_t ramPages = 0;
    const NextVideoRegs* regs = nullptr;  ///< palettes, clip windows
    NextSprites* sprites = nullptr;       ///< drawn from here (its collision / overtime flags are set by drawing)
    uint8_t nr[256] = {};                 ///< the stored NextREG bytes (scrolls, layer control, timex port alias, ...)
    uint8_t border = 0;                   ///< port #FE bits 2:0
    uint8_t portFf = 0;                   ///< the Timex port (NR #69 bits 5:0 alias it)
    bool shadowScreen = false;            ///< the ULA reads bank 7 instead of 5 (#7FFD bit 3 / NR #69 bit 6)
    bool layer2Enable = false;            ///< port #123B bit 1 / NR #69 bit 7
    bool flash = false;                   ///< the FLASH attribute phase
    /// When non-null RenderLine also stores the four layer rows, the winner and the sprite number of each pixel here
    /// (design-layer-capture.md); null: nothing is recorded and the line is drawn as before
    const NextLayer::LineSink* layerOut = nullptr;
};

class NextVideoRenderer
{
public:
    static constexpr unsigned kWidth = 640;
    static constexpr unsigned kHeight = 256;
    static constexpr unsigned kPaperLeft = 64;   ///< paper x in sub-pixels (32 pixels of border)
    static constexpr unsigned kPaperTop = 32;    ///< paper y

    /// Draw line `y` (0..255) into `out` (kWidth RGBA pixels, bytes R G B A)
    static void RenderLine(const NextVideoInputs& in, unsigned y, uint32_t* out);

    /// A 9-bit colour (RRRGGGBBb) as RGBA bytes
    static uint32_t Rgba(uint16_t colour9);

private:
    using Pixel = NextLayer::Pixel;
    static void DrawLayers(const NextVideoInputs& in, unsigned y, Pixel* ula, Pixel* layer2, Pixel* tiles, Pixel* sprites,
                           uint8_t* spriteNumbers);
    static void UlaLine(const NextVideoInputs& in, unsigned y, Pixel* line);
    static void LoResLine(const NextVideoInputs& in, unsigned y, Pixel* line);
    static void ApplyUlaClip(const NextVideoInputs& in, unsigned y, Pixel* line);
    static void SpriteLine(const NextVideoInputs& in, unsigned y, Pixel* line, uint8_t* numbers);
    template <bool kRecord>
    static void ComposeLine(const NextVideoInputs& in, const Pixel* ula, const Pixel* layer2, const Pixel* tiles, const Pixel* sprites,
                            const uint8_t* spriteNumbers, uint32_t* out);
    static void TilemapLine(const NextVideoInputs& in, unsigned y, Pixel* line);
    static void Layer2Line(const NextVideoInputs& in, unsigned y, Pixel* line);
};
