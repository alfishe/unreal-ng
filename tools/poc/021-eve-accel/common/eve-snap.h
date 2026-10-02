// eve-accel 04-07: frame snapshots and the display list flattened into an op list.
//
// A snapshot (written by the threads variant, --snap) holds what eve-emu read while it drew
// one frame in one batch: registers, the bitmap handle table at the frame's first line, the
// active display list, RAM_G, and the picture eve-emu produced.
//
// The display list runs the same way on every line (the graphics context is reset per
// line, the handle table is a fixed point after the first line), so one walk of the list
// gives the frame's whole sequence of operations: clears and bitmap draws, each with the
// state it runs with. A GPU (or the CPU reference below) then evaluates, for every pixel,
// the operations that cover it, in order - one dispatch per frame, no kernel switches.
//
// The per-pixel arithmetic mirrors eve-emu exactly (eve-bitmap.cpp, eve-pixel.cpp):
// Multiply rounds (x + 127) / 255, channels expand by bit replication, BILINEAR weights are
// rounded down per texel, REPEAT wraps with a positive modulo.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// The per-pixel core (common/eve-ops-core.h) compiled as C++: the CPU reference
typedef uint32_t EveU32;
typedef int32_t EveI32;
typedef uint64_t EveU64;
typedef int64_t EveI64;
struct EveOp;
namespace EveSnapCpu
{
inline const uint8_t* memory = nullptr; // the memory image of the frame being evaluated
inline const uint8_t* aa = nullptr;
inline const EveOp* ops = nullptr;
inline const uint32_t* list = nullptr;
} // namespace EveSnapCpu
#define EVE_FN static inline
#define EVE_BYTE(a) (EveSnapCpu::memory[(a)])
#define EVE_AA(a) (EveSnapCpu::aa[(a)])
#define EVE_OP(i) (EveSnapCpu::ops[(i)])
#define EVE_LIST(k) (EveSnapCpu::list[(k)])
#define EVE_SQRTF(x) std::sqrt(x)
#define EVE_CTX
#define EVE_PASS
#include "eve-ops-core.h"

namespace EveLib
{
uint32_t AntialiasAlpha(uint32_t radius, uint32_t distance); // eve-aa-table.cpp (link an eve-lib-*)
}

namespace EveSnap
{

constexpr uint32_t kRamGSize = 1024 * 1024;
// What a bitmap can read: RAM_G, then zeros, then the ROM (fonts) at 0x1E0000..0x300000
constexpr uint32_t kRomBase = 0x1E0000;
constexpr uint32_t kMemorySize = 0x300000;
constexpr uint32_t kDlWords = 2048;
constexpr uint32_t kRegWords = 1024;
constexpr uint32_t kHandles = 32;

// Register offsets in RAM_REG (FT812)
constexpr uint32_t kRegHsize = 0x34 / 4, kRegVsize = 0x48 / 4, kRegRotate = 0x58 / 4, kRegSwizzle = 0x64 / 4,
                   kRegCspread = 0x68 / 4, kRegMacro0 = 0xD8 / 4, kRegMacro1 = 0xDC / 4;

struct Handle
{
    uint32_t source, format, filter, wrapX, wrapY, width, height, stride, layoutHeight;
};

struct Snapshot
{
    uint32_t width = 0, height = 0, frame = 0, cpuUs = 0;
    uint32_t regs[kRegWords] = {};
    Handle handles[kHandles] = {};
    uint32_t dl[kDlWords] = {};
    std::vector<uint8_t> ramG;
    std::vector<uint32_t> picture;
    std::vector<uint8_t> memory; // kMemorySize bytes: RAM_G + ROM (BuildMemory)
};

// A GPU source with `#include "eve-ops-core.h"` replaced by the core itself (run-time
// compilers such as Metal's newLibraryWithSource do not read include files)
inline std::string InlineCore(const char* kernelPath, const char* corePath)
{
    auto read = [](const char* path) {
        std::string text;
        if (FILE* f = std::fopen(path, "rb"))
        {
            char buffer[65536];
            size_t n;
            while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0)
                text.append(buffer, n);
            std::fclose(f);
        }
        return text;
    };
    std::string kernel = read(kernelPath);
    const std::string include = "#include \"eve-ops-core.h\"";
    const size_t at = kernel.find(include);
    if (at != std::string::npos)
        kernel.replace(at, include.size(), read(corePath));
    return kernel;
}

