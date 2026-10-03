// eve-emu - inspection (arch §4.6). Nothing here changes chip state.
#include "eve-internal.h"

#include <cstdarg>
#include <cstdio>

namespace EveLib
{

namespace
{

int Print(char* text, size_t size, const char* format, ...)
{
    if (text == nullptr || size == 0)
        return 0;
    va_list args;
    va_start(args, format);
    const int n = std::vsnprintf(text, size, format, args);
    va_end(args);
    if (n < 0)
    {
        text[0] = '\0';
        return 0;
    }
    return static_cast<size_t>(n) >= size ? static_cast<int>(size - 1) : n;
}

int32_t SignExtend(uint32_t value, uint32_t bits)
{
    const uint32_t shift = 32 - bits;
    return static_cast<int32_t>(value << shift) >> shift;
}

uint32_t Bits(uint32_t word, uint32_t high, uint32_t low)
{
    return (word >> low) & ((1u << (high - low + 1)) - 1);
}

const char* const kPrimitives[] = {"0", "BITMAPS", "POINTS", "LINES", "LINE_STRIP", "EDGE_STRIP_R",
                                   "EDGE_STRIP_L", "EDGE_STRIP_A", "EDGE_STRIP_B", "RECTS"};
const char* const kFuncs[] = {"NEVER", "LESS", "LEQUAL", "GREATER", "GEQUAL", "EQUAL", "NOTEQUAL", "ALWAYS"};
const char* const kBlends[] = {"ZERO", "ONE", "SRC_ALPHA", "DST_ALPHA", "ONE_MINUS_SRC_ALPHA", "ONE_MINUS_DST_ALPHA", "6", "7"};
const char* const kStencilOps[] = {"ZERO", "KEEP", "REPLACE", "INCR", "DECR", "INVERT", "6", "7"};

const char* FormatName(uint32_t format)
{
    switch (format)
    {
    case 0: return "ARGB1555";
    case 1: return "L1";
    case 2: return "L4";
    case 3: return "L8";
    case 4: return "RGB332";
    case 5: return "ARGB2";
    case 6: return "ARGB4";
    case 7: return "RGB565";
    case 9: return "TEXT8X8";
    case 10: return "TEXTVGA";
    case 11: return "BARGRAPH";
    case 14: return "PALETTED565";
    case 15: return "PALETTED4444";
    case 16: return "PALETTED8";
    case 17: return "L2";
    default: return nullptr;
    }
}

int Disassemble(uint32_t word, char* text, size_t size)
{
    if ((word >> 30) == 1)
        return Print(text, size, "VERTEX2F(%d, %d)", SignExtend(Bits(word, 29, 15), 15), SignExtend(Bits(word, 14, 0), 15));
    if ((word >> 30) == 2)
        return Print(text, size, "VERTEX2II(%u, %u, %u, %u)", Bits(word, 29, 21), Bits(word, 20, 12), Bits(word, 11, 7),
                     Bits(word, 6, 0));
    const uint32_t opcode = word >> 24;
    switch (opcode)
    {
    case 0x00: return Print(text, size, "DISPLAY()");
    case 0x01: return Print(text, size, "BITMAP_SOURCE(0x%06X)", Bits(word, 21, 0));
    case 0x02: return Print(text, size, "CLEAR_COLOR_RGB(%u, %u, %u)", Bits(word, 23, 16), Bits(word, 15, 8), Bits(word, 7, 0));
    case 0x03: return Print(text, size, "TAG(%u)", Bits(word, 7, 0));
    case 0x04: return Print(text, size, "COLOR_RGB(%u, %u, %u)", Bits(word, 23, 16), Bits(word, 15, 8), Bits(word, 7, 0));
    case 0x05: return Print(text, size, "BITMAP_HANDLE(%u)", Bits(word, 4, 0));
    case 0x06: return Print(text, size, "CELL(%u)", Bits(word, 6, 0));
    case 0x07:
    {
        const char* name = FormatName(Bits(word, 23, 19));
        if (name != nullptr)
            return Print(text, size, "BITMAP_LAYOUT(%s, %u, %u)", name, Bits(word, 18, 9), Bits(word, 8, 0));
        return Print(text, size, "BITMAP_LAYOUT(%u, %u, %u)", Bits(word, 23, 19), Bits(word, 18, 9), Bits(word, 8, 0));
    }
    case 0x08:
        return Print(text, size, "BITMAP_SIZE(%s, %s, %s, %u, %u)", Bits(word, 20, 20) ? "BILINEAR" : "NEAREST",
                     Bits(word, 19, 19) ? "REPEAT" : "BORDER", Bits(word, 18, 18) ? "REPEAT" : "BORDER", Bits(word, 17, 9),
                     Bits(word, 8, 0));
    case 0x09: return Print(text, size, "ALPHA_FUNC(%s, %u)", kFuncs[Bits(word, 10, 8)], Bits(word, 7, 0));
    case 0x0A:
        return Print(text, size, "STENCIL_FUNC(%s, %u, %u)", Bits(word, 19, 16) < 8 ? kFuncs[Bits(word, 19, 16)] : "?",
                     Bits(word, 15, 8), Bits(word, 7, 0));
    case 0x0B: return Print(text, size, "BLEND_FUNC(%s, %s)", kBlends[Bits(word, 5, 3)], kBlends[Bits(word, 2, 0)]);
    case 0x0C: return Print(text, size, "STENCIL_OP(%s, %s)", kStencilOps[Bits(word, 5, 3)], kStencilOps[Bits(word, 2, 0)]);
    case 0x0D: return Print(text, size, "POINT_SIZE(%u)", Bits(word, 12, 0));
    case 0x0E: return Print(text, size, "LINE_WIDTH(%u)", Bits(word, 11, 0));
    case 0x0F: return Print(text, size, "CLEAR_COLOR_A(%u)", Bits(word, 7, 0));
    case 0x10: return Print(text, size, "COLOR_A(%u)", Bits(word, 7, 0));
    case 0x11: return Print(text, size, "CLEAR_STENCIL(%u)", Bits(word, 7, 0));
    case 0x12: return Print(text, size, "CLEAR_TAG(%u)", Bits(word, 7, 0));
    case 0x13: return Print(text, size, "STENCIL_MASK(%u)", Bits(word, 7, 0));
    case 0x14: return Print(text, size, "TAG_MASK(%u)", Bits(word, 0, 0));
    case 0x15: return Print(text, size, "BITMAP_TRANSFORM_A(%d)", SignExtend(Bits(word, 16, 0), 17));
    case 0x16: return Print(text, size, "BITMAP_TRANSFORM_B(%d)", SignExtend(Bits(word, 16, 0), 17));
    case 0x17: return Print(text, size, "BITMAP_TRANSFORM_C(%d)", SignExtend(Bits(word, 23, 0), 24));
    case 0x18: return Print(text, size, "BITMAP_TRANSFORM_D(%d)", SignExtend(Bits(word, 16, 0), 17));
    case 0x19: return Print(text, size, "BITMAP_TRANSFORM_E(%d)", SignExtend(Bits(word, 16, 0), 17));
    case 0x1A: return Print(text, size, "BITMAP_TRANSFORM_F(%d)", SignExtend(Bits(word, 23, 0), 24));
    case 0x1B: return Print(text, size, "SCISSOR_XY(%u, %u)", Bits(word, 21, 11), Bits(word, 10, 0));
    case 0x1C: return Print(text, size, "SCISSOR_SIZE(%u, %u)", Bits(word, 23, 12), Bits(word, 11, 0));
    case 0x1D: return Print(text, size, "CALL(%u)", Bits(word, 15, 0));
    case 0x1E: return Print(text, size, "JUMP(%u)", Bits(word, 15, 0));
    case 0x1F:
        return Bits(word, 3, 0) <= 9 ? Print(text, size, "BEGIN(%s)", kPrimitives[Bits(word, 3, 0)])
                                     : Print(text, size, "BEGIN(%u)", Bits(word, 3, 0));
    case 0x20:
        return Print(text, size, "COLOR_MASK(%u, %u, %u, %u)", Bits(word, 3, 3), Bits(word, 2, 2), Bits(word, 1, 1),
                     Bits(word, 0, 0));
    case 0x21: return Print(text, size, "END()");
    case 0x22: return Print(text, size, "SAVE_CONTEXT()");
    case 0x23: return Print(text, size, "RESTORE_CONTEXT()");
    case 0x24: return Print(text, size, "RETURN()");
    case 0x25: return Print(text, size, "MACRO(%u)", Bits(word, 0, 0));
    case 0x26: return Print(text, size, "CLEAR(%u, %u, %u)", Bits(word, 2, 2), Bits(word, 1, 1), Bits(word, 0, 0));
    case 0x27: return Print(text, size, "VERTEX_FORMAT(%u)", Bits(word, 2, 0));
    case 0x28: return Print(text, size, "BITMAP_LAYOUT_H(%u, %u)", Bits(word, 3, 2), Bits(word, 1, 0));
    case 0x29: return Print(text, size, "BITMAP_SIZE_H(%u, %u)", Bits(word, 3, 2), Bits(word, 1, 0));
    case 0x2A: return Print(text, size, "PALETTE_SOURCE(0x%06X)", Bits(word, 21, 0));
    case 0x2B: return Print(text, size, "VERTEX_TRANSLATE_X(%d)", SignExtend(Bits(word, 16, 0), 17));
    case 0x2C: return Print(text, size, "VERTEX_TRANSLATE_Y(%d)", SignExtend(Bits(word, 16, 0), 17));
    case 0x2D: return Print(text, size, "NOP()");
    default: return Print(text, size, "UNKNOWN(0x%08X)", word);
    }
}

} // namespace

} // namespace EveLib

