// eve-emu - the graphics engine's internals shared by eve-dl, eve-raster, eve-bitmap,
// eve-pixel and eve-output (spec §5, §6, arch §8.4).
//
// A picture is built line by line (spec §5.1): for each visible line the active display
// list runs from its start, every primitive is asked for its span on that line, and the
// span's pixels go through the per-pixel pipeline into the line buffers.
#pragma once

#include "eve-internal.h"

namespace EveLib
{

// --- Display list encoding (spec §6.1) -----------------------------------------------------

enum : uint32_t
{
    kOpDisplay = 0x00,
    kOpBitmapSource = 0x01,
    kOpClearColorRgb = 0x02,
    kOpTag = 0x03,
    kOpColorRgb = 0x04,
    kOpBitmapHandle = 0x05,
    kOpCell = 0x06,
    kOpBitmapLayout = 0x07,
    kOpBitmapSize = 0x08,
    kOpAlphaFunc = 0x09,
    kOpStencilFunc = 0x0A,
    kOpBlendFunc = 0x0B,
    kOpStencilOp = 0x0C,
    kOpPointSize = 0x0D,
    kOpLineWidth = 0x0E,
    kOpClearColorA = 0x0F,
    kOpColorA = 0x10,
    kOpClearStencil = 0x11,
    kOpClearTag = 0x12,
    kOpStencilMask = 0x13,
    kOpTagMask = 0x14,
    kOpBitmapTransformA = 0x15,
    kOpBitmapTransformB = 0x16,
    kOpBitmapTransformC = 0x17,
    kOpBitmapTransformD = 0x18,
    kOpBitmapTransformE = 0x19,
    kOpBitmapTransformF = 0x1A,
    kOpScissorXy = 0x1B,
    kOpScissorSize = 0x1C,
    kOpCall = 0x1D,
    kOpJump = 0x1E,
    kOpBegin = 0x1F,
    kOpColorMask = 0x20,
    kOpEnd = 0x21,
    kOpSaveContext = 0x22,
    kOpRestoreContext = 0x23,
    kOpReturn = 0x24,
    kOpMacro = 0x25,
    kOpClear = 0x26,
    kOpVertexFormat = 0x27,
    kOpBitmapLayoutH = 0x28,
    kOpBitmapSizeH = 0x29,
    kOpPaletteSource = 0x2A,
    kOpVertexTranslateX = 0x2B,
    kOpVertexTranslateY = 0x2C,
    kOpNop = 0x2D,
};

constexpr uint32_t kOpcodeShift = 24;
constexpr uint32_t kVertexKindShift = 30;
constexpr uint32_t kVertex2f = 1;
constexpr uint32_t kVertex2ii = 2;

enum Primitive : uint8_t
{
    kPrimNone = 0,
    kPrimBitmaps = 1,
    kPrimPoints = 2,
    kPrimLines = 3,
    kPrimLineStrip = 4,
    kPrimEdgeStripR = 5,
    kPrimEdgeStripL = 6,
    kPrimEdgeStripA = 7,
    kPrimEdgeStripB = 8,
    kPrimRects = 9,
};

// Bitmap formats [PG §4.7 Table 7].
enum BitmapFormat : uint8_t
{
    kFormatArgb1555 = 0,
    kFormatL1 = 1,
    kFormatL4 = 2,
    kFormatL8 = 3,
    kFormatRgb332 = 4,
    kFormatArgb2 = 5,
    kFormatArgb4 = 6,
    kFormatRgb565 = 7,
    kFormatText8x8 = 9,
    kFormatTextVga = 10,
    kFormatBargraph = 11,
    kFormatPaletted565 = 14,
    kFormatPaletted4444 = 15,
    kFormatPaletted8 = 16,
    kFormatL2 = 17,
};

// Comparison functions [PG §4.4].
enum TestFunc : uint8_t
{
    kFuncNever = 0, kFuncLess = 1, kFuncLequal = 2, kFuncGreater = 3,
    kFuncGequal = 4, kFuncEqual = 5, kFuncNotequal = 6, kFuncAlways = 7
};

// Blend factors [PG §4.10].
enum BlendFactor : uint8_t
{
    kBlendZero = 0, kBlendOne = 1, kBlendSrcAlpha = 2, kBlendDstAlpha = 3,
    kBlendOneMinusSrcAlpha = 4, kBlendOneMinusDstAlpha = 5
};

// Stencil operations [PG §4.44]; 6 / 7 per the TS-Labs SDK names.
enum StencilOp : uint8_t
{
    kStencilZero = 0, kStencilKeep = 1, kStencilReplace = 2, kStencilIncr = 3,
    kStencilDecr = 4, kStencilInvert = 5, kStencilIncrWrap = 6, kStencilDecrWrap = 7
};

constexpr uint32_t kSubpixel = 16;          // positions are in 1/16 pixel
constexpr uint32_t kSubpixelShift = 4;
constexpr uint32_t kFixedShift = 8;         // transform coefficients: 8 fraction bits
constexpr int32_t kFixedOneTransform = 256; // 1.0 in 8.8
constexpr uint32_t kContextStackDepth = 4;  // SAVE_CONTEXT levels [PG §4.39]
constexpr uint32_t kCallStackDepth = 4;     // CALL levels [PG §4.19]
constexpr uint32_t kMaxLineWidth = 4096;    // HSIZE is 12 bits
constexpr uint32_t kMaxLines = 4096;        // VSIZE is 12 bits
constexpr uint32_t kChannels = 4;           // line buffer: R, G, B, A
constexpr uint32_t kChannelMax = 255;
constexpr uint32_t kBitmapSizeBits = 9;     // BITMAP_SIZE width / height low bits
constexpr uint32_t kBitmapSizeFull = 2048;  // a width / height of 0 means 2048
constexpr uint32_t kFillCostScale = 16;     // fill cost counted in 1/16 clock

// Color mask bits of the context.
constexpr uint8_t kMaskRed = 8, kMaskGreen = 4, kMaskBlue = 2, kMaskAlpha = 1;

// The three ways a line is executed.
enum class LineMode { Draw, Probe, StateOnly };

// A vertex in 1/16 pixel, with the bitmap handle and cell it selects.
struct Vertex
{
    int32_t x, y;
    uint8_t handle, cell;
    uint32_t index;   // display list index, for probing
    uint32_t word;
};

// A palette decoded for one line (memory does not change while a line is drawn).
struct PaletteCache
{
    bool valid;
    uint8_t format;
    uint32_t source;
    uint32_t entry[kPaletteEntries];
};

// One execution of the display list for one line.
struct LineRun
{
    EveChip* chip;
    BitmapHandle* handles; // eve-accel 03: the handle table this run reads and writes
    uint32_t y;            // logical line (after REG_ROTATE)
    uint32_t width;        // pixels in the line buffer
    uint8_t* color;        // kChannels bytes per pixel
    uint8_t* stencil;
    uint8_t* tag;
    uint32_t* texels;      // scratch: a span's decoded texels (kMaxLineWidth)
    GraphicsContext ctx;
    GraphicsContext saved[kContextStackDepth];
    uint32_t savedCount;
    uint8_t primitive;
    uint32_t vertexCount;  // since BEGIN
    Vertex previous;
    // Line cost (spec §5.2).
    uint32_t commands;
    uint64_t fillCost;     // in 1/kFillCostScale clock
    uint32_t events;       // stack misuse, unknown opcodes, cut loops
    PaletteCache palette;
    // Probe.
    int32_t probeX;
    EvePixelSource* probe;
    uint32_t commandIndex;
    uint32_t commandWord;
};

// --- eve-dl.cpp --------------------------------------------------------------------------------

void ContextDefaults(GraphicsContext& ctx);
template <LineMode Mode>
void ExecuteLine(LineRun& run);
// Bitmap size of a handle in pixels (0 means 2048).
uint32_t HandleWidth(const BitmapHandle& h);
uint32_t HandleHeight(const BitmapHandle& h);
uint32_t HandleStride(const BitmapHandle& h);
uint32_t HandleLayoutHeight(const BitmapHandle& h);

// --- eve-raster.cpp: points, lines, rectangles, edge strips --------------------------------------

template <LineMode Mode>
void DrawPoint(LineRun& run, const Vertex& v);
template <LineMode Mode>
void DrawLine(LineRun& run, const Vertex& a, const Vertex& b);
template <LineMode Mode>
void DrawRect(LineRun& run, const Vertex& a, const Vertex& b);
template <LineMode Mode>
void DrawEdge(LineRun& run, const Vertex& a, const Vertex& b, uint8_t primitive);

// --- eve-bitmap.cpp ------------------------------------------------------------------------------

template <LineMode Mode>
void DrawBitmap(LineRun& run, const Vertex& v);
uint32_t PixelsPerClock(uint8_t format, uint8_t filter);
void InitBitmapTables();

// --- eve-pixel.cpp: scissor, alpha, stencil, blend, mask, tag -----------------------------------

// Two 8-bit values, rounding per kMultiplyRoundDiv255. Inline: it runs per channel of every
// pixel in every translation unit that shades
constexpr uint32_t kMultiplyRoundHalf = 127; // rounding term of a division by 255
constexpr uint32_t kMultiplyShift = 8;
inline uint8_t Multiply(uint32_t a, uint32_t b)
{
    if (kMultiplyRoundDiv255)
        return static_cast<uint8_t>((a * b + kMultiplyRoundHalf) / kChannelMax);
    return static_cast<uint8_t>((a * (b + 1)) >> kMultiplyShift);
}
// Alpha 0...255 of an antialiased point / line / rectangle pixel: radius and distance to
// the shape's core in 1/16 pixel (eve-aa-table.cpp, BT8XX).
uint32_t AntialiasAlpha(uint32_t radius, uint32_t distance);
constexpr uint32_t kAntialiasReach = 14; // the alpha is 0 beyond distance = radius + 14
template <LineMode Mode>
void Shade(LineRun& run, int32_t x, uint32_t r, uint32_t g, uint32_t b, uint32_t a);
// Shade a span of pixels [first, first + count) with packed RGBA texels (R in bits 31..24,
// A in 7..0): the same per-pixel rules as Shade<Draw>, the pipeline decided once per span
void ShadeSpan(LineRun& run, int32_t first, const uint32_t* rgba, uint32_t count);
// eve-accel 03: deferred catch-up (eve-dl.cpp)
bool PocWriteNeedsCatchUp(EveChip& chip, uint32_t address); // false: the write may skip CatchUp
void PocWriteDeferred(EveChip& chip);                        // a RAM_G write skipped CatchUp
void PocCatchUpDeferred(EveChip& chip);                      // draw what the reference would have drawn
void PocInvalidateReadSet(EveChip& chip);
void PocNoteRamGWrite(EveChip& chip, uint32_t address);     // 11: a RAM_G byte is about to change

// eve-accel 04c: an optional GPU backend for a batch of lines (eve-gpu-metal.mm registers
// it at start-up; without it every line is drawn on the CPU)
struct GpuHooks
{
    // Draw lines [from, to) of the current frame into the frame buffer; false (and why) when
    // the batch cannot be expressed (the CPU then draws it)
    bool (*draw)(EveChip& chip, uint32_t from, uint32_t to, const char** why);
    void (*ramGWritten)(EveChip& chip, uint32_t address); // a RAM_G byte changed
    void (*invalidate)(EveChip& chip);                     // all memory may have changed
};
extern GpuHooks g_gpu;
template <LineMode Mode>
void ClearLine(LineRun& run, uint32_t mask);
// Pixel range of the line inside the scissor rectangle and the line: [first, last).
bool ScissorSpan(const LineRun& run, int32_t& first, int32_t& last);

// --- eve-output.cpp --------------------------------------------------------------------------------

// Screen line -> logical line under REG_ROTATE; sets whether x is mirrored.
uint32_t LogicalLine(const EveChip& chip, uint32_t screenLine, bool& mirrorX);
void OutputLine(EveChip& chip, uint32_t screenLine, const uint8_t* color, uint32_t width, bool mirrorX);
bool Portrait(const EveChip& chip);           // REG_ROTATE swaps the axes
// A logical line of a portrait orientation into its screen column.
void OutputPortraitLine(EveChip& chip, uint32_t logicalLine, const uint8_t* color, uint32_t width);

} // namespace EveLib