// The ROM image (eve-replay's --rom file, 0x120000 bytes from ROM_FONT): environment EVE_ROM
inline const std::vector<uint8_t>& Rom()
{
    static std::vector<uint8_t> rom;
    static bool loaded = false;
    if (!loaded)
    {
        loaded = true;
        const char* path = std::getenv("EVE_ROM");
        if (FILE* f = path ? std::fopen(path, "rb") : nullptr)
        {
            rom.resize(kMemorySize - kRomBase);
            rom.resize(std::fread(rom.data(), 1, rom.size(), f));
            std::fclose(f);
        }
    }
    return rom;
}

inline void BuildMemory(Snapshot& s)
{
    s.memory.assign(kMemorySize, 0);
    std::memcpy(s.memory.data(), s.ramG.data(), kRamGSize);
    const std::vector<uint8_t>& rom = Rom();
    std::memcpy(s.memory.data() + kRomBase, rom.data(), rom.size());
}

inline bool Load(const std::string& path, Snapshot& s)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    char magic[8];
    uint32_t header[5];
    bool ok = std::fread(magic, 1, 8, f) == 8 && std::memcmp(magic, "EVESNAP1", 8) == 0 &&
              std::fread(header, 4, 5, f) == 5;
    if (ok)
    {
        s.width = header[0];
        s.height = header[1];
        s.frame = header[2];
        s.cpuUs = header[3];
        uint32_t raw[kHandles * 12];
        ok = std::fread(s.regs, 4, kRegWords, f) == kRegWords && std::fread(raw, 4, kHandles * 12, f) == kHandles * 12 &&
             std::fread(s.dl, 4, kDlWords, f) == kDlWords;
        for (uint32_t i = 0; i < kHandles; ++i)
        {
            const uint32_t* r = raw + i * 12;
            s.handles[i] = Handle{r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8]};
        }
        s.ramG.resize(kRamGSize);
        s.picture.resize(static_cast<size_t>(s.width) * s.height);
        ok = ok && std::fread(s.ramG.data(), 1, kRamGSize, f) == kRamGSize &&
             std::fread(s.picture.data(), 4, s.picture.size(), f) == s.picture.size();
        if (ok)
            BuildMemory(s);
    }
    std::fclose(f);
    return ok;
}

// --- The op list ---------------------------------------------------------------------------

enum OpKind : uint32_t
{
    kOpClear = 0,
    kOpBitmap = 1,
    // 07: antialiased primitives (eve-raster.cpp); the vertices in kx, ky (a) and a, b (b),
    // the radius (POINT_SIZE or LINE_WIDTH) in base
    kOpPoint = 2,
    kOpLine = 3,
    kOpRect = 4
};

// The BT8XX antialiasing table, dense (built with eve-emu's AntialiasAlpha): rows r = 1..61
// of 80 entries (d = 0..r + 14), then 29 entries for r >= 62 indexed by r + 14 - d
constexpr uint32_t kAaRow = 80, kAaEdge = 61 * kAaRow, kAaTableSize = kAaEdge + 32;
inline const uint8_t* AaTable()
{
    static const std::vector<uint8_t> table = [] {
        std::vector<uint8_t> t(kAaTableSize, 0);
        for (uint32_t r = 1; r < 62; ++r)
            for (uint32_t d = 0; d <= r + 14; ++d)
                t[(r - 1) * kAaRow + d] = static_cast<uint8_t>(EveLib::AntialiasAlpha(r, d));
        for (uint32_t i = 0; i <= 28; ++i)
            t[kAaEdge + i] = static_cast<uint8_t>(EveLib::AntialiasAlpha(100, 100 + 14 - i));
        return t;
    }();
    return table.data();
}