using namespace EveLib;

extern "C" {

uint8_t EvePeek(const EveChip* chip, uint32_t address)
{
    return BusPeek(*chip, address);
}

size_t EveGetDisplayList(const EveChip* chip, int active, uint32_t* words, size_t max)
{
    const uint8_t* list = active ? ActiveDl(*chip) : chip->regions[RegionDl0 + (chip->state.scan.activeDl ^ 1)].base;
    const size_t count = max < kDlWords ? max : kDlWords;
    for (size_t i = 0; i < count; ++i)
        words[i] = LoadLe32(list + 4 * i);
    return count;
}

int EveDisassemble(uint32_t word, char* text, size_t size)
{
    return Disassemble(word, text, size);
}

void EveGetCoprocessor(const EveChip* chip, EveCoproView* out)
{
    *out = EveCoproView{};
    out->faultReason = "";
    out->readPtr = RegGet(*chip, Reg::CmdRead);
    out->writePtr = RegGet(*chip, Reg::CmdWrite);
    out->cmdDl = RegGet(*chip, Reg::CmdDl);
    GetCoproView(*chip, *out);
}

void EveGetLineCost(const EveChip* chip, uint32_t line, EveLineCost* out)
{
    *out = EveLineCost{};
    out->line = line;
    GetLineCost(*chip, line, *out);
}

size_t EveGetFrameMetrics(const EveChip* chip, EveFrameMetrics* out, uint16_t* lineClocks, size_t maxLines)
{
    const FrameMetricsState& m = chip->state.metrics;
    if (out)
    {
        *out = EveFrameMetrics{};
        out->frame = m.frame;
        out->valid = m.valid;
        out->lines = m.lines;
        out->hardBudget = m.hardBudget;
        out->softBudget = m.softBudget;
        out->worstLine = m.worstLine;
        out->worstClocks = m.worstClocks;
        out->totalClocks = m.totalClocks;
        out->linesOverSoft = m.linesOverSoft;
        out->linesOverHard = m.linesOverHard;
    }
    if (!lineClocks)
        return 0;
    const size_t count = maxLines < m.lines ? maxLines : m.lines;
    std::memcpy(lineClocks, m.lineClocks, count * sizeof(uint16_t));
    return count;
}

uint32_t EveFrameLinesPassed(const EveChip* chip)
{
    return FrameLinesDue(*chip);
}

void EveSetLineBudgetMargin(EveChip* chip, uint32_t percent)
{
    chip->lineBudgetMargin = percent > 50 ? 50 : percent;
}

int EveProbePixel(const EveChip* chip, uint32_t x, uint32_t y, EvePixelSource* out)
{
    *out = EvePixelSource{};
    return ProbePixel(*chip, x, y, *out) ? 1 : 0;
}

} // extern "C"
