// eve-accel 04-08: the FT812 op list evaluated on the GPU (Metal compute) - a thin wrapper
// around the shared per-pixel core (common/eve-ops-core.h, the same code the CPU reference
// runs). The host inlines the #include below before compiling the source at run time; an
// offline build compiles it with -I common.
#include <metal_stdlib>
using namespace metal;

typedef uint EveU32;
typedef int EveI32;
typedef ulong EveU64;
typedef long EveI64;
struct EveOp;
struct EveCtx
{
    device const uchar* ram;   // RAM_G, zeros, the ROM
    constant EveOp* ops;
    device const uint* list;   // the row-band op lists
    device const uchar* aa;    // the antialiasing table
};
#define EVE_FN static inline
#define EVE_CTX EveCtx ctx,
#define EVE_PASS ctx,
#define EVE_BYTE(a) (ctx.ram[(a)])
#define EVE_AA(a) (ctx.aa[(a)])
#define EVE_OP(i) (ctx.ops[(i)])
#define EVE_LIST(k) (ctx.list[(k)])
#define EVE_SQRTF(x) sqrt(x)
#include "eve-ops-core.h"

struct Params
{
    uint width, height, band, opCount;
    uint rowBase, rowCount, pad0, pad1; // the dispatch draws rows [rowBase, rowBase + rowCount)
};

// 04a: the whole frame (or a band of rows) in one dispatch, one thread per pixel
kernel void RenderFrame(device const uchar* ram [[buffer(0)]], constant EveOp* ops [[buffer(1)]],
                        device const uint* bandStart [[buffer(2)]], device const uint* bandOps [[buffer(3)]],
                        device uint* out [[buffer(4)]], constant Params& p [[buffer(5)]],
                        device const uchar* aa [[buffer(6)]], uint2 gid [[thread_position_in_grid]])
{
    const uint row = gid.y + p.rowBase;
    if (gid.x >= p.width || row >= p.height)
        return;
    const EveCtx ctx = {ram, ops, bandOps, aa};
    const uint band = row / p.band;
    out[row * p.width + gid.x] = EveEvaluatePixel(ctx, bandStart[band], bandStart[band + 1], int(gid.x), int(row));
}

// 07: the "one kernel per primitive" model - one dispatch per op over the op's rectangle,
// the pixel state (RGBA, stencil) kept in device memory between dispatches
struct OpParams
{
    uint width, index, x0, y0;
};

kernel void ClearState(device uint* rgba [[buffer(0)]], device uchar* stencil [[buffer(1)]],
                       constant Params& p [[buffer(2)]], uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= p.width || gid.y >= p.height)
        return;
    rgba[gid.y * p.width + gid.x] = 0;
    stencil[gid.y * p.width + gid.x] = 0;
}

kernel void RenderOp(device const uchar* ram [[buffer(0)]], constant EveOp* ops [[buffer(1)]],
                     device uint* rgba [[buffer(2)]], device uchar* stencilBuffer [[buffer(3)]],
                     constant OpParams& p [[buffer(4)]], device const uchar* aa [[buffer(5)]],
                     uint2 gid [[thread_position_in_grid]])
{
    const EveCtx ctx = {ram, ops, nullptr, aa};
    const int x = int(gid.x + p.x0), y = int(gid.y + p.y0);
    if (x >= ops[p.index].x1 || y >= ops[p.index].y1)
        return;
    const uint i = uint(y) * p.width + uint(x);
    const uint v = rgba[i];
    EvePixel c;
    c.r = v >> 24;
    c.g = (v >> 16) & 255;
    c.b = (v >> 8) & 255;
    c.a = v & 255;
    c.stencil = stencilBuffer[i];
    c = EveApply(ctx, c, p.index, x, y);
    rgba[i] = (c.r << 24) | (c.g << 16) | (c.b << 8) | c.a;
    stencilBuffer[i] = uchar(c.stencil);
}

kernel void Resolve(device const uint* rgba [[buffer(0)]], device uint* out [[buffer(1)]],
                    constant Params& p [[buffer(2)]], uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= p.width || gid.y >= p.height)
        return;
    const uint v = rgba[gid.y * p.width + gid.x];
    out[gid.y * p.width + gid.x] = 0xFF000000u | (v >> 8);
}

kernel void Empty(device uint* out [[buffer(0)]], uint gid [[thread_position_in_grid]])
{
    if (gid == 0xFFFFFFFFu)
        out[0] = 0;
}