inline uint32_t AaAlpha(const uint8_t* table, uint32_t radius, uint32_t distance)
{
    if (radius == 0 || distance > radius + 14)
        return 0;
    if (radius < 62)
        return table[(radius - 1) * kAaRow + distance];
    if (distance + 14 <= radius)
        return 255;
    return table[kAaEdge + radius + 14 - distance];
}

// One operation with everything a pixel needs (32-bit fields only: the same layout in C++,
// Metal and GLSL). Texture coordinates of pixel (x, y): sx = kx + a (x - x0) + b y,
// sy = ky + d (x - x0) + e y, in 1/256 texel (the >> 4 of eve-emu folded into kx, ky).
// The op layout is the core's (common/eve-ops-core.h): the same in C++, Metal, CUDA, HLSL
using Op = ::EveOp;
static_assert(sizeof(Op) % 16 == 0, "Op is 16-byte aligned in a GPU buffer");

struct Flattened
{
    std::vector<Op> ops;
    bool supported = true;
    std::string reason;       // why not, when not supported
    uint32_t bitmapOps = 0, clearOps = 0, primitiveOps = 0, commands = 0;
    // Row bands of kBand rows: op indices that touch the band (ordered)
    static constexpr uint32_t kBand = 16;
    std::vector<uint32_t> bandStart; // bands + 1 entries into bandOps
    std::vector<uint32_t> bandOps;
};

