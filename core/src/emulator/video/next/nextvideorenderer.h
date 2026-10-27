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
    NextSprites* sprites = nullptr;       ///< the output stage's, and the engine's when `spriteLine` is null
    /// The line buffer the sprite engine filled for this row in machine time (ScreenNext, which posts its flags); null: run the engine on
    /// `sprites` as it is and set its collision / overtime flags at once
    const NextSprites::LineBuffer* spriteLine = nullptr;
    uint8_t nr[256] = {};                 ///< the stored NextREG bytes (scrolls, layer control, timex port alias, ...)
    uint8_t border = 0;                   ///< port #FE bits 2:0
    uint8_t portFf = 0;                   ///< the Timex port (NR #69 bits 5:0 alias it)
    bool shadowScreen = false;            ///< the ULA reads bank 7 instead of 5 (#7FFD bit 3 / NR #69 bit 6)
    bool layer2Enable = false;            ///< port #123B bit 1 / NR #69 bit 7
    bool flash = false;                   ///< the FLASH attribute phase
    unsigned lineClocks = 1792;           ///< the line period in 28 MHz clocks (4 per 7 MHz pixel: 1792 for 448 pixels, 1824 for 456): the sprite engine's time
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

    /// Append to `frame`'s line table the register state `in` holds for the stretch [fromX, toX) (sub-pixels) of grid line y: what the pixel
    /// inspector needs to recompute where a pixel came from. Called by ScreenNext before each segment it draws
    static void RecordSegment(NextLayer::Frame& frame, unsigned y, unsigned fromX, unsigned toX, const NextVideoInputs& in);

    /// A 9-bit colour (RRRGGGBBb) as RGBA bytes
    static uint32_t Rgba(uint16_t colour9);

    /// Where the ULA's paper sub-pixel `sub` (0..511, 14 MHz) of paper row `py` (0..191) takes its bitmap bit and its attribute from
    /// (zxula.vhd: the screen mode of port #FF bits 2:0 - 000 while the shadow screen is shown - the Y scroll NR #27, the X scroll NR #26
    /// and the half-pixel fine scroll NR #68 bit 2). Offsets are inside the 16K page of the screen (bank 5, or 7 for the shadow screen)
    struct UlaFetch
    {
        unsigned mode = 0;          ///< the screen mode in force (port #FF bits 2:0, 0 for the shadow screen)
        unsigned bitmapOffset = 0;  ///< the byte holding the pixel
        unsigned bit = 0;           ///< its bit number counted from the left (0 = bit 7)
        unsigned attrOffset = 0;    ///< the attribute byte (the second bitmap byte of the hi-res modes 100 / 101 / 110 / 111 for the odd bytes)
        bool synthesised = false;   ///< hi-res: no attribute byte, it is bright | paper (7 - ink) << 3 | ink of port #FF bits 5:3
    };
    static UlaFetch UlaFetchAt(unsigned portFf, bool shadowScreen, unsigned scrollX, unsigned scrollY, bool fineScroll, unsigned py, unsigned sub);

    /// layer2.vhd for the 320 x 256 and 640 x 256 modes: the column (0..511, the address's x field) the pixel at 320-grid position `hc`
    /// (0..319) reads with the 9-bit scroll `scrollX` (NR #71 bit 0 : NR #16). Below 320 it is the plain sum, from 320 on the VHDL's
    /// +3 on bits 8:6 (a modulo 320 as long as hc + scroll < 640; a scroll above 319 reads the columns 320..510, the banks after the bitmap)
    static unsigned Layer2WideColumn(unsigned hc, unsigned scrollX);

private:
    using Pixel = NextLayer::Pixel;
    /// Which of the layers above the ULA may have an opaque pixel on the line (bits of Above); a layer that is off leaves its row as
    /// the caller cleared it
    enum Above : unsigned
    {
        kAboveLayer2 = 1,
        kAboveTilemap = 2,
        kAboveSprites = 4
    };
    static unsigned DrawLayers(const NextVideoInputs& in, unsigned y, Pixel* ula, Pixel* layer2, Pixel* tiles, Pixel* sprites,
                               uint8_t* spriteNumbers);
    static void UlaLine(const NextVideoInputs& in, unsigned y, Pixel* line);
    static void LoResLine(const NextVideoInputs& in, unsigned y, Pixel* line);
    static void ApplyUlaClip(const NextVideoInputs& in, unsigned y, Pixel* line);
    static bool SpriteLine(const NextVideoInputs& in, unsigned y, Pixel* line, uint8_t* numbers);
    template <bool kRecord>
    static void ComposeLine(const NextVideoInputs& in, unsigned above, const Pixel* ula, const Pixel* layer2, const Pixel* tiles,
                            const Pixel* sprites, const uint8_t* spriteNumbers, uint32_t* out);
    /// A line where only the ULA layer has pixels, in a plain layer order without the stencil: the ULA pixel or the fallback colour
    template <bool kRecord>
    static void ComposeUlaOnly(const NextVideoInputs& in, const Pixel* ula, uint32_t* out);
    /// The compositor's loop for one layer order (NR #15 bits 4:2): the order is the same for the whole line, so each instance has it
    /// folded in
    template <bool kRecord, unsigned kOrder>
    static void ComposeOrder(const NextVideoInputs& in, const Pixel* ula, const Pixel* layer2, const Pixel* tiles, const Pixel* sprites,
                             const uint8_t* spriteNumbers, uint32_t* out);
    static bool TilemapLine(const NextVideoInputs& in, unsigned y, Pixel* line);
    static bool Layer2Line(const NextVideoInputs& in, unsigned y, Pixel* line);
};