// 08: the hardware's own shape - one sequential pipeline per line (one GPU thread walks a
// whole line, pixel after pixel), lines in parallel
kernel void RenderLineSeq(device const uchar* ram [[buffer(0)]], constant EveOp* ops [[buffer(1)]],
                          device const uint* bandStart [[buffer(2)]], device const uint* bandOps [[buffer(3)]],
                          device uint* out [[buffer(4)]], constant Params& p [[buffer(5)]],
                          device const uchar* aa [[buffer(6)]], uint gid [[thread_position_in_grid]])
{
    const uint row = gid + p.rowBase;
    if (row >= p.height)
        return;
    const EveCtx ctx = {ram, ops, bandOps, aa};
    const uint band = row / p.band;
    for (uint x = 0; x < p.width; ++x)
        out[row * p.width + x] = EveEvaluatePixel(ctx, bandStart[band], bandStart[band + 1], int(x), int(row));
}

// 04b: TS-Labs' scenario (b) with sub-pixels - lines per dispatch, 256 threads per pixel (a
// threadgroup per pixel, one thread per 1/16 x 1/16 subsample). Bitmap and clear ops are
// applied by thread 0; an antialiased primitive's coverage is counted by all 256 threads
// ("honest" coverage, 06) and reduced, then thread 0 blends. Not bit-exact for primitives by
// design (the reference uses the distance table); the bitmap / clear part is exact.
static inline float Distance2(float px, float py, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax, dy = by - ay;
    const float length2 = dx * dx + dy * dy;
    float t = 0;
    if (length2 > 0)
        t = clamp(((px - ax) * dx + (py - ay) * dy) / length2, 0.0f, 1.0f);
    const float ex = px - (ax + t * dx), ey = py - (ay + t * dy);
    return ex * ex + ey * ey;
}

kernel void RenderLineSub(device const uchar* ram [[buffer(0)]], constant EveOp* ops [[buffer(1)]],
                          device const uint* bandStart [[buffer(2)]], device const uint* bandOps [[buffer(3)]],
                          device uint* out [[buffer(4)]], constant Params& p [[buffer(5)]],
                          device const uchar* aa [[buffer(6)]], uint2 group [[threadgroup_position_in_grid]],
                          uint lane [[thread_index_in_threadgroup]], uint simdLane [[thread_index_in_simdgroup]],
                          uint simdGroup [[simdgroup_index_in_threadgroup]])
{
    threadgroup uint partial[8];
    const EveCtx ctx = {ram, ops, bandOps, aa};
    const uint row = group.y + p.rowBase;
    const int x = int(group.x), y = int(row);
    const uint band = row / p.band;
    EvePixel c;
    c.r = 0;
    c.g = 0;
    c.b = 0;
    c.a = 0;
    c.stencil = 0;
    const float sx = float(x * 16 + int(lane % 16)) - 7.5f, sy = float(y * 16 + int(lane / 16)) - 7.5f;
    for (uint k = bandStart[band]; k < bandStart[band + 1]; ++k)
    {
        const uint i = bandOps[k];
        constant EveOp& op = ops[i];
        if (op.kind >= 2)
        {
            if (y < op.y0 || y >= op.y1 || x < op.x0 || x >= op.x1)
                continue;
            const float ax = float(op.kx), ay = float(op.ky), bx = float(op.a), by = float(op.b);
            const float r2 = float(op.base) * float(op.base);
            uint inside;
            if (op.kind == 4)
            {
                const float ex = sx < min(ax, bx) ? min(ax, bx) - sx : (sx > max(ax, bx) ? sx - max(ax, bx) : 0);
                const float ey = sy < min(ay, by) ? min(ay, by) - sy : (sy > max(ay, by) ? sy - max(ay, by) : 0);
                inside = ex * ex + ey * ey <= r2;
            }
            else if (op.kind == 2)
                inside = Distance2(sx, sy, bx, by, bx, by) <= r2;
            else
                inside = Distance2(sx, sy, ax, ay, bx, by) <= r2;
            const uint sum = simd_sum(inside);
            if (simdLane == 0)
                partial[simdGroup] = sum;
            threadgroup_barrier(mem_flags::mem_threadgroup);
            if (lane == 0)
            {
                uint total = 0;
                for (uint g = 0; g < 8; ++g)
                    total += partial[g];
                const uint coverage = (total * 255 + 128) / 256;
                if (coverage != 0)
                {
                    const uint a = EveMul(op.colorA, coverage), inv = 255 - a;
                    c.r = min(EveMul(op.colorRgb >> 16, a) + EveMul(c.r, inv), 255u);
                    c.g = min(EveMul((op.colorRgb >> 8) & 255, a) + EveMul(c.g, inv), 255u);
                    c.b = min(EveMul(op.colorRgb & 255, a) + EveMul(c.b, inv), 255u);
                }
            }
            threadgroup_barrier(mem_flags::mem_threadgroup);
        }
        else if (lane == 0)
            c = EveApply(ctx, c, i, x, y);
    }
    if (lane == 0)
        out[row * p.width + uint(x)] = 0xFF000000u | (c.r << 16) | (c.g << 8) | c.b;
}