namespace Detail
{

inline uint32_t Field(uint32_t w, uint32_t hi, uint32_t lo)
{
    return (w >> lo) & ((1u << (hi - lo + 1)) - 1);
}
inline int32_t SignedField(uint32_t w, uint32_t hi, uint32_t lo)
{
    const uint32_t bits = hi - lo + 1;
    return static_cast<int32_t>(Field(w, hi, lo) << (32 - bits)) >> (32 - bits);
}
inline int64_t FloorDiv(int64_t v, int64_t d)
{
    int64_t q = v / d;
    if ((v % d != 0) && ((v < 0) != (d < 0)))
        --q;
    return q;
}

struct Ctx
{
    uint32_t clearColorRgb = 0, colorRgb = 0xFFFFFF, paletteSource = 0;
    int32_t transform[6] = {256, 0, 0, 0, 256, 0};
    int32_t translateX = 0, translateY = 0;
    uint32_t scissorX = 0, scissorY = 0, scissorW = 2048, scissorH = 2048;
    uint32_t clearColorA = 0, colorA = 255, alphaFunc = 7, alphaRef = 0;
    uint32_t stencilFunc = 7, stencilRef = 0, stencilFuncMask = 255, stencilWriteMask = 255;
    uint32_t stencilFail = 1, stencilPass = 1, blendSrc = 2, blendDst = 4, clearStencil = 0, clearTag = 0;
    uint32_t tag = 255, tagMask = 1, colorMask = 15, handle = 0, cell = 0, vertexFormat = 4;
    uint32_t pointSize = 16, lineWidth = 16;
};

struct RawHandle
{
    uint32_t source = 0, format = 0, filter = 0, wrapX = 0, wrapY = 0;
    uint32_t strideLow = 0, layoutHeightLow = 0, widthLow = 0, heightLow = 0;
    uint32_t strideHigh = 0, layoutHeightHigh = 0, widthHigh = 0, heightHigh = 0;
    uint32_t Width() const { uint32_t w = widthLow | (widthHigh << 9); return w ? w : 2048; }
    uint32_t Height() const { uint32_t h = heightLow | (heightHigh << 9); return h ? h : 2048; }
    uint32_t Stride() const { return strideLow | (strideHigh << 10); }
    uint32_t LayoutHeight() const { return layoutHeightLow | (layoutHeightHigh << 9); }
};

inline RawHandle FromSnapshot(const Handle& h)
{
    RawHandle r;
    r.source = h.source;
    r.format = h.format;
    r.filter = h.filter;
    r.wrapX = h.wrapX;
    r.wrapY = h.wrapY;
    r.strideLow = h.stride & 1023;
    r.strideHigh = h.stride >> 10;
    r.layoutHeightLow = h.layoutHeight & 511;
    r.layoutHeightHigh = h.layoutHeight >> 9;
    const uint32_t w = h.width == 2048 ? 0 : h.width, hh = h.height == 2048 ? 0 : h.height;
    r.widthLow = w & 511;
    r.widthHigh = w >> 9;
    r.heightLow = hh & 511;
    r.heightHigh = hh >> 9;
    return r;
}

inline uint32_t BitsPerPixel(uint32_t format)
{
    switch (format)
    {
    case 1: return 1;
    case 17: return 2;
    case 2: return 4;
    case 0: case 6: case 7: case 10: return 16;
    default: return 8;
    }
}

inline bool GpuFormat(uint32_t f)
{
    return f <= 7 || f == 14 || f == 15 || f == 16 || f == 17;
}

// One walk of the list from the handle table `table` (changed as the list changes it)
inline void Walk(const Snapshot& s, RawHandle* table, Flattened& out)
{
    const int32_t lineWidth = static_cast<int32_t>(s.regs[kRegHsize] < 4096 ? s.regs[kRegHsize] : 4096);
    const int32_t lines = static_cast<int32_t>(s.height);
    Ctx ctx;
    Ctx saved[4];
    uint32_t savedCount = 0;
    uint32_t stack[4];
    uint32_t depth = 0;
    uint32_t primitive = 0, vertexCount = 0;
    int32_t prevX = 0, prevY = 0;
    uint32_t pc = 0;
    auto fail = [&](const std::string& why) {
        if (out.supported)
        {
            out.supported = false;
            out.reason = why;
        }
    };
    auto pipeline = [&](Op& op) {
        op.colorMask = ctx.colorMask;
        op.blendSrc = ctx.blendSrc;
        op.blendDst = ctx.blendDst;
        op.alphaFunc = ctx.alphaFunc;
        op.alphaRef = ctx.alphaRef;
        op.stencilFunc = ctx.stencilFunc;
        op.stencilRef = ctx.stencilRef;
        op.stencilFuncMask = ctx.stencilFuncMask;
        op.stencilWriteMask = ctx.stencilWriteMask;
        op.stencilFail = ctx.stencilFail;
        op.stencilPass = ctx.stencilPass;
        op.colorRgb = ctx.colorRgb;
        op.colorA = ctx.colorA;
        op.y0 = static_cast<int32_t>(ctx.scissorY);
        op.y1 = static_cast<int32_t>(ctx.scissorY + ctx.scissorH);
        if (op.y1 > lines)
            op.y1 = lines;
    };
    auto scissorX = [&](int32_t& first, int32_t& last) {
        const int32_t left = static_cast<int32_t>(ctx.scissorX), right = static_cast<int32_t>(ctx.scissorX + ctx.scissorW);
        first = first > left ? first : left;
        last = last < right ? last : right;
        if (first < 0)
            first = 0;
        if (last > lineWidth)
            last = lineWidth;
        return first < last;
    };
    auto bitmap = [&](int32_t vx, int32_t vy, uint32_t handleIndex, uint32_t cell) {
        const RawHandle& h = table[handleIndex];
        if (!GpuFormat(h.format))
        {
            fail("bitmap format " + std::to_string(h.format));
            return;
        }
        Op op{};
        op.kind = kOpBitmap;
        pipeline(op);
        const int64_t width = h.Width(), height = h.Height();
        int32_t first = static_cast<int32_t>(FloorDiv(static_cast<int64_t>(vx) + 15, 16));
        int32_t last = static_cast<int32_t>(FloorDiv(static_cast<int64_t>(vx) + width * 16 + 15, 16));
        if (!scissorX(first, last))
            return;
        // rows: 0 <= y * 16 - vy < height * 16
        const int32_t rowFirst = static_cast<int32_t>(FloorDiv(static_cast<int64_t>(vy) + 15, 16));
        const int32_t rowLast = static_cast<int32_t>(FloorDiv(static_cast<int64_t>(vy) + height * 16 + 15, 16));
        op.y0 = op.y0 > rowFirst ? op.y0 : rowFirst;
        op.y1 = op.y1 < rowLast ? op.y1 : rowLast;
        if (op.y0 < 0)
            op.y0 = 0;
        if (op.y0 >= op.y1)
            return;
        op.x0 = first;
        op.x1 = last;
        op.vy = vy;
        op.height16 = static_cast<int32_t>(height * 16);
        const uint32_t stride = h.Stride();
        const uint32_t layoutHeight = h.LayoutHeight();
        const uint32_t bpp = BitsPerPixel(h.format);
        op.stride = stride;
        op.layoutHeight = layoutHeight;
        op.layoutWidth = stride * 8 / bpp;
        op.base = h.source + cell * stride * layoutHeight;
        op.format = h.format;
        op.filterMode = h.filter;
        op.wrapX = h.wrapX;
        op.wrapY = h.wrapY;
        op.paletteSource = ctx.paletteSource;
        // Everything the op reads must lie in RAM_G or the ROM (no address wrap)
        const uint64_t end = static_cast<uint64_t>(op.base) + static_cast<uint64_t>(stride) * layoutHeight;
        const uint64_t paletteEnd = static_cast<uint64_t>(op.paletteSource) + 1024;
        if (end > kMemorySize || (op.base < kRomBase && end > kRamGSize) || paletteEnd > kMemorySize)
        {
            fail("bitmap outside RAM_G and ROM");
            return;
        }
        if (op.base >= kRomBase && Rom().empty())
        {
            fail("bitmap in ROM, no ROM image (EVE_ROM)");
            return;
        }
        const int64_t a = ctx.transform[0], b = ctx.transform[1], c = ctx.transform[2];
        const int64_t d = ctx.transform[3], e = ctx.transform[4], f = ctx.transform[5];
        const int64_t relFirst = static_cast<int64_t>(first) * 16 - vx;
        // eve-emu: sx = ((a relX + b rely) >> 4) + c, relX = 16 x - vx, rely = 16 y - vy
        const int64_t kx = ((a * relFirst - b * vy) >> 4) + c;
        const int64_t ky = ((d * relFirst - e * vy) >> 4) + f;
        // The 32-bit per-pixel form must not overflow anywhere in the op's rectangle
        const int64_t dxMax = last - first - 1;
        for (int64_t cx : {int64_t{0}, dxMax})
            for (int64_t cy : {int64_t{op.y0}, int64_t{op.y1 - 1}})
            {
                const int64_t sx = kx + a * cx + b * cy, sy = ky + d * cx + e * cy;
                if (sx < INT32_MIN / 2 || sx > INT32_MAX / 2 || sy < INT32_MIN / 2 || sy > INT32_MAX / 2)
                {
                    fail("texture coordinates beyond 32 bits");
                    return;
                }
            }
        if (kx < INT32_MIN || kx > INT32_MAX || ky < INT32_MIN || ky > INT32_MAX)
        {
            fail("texture coordinates beyond 32 bits");
            return;
        }
        op.kx = static_cast<int32_t>(kx);
        op.ky = static_cast<int32_t>(ky);
        op.a = static_cast<int32_t>(a);
        op.b = static_cast<int32_t>(b);
        op.d = static_cast<int32_t>(d);
        op.e = static_cast<int32_t>(e);
        out.ops.push_back(op);
        ++out.bitmapOps;
    };

    for (uint32_t executed = 0; executed < 8 * 2048 && pc < kDlWords; ++executed)
    {
        uint32_t word = s.dl[pc];
        ++out.commands;
        uint32_t target = pc + 1;
        bool jump = false, stop = false;
        for (int macroDepth = 0; macroDepth < 2; ++macroDepth)
        {
            const uint32_t kind = word >> 30;
            if (kind == 1 || kind == 2)
            {
                int32_t x, y;
                uint32_t handle, cell;
                if (kind == 1)
                {
                    const uint32_t frac = ctx.vertexFormat < 4 ? ctx.vertexFormat : 4;
                    x = SignedField(word, 29, 15) * (1 << (4 - frac));
                    y = SignedField(word, 14, 0) * (1 << (4 - frac));
                    handle = ctx.handle;
                    cell = ctx.cell;
                }
                else
                {
                    x = static_cast<int32_t>(Field(word, 29, 21) * 16);
                    y = static_cast<int32_t>(Field(word, 20, 12) * 16);
                    handle = Field(word, 11, 7);
                    cell = Field(word, 6, 0);
                }
                x += ctx.translateX;
                y += ctx.translateY;
                if (primitive == 1)
                    bitmap(x, y, handle, cell);
                else if (primitive >= 2 && primitive <= 9)
                {
                    const bool draws = primitive == 2 || ((primitive == 3 || primitive == 9) && (vertexCount & 1)) ||
                                       (primitive >= 4 && primitive <= 8 && vertexCount > 0);
                    if (draws && primitive >= 5 && primitive <= 8)
                        fail("edge strip");
                    else if (draws)
                    {
                        Op op{};
                        op.kind = primitive == 2 ? kOpPoint : primitive == 9 ? kOpRect : kOpLine;
                        pipeline(op);
                        int32_t first = 0, last = lineWidth;
                        if (scissorX(first, last) && op.y0 < op.y1)
                        {
                            op.x0 = first;
                            op.x1 = last;
                            if (op.y0 < 0)
                                op.y0 = 0;
                            op.kx = primitive == 2 ? x : prevX;
                            op.ky = primitive == 2 ? y : prevY;
                            op.a = x;
                            op.b = y;
                            op.base = primitive == 2 ? ctx.pointSize : ctx.lineWidth;
                            // rows the shape can reach: radius + the table's 14
                            const int32_t reach = static_cast<int32_t>(op.base) + 16;
                            const int32_t top = static_cast<int32_t>(FloorDiv(std::min(op.ky, op.b) - reach, 16));
                            const int32_t bottom = static_cast<int32_t>(FloorDiv(std::max(op.ky, op.b) + reach, 16)) + 1;
                            op.y0 = std::max(op.y0, top);
                            op.y1 = std::min(op.y1, bottom);
                            if (op.y0 < op.y1)
                            {
                                out.ops.push_back(op);
                                ++out.primitiveOps;
                            }
                        }
                    }
                }
                prevX = x;
                prevY = y;
                ++vertexCount;
                break;
            }
            RawHandle& h = table[ctx.handle];
            switch (word >> 24)
            {
            case 0x00: stop = true; break;
            case 0x01: h.source = Field(word, 21, 0); break;
            case 0x02: ctx.clearColorRgb = Field(word, 23, 0); break;
            case 0x03: ctx.tag = Field(word, 7, 0); break;
            case 0x04: ctx.colorRgb = Field(word, 23, 0); break;
            case 0x05: ctx.handle = Field(word, 4, 0); break;
            case 0x06: ctx.cell = Field(word, 6, 0); break;
            case 0x07:
                h.format = Field(word, 23, 19);
                h.strideLow = Field(word, 18, 9);
                h.layoutHeightLow = Field(word, 8, 0);
                break;
            case 0x08:
                h.filter = Field(word, 20, 20);
                h.wrapX = Field(word, 19, 19);
                h.wrapY = Field(word, 18, 18);
                h.widthLow = Field(word, 17, 9);
                h.heightLow = Field(word, 8, 0);
                break;
            case 0x09: ctx.alphaFunc = Field(word, 10, 8); ctx.alphaRef = Field(word, 7, 0); break;
            case 0x0A:
                ctx.stencilFunc = Field(word, 19, 16);
                ctx.stencilRef = Field(word, 15, 8);
                ctx.stencilFuncMask = Field(word, 7, 0);
                break;
            case 0x0B: ctx.blendSrc = Field(word, 5, 3); ctx.blendDst = Field(word, 2, 0); break;
            case 0x0C: ctx.stencilFail = Field(word, 5, 3); ctx.stencilPass = Field(word, 2, 0); break;
            case 0x0D: ctx.pointSize = Field(word, 12, 0); break;
            case 0x0E: ctx.lineWidth = Field(word, 11, 0); break;
            case 0x0F: ctx.clearColorA = Field(word, 7, 0); break;
            case 0x10: ctx.colorA = Field(word, 7, 0); break;
            case 0x11: ctx.clearStencil = Field(word, 7, 0); break;
            case 0x12: ctx.clearTag = Field(word, 7, 0); break;
            case 0x13: ctx.stencilWriteMask = Field(word, 7, 0); break;
            case 0x14: ctx.tagMask = Field(word, 0, 0); break;
            case 0x15: ctx.transform[0] = SignedField(word, 16, 0); break;
            case 0x16: ctx.transform[1] = SignedField(word, 16, 0); break;
            case 0x17: ctx.transform[2] = SignedField(word, 23, 0); break;
            case 0x18: ctx.transform[3] = SignedField(word, 16, 0); break;
            case 0x19: ctx.transform[4] = SignedField(word, 16, 0); break;
            case 0x1A: ctx.transform[5] = SignedField(word, 23, 0); break;
            case 0x1B: ctx.scissorX = Field(word, 21, 11); ctx.scissorY = Field(word, 10, 0); break;
            case 0x1C: ctx.scissorW = Field(word, 23, 12); ctx.scissorH = Field(word, 11, 0); break;
            case 0x1D:
                if (depth == 4)
                {
                    stop = true;
                    break;
                }
                stack[depth++] = pc + 1;
                target = Field(word, 15, 0);
                jump = true;
                break;
            case 0x1E: target = Field(word, 15, 0); jump = true; break;
            case 0x1F: primitive = Field(word, 3, 0); vertexCount = 0; break;
            case 0x20: ctx.colorMask = Field(word, 3, 0); break;
            case 0x21: primitive = 0; vertexCount = 0; break;
            case 0x22:
                if (savedCount == 4)
                {
                    for (uint32_t i = 1; i < 4; ++i)
                        saved[i - 1] = saved[i];
                    --savedCount;
                }
                saved[savedCount++] = ctx;
                break;
            case 0x23:
                if (savedCount == 0)
                    ctx = Ctx{};
                else
                    ctx = saved[--savedCount];
                break;
            case 0x24:
                if (depth == 0)
                {
                    stop = true;
                    break;
                }
                target = stack[--depth];
                jump = true;
                break;
            case 0x25:
            {
                const uint32_t macro = s.regs[Field(word, 0, 0) ? kRegMacro1 : kRegMacro0];
                if ((macro >> 24) == 0x25)
                    break;
                word = macro;
                continue; // execute the macro word in place
            }
            case 0x26:
            {
                Op op{};
                op.kind = kOpClear;
                pipeline(op);
                int32_t first = 0, last = lineWidth;
                if (!scissorX(first, last))
                    break;
                if (op.y0 >= op.y1)
                    break;
                op.x0 = first;
                op.x1 = last;
                op.clearMask = Field(word, 2, 0);
                op.clearRgba = (ctx.clearColorRgb << 8) | ctx.clearColorA;
                op.clearStencil = ctx.clearStencil;
                out.ops.push_back(op);
                ++out.clearOps;
                break;
            }
            case 0x27: ctx.vertexFormat = Field(word, 2, 0); break;
            case 0x28: h.strideHigh = Field(word, 3, 2); h.layoutHeightHigh = Field(word, 1, 0); break;
            case 0x29: h.widthHigh = Field(word, 3, 2); h.heightHigh = Field(word, 1, 0); break;
            case 0x2A: ctx.paletteSource = Field(word, 21, 0); break;
            case 0x2B: ctx.translateX = SignedField(word, 16, 0); break;
            case 0x2C: ctx.translateY = SignedField(word, 16, 0); break;
            default: break;
            }
            break;
        }
        if (stop)
            break;
        pc = jump ? target : pc + 1;
    }
}

} // namespace Detail

// The frame as an op list, or supported = false with the reason.
inline Flattened Flatten(const Snapshot& s)
{
    Flattened out;
    if (s.regs[kRegRotate] & 7)
        out.reason = "REG_ROTATE";
    else if (s.regs[kRegSwizzle] & 15)
        out.reason = "REG_SWIZZLE";
    else if (s.regs[kRegCspread] & 1)
        out.reason = "REG_CSPREAD";
    if (!out.reason.empty())
    {
        out.supported = false;
        return out;
    }
    Detail::RawHandle table[kHandles];
    for (uint32_t i = 0; i < kHandles; ++i)
        table[i] = Detail::FromSnapshot(s.handles[i]);
    // Line 0 runs from the snapshot's table; every later line from the table line 0 left.
    Flattened first;
    Detail::Walk(s, table, first);
    Detail::RawHandle after[kHandles];
    std::memcpy(after, table, sizeof(after));
    Detail::Walk(s, table, out);
    if (std::memcmp(after, table, sizeof(after)) != 0)
    {
        out.supported = false;
        out.reason = "handle table not a fixed point";
    }
    else if (first.ops.size() != out.ops.size() ||
             std::memcmp(first.ops.data(), out.ops.data(), first.ops.size() * sizeof(Op)) != 0)
    {
        out.supported = false;
        out.reason = "first line differs (handle table)";
    }
    // Row bands
    const uint32_t bands = (s.height + Flattened::kBand - 1) / Flattened::kBand;
    out.bandStart.assign(bands + 1, 0);
    for (uint32_t band = 0; band < bands; ++band)
    {
        out.bandStart[band] = static_cast<uint32_t>(out.bandOps.size());
        const int32_t top = static_cast<int32_t>(band * Flattened::kBand);
        const int32_t bottom = top + static_cast<int32_t>(Flattened::kBand);
        for (uint32_t i = 0; i < out.ops.size(); ++i)
            if (out.ops[i].y0 < bottom && out.ops[i].y1 > top)
                out.bandOps.push_back(i);
    }
    out.bandStart[bands] = static_cast<uint32_t>(out.bandOps.size());
    return out;
}

// --- The CPU reference: the shared per-pixel core on one core ---------------------------------

// The whole frame on the CPU, one pixel at a time (SIMD-CANDIDATE(op loop over 4 pixels))
inline void RenderCpu(const Snapshot& s, const Flattened& f, std::vector<uint32_t>& out)
{
    EveSnapCpu::memory = s.memory.data();
    EveSnapCpu::aa = AaTable();
    EveSnapCpu::ops = f.ops.data();
    EveSnapCpu::list = f.bandOps.data();
    out.assign(static_cast<size_t>(s.width) * s.height, 0);
    for (uint32_t y = 0; y < s.height; ++y)
    {
        const uint32_t band = y / Flattened::kBand;
        for (uint32_t x = 0; x < s.width; ++x)
            out[static_cast<size_t>(y) * s.width + x] =
                EveEvaluatePixel(f.bandStart[band], f.bandStart[band + 1], static_cast<int32_t>(x), static_cast<int32_t>(y));
    }
}

} // namespace EveSnap
