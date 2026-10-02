// eve-accel: the per-pixel evaluation of the FT812 op list, in a plain C-like subset that
// compiles unchanged as C++ (the CPU reference), Metal Shading Language, CUDA C++ and HLSL:
// scalars, structs passed and returned by value, if / switch / for, C casts. No pointers,
// references, templates or vectors. Every backend is a thin wrapper that defines the
// contract below and calls EveEvaluatePixel for each pixel.
//
// The arithmetic mirrors eve-emu (eve-bitmap.cpp, eve-pixel.cpp, eve-raster.cpp) exactly:
// Multiply rounds (x + 127) / 255, channels expand by bit replication, BILINEAR weights are
// rounded down per texel, REPEAT wraps with a positive modulo, antialiasing comes from the
// BT8XX distance table with the line foot rounded down to 1/16 pixel.
//
// Contract - define before including:
//   EVE_FN            qualifiers of every function (C++/MSL: static inline; CUDA: __device__ static inline)
//   EveU32, EveI32    32-bit unsigned / signed integer types
//   EveU64, EveI64    64-bit types (antialiased lines and points only)
//   EVE_BYTE(a)       byte at address a of the memory image (RAM_G, zeros, the ROM at 0x1E0000)
//   EVE_AA(a)         byte a of the antialiasing table (EveSnap::AaTable layout)
//   EVE_OP(i)         op number i (an EveOp)
//   EVE_LIST(k)       entry k of the row-band op lists (an op number)
//   EVE_SQRTF(x)      float square root (a first guess only: corrected in integers)
//   EVE_CTX, EVE_PASS how the data reaches the functions: empty where the buffers are
//                     globals (C++ reference, HLSL, CUDA __constant__/globals); in Metal a
//                     context struct of buffer pointers: "EveCtx ctx," and "ctx,"
#ifndef EVE_OPS_CORE_H
#define EVE_OPS_CORE_H

struct EveOp
{
    EveU32 kind;              // 0 clear, 1 bitmap, 2 point, 3 line, 4 rectangle
    EveI32 x0, x1, y0, y1;    // the pixels it may touch: [x0, x1) x [y0, y1)
    EveU32 colorMask, blendSrc, blendDst, alphaFunc, alphaRef;
    EveU32 stencilFunc, stencilRef, stencilFuncMask, stencilWriteMask, stencilFail, stencilPass;
    EveU32 colorRgb, colorA;
    EveU32 clearMask, clearRgba, clearStencil;
    EveI32 vy, height16;      // bitmap rows: 0 <= 16 y - vy < height16
    EveI32 kx, ky, a, b, d, e; // bitmap: sx = kx + a (x - x0) + b y, sy = ky + d (x - x0) + e y;
                              // primitives: vertices (kx, ky) and (a, b), 1/16 pixel
    EveU32 base, format, filterMode, wrapX, wrapY, stride, layoutWidth, layoutHeight, paletteSource;
                              // primitives: base = radius (POINT_SIZE / LINE_WIDTH)
    EveU32 pad0, pad1, pad2, pad3, pad4, pad5;
};

// What a pixel holds while the ops of its line run
struct EvePixel
{
    EveU32 r, g, b, a, stencil;
};

// A source color (after COLOR_RGB / COLOR_A); valid = false: the op does not touch the pixel
struct EveSource
{
    EveU32 r, g, b, a;
    bool valid;
};

EVE_FN EveU32 EveMul(EveU32 a, EveU32 b)
{
    const EveU32 t = a * b + 127u;
    return (t + 1u + (t >> 8)) >> 8; // == t / 255 for every t the blend produces (<= 65152)
}

EVE_FN EveU32 EveExpand(EveU32 v, EveU32 bits)
{
    const EveI32 shift = 8 - (EveI32)bits;
    EveU32 out = 0u;
    for (EveI32 pos = shift; pos > -(EveI32)bits; pos -= (EveI32)bits)
        out |= pos >= 0 ? v << (EveU32)pos : v >> (EveU32)(-pos);
    return out & 255u;
}

EVE_FN EveU32 EvePack(EveU32 r, EveU32 g, EveU32 b, EveU32 a)
{
    return (r << 24) | (g << 16) | (b << 8) | a;
}

EVE_FN EveU32 EveDirect(EveU32 p, EveU32 rs, EveU32 rb, EveU32 gs, EveU32 gb, EveU32 bs, EveU32 bb, EveU32 as,
                        EveU32 ab)
{
    const EveU32 r = EveExpand((p >> rs) & ((1u << rb) - 1u), rb);
    const EveU32 g = EveExpand((p >> gs) & ((1u << gb) - 1u), gb);
    const EveU32 b = EveExpand((p >> bs) & ((1u << bb) - 1u), bb);
    const EveU32 a = ab != 0u ? EveExpand((p >> as) & ((1u << ab) - 1u), ab) : 255u;
    return EvePack(r, g, b, a);
}

EVE_FN EveU32 EveRead16(EVE_CTX EveU32 a)
{
    return (EveU32)EVE_BYTE(a) | ((EveU32)EVE_BYTE(a + 1u) << 8);
}

// Texel (tx, ty) of op i's bitmap, packed R << 24 | G << 16 | B << 8 | A
EVE_FN EveU32 EveTexel(EVE_CTX EveU32 i, EveI32 tx, EveI32 ty)
{
    const EveU32 x = (EveU32)tx, y = (EveU32)ty;
    const EveU32 row = EVE_OP(i).base + y * EVE_OP(i).stride;
    const EveU32 pal = EVE_OP(i).paletteSource;
    switch (EVE_OP(i).format)
    {
    case 0u: return EveDirect(EveRead16(EVE_PASS row + 2u * x), 10u, 5u, 5u, 5u, 0u, 5u, 15u, 1u);
    case 1u: return EvePack(255u, 255u, 255u, EveExpand(((EveU32)EVE_BYTE(row + x / 8u) >> (7u - x % 8u)) & 1u, 1u));
    case 17u: return EvePack(255u, 255u, 255u, EveExpand(((EveU32)EVE_BYTE(row + x / 4u) >> (6u - 2u * (x % 4u))) & 3u, 2u));
    case 2u: return EvePack(255u, 255u, 255u, EveExpand(((EveU32)EVE_BYTE(row + x / 2u) >> (4u - 4u * (x % 2u))) & 15u, 4u));
    case 3u: return EvePack(255u, 255u, 255u, (EveU32)EVE_BYTE(row + x));
    case 4u: return EveDirect((EveU32)EVE_BYTE(row + x), 5u, 3u, 2u, 3u, 0u, 2u, 0u, 0u);
    case 5u: return EveDirect((EveU32)EVE_BYTE(row + x), 4u, 2u, 2u, 2u, 0u, 2u, 6u, 2u);
    case 6u: return EveDirect(EveRead16(EVE_PASS row + 2u * x), 8u, 4u, 4u, 4u, 0u, 4u, 12u, 4u);
    case 7u: return EveDirect(EveRead16(EVE_PASS row + 2u * x), 11u, 5u, 5u, 6u, 0u, 5u, 0u, 0u);
    case 14u: return EveDirect(EveRead16(EVE_PASS pal + 2u * (EveU32)EVE_BYTE(row + x)), 11u, 5u, 5u, 6u, 0u, 5u, 0u, 0u);
    case 15u: return EveDirect(EveRead16(EVE_PASS pal + 2u * (EveU32)EVE_BYTE(row + x)), 8u, 4u, 4u, 4u, 0u, 4u, 12u, 4u);
    case 16u:
    {
        const EveU32 v = (EveU32)EVE_BYTE(pal + 4u * (EveU32)EVE_BYTE(row + x));
        return EvePack(v, v, v, v);
    }
    default: return 0u;
    }
}

EVE_FN EveI32 EveWrap(EveI32 t, EveI32 n)
{
    // positive modulo without a negative operand (C and HLSL differ on negative %)
    return t >= 0 ? t % n : n - 1 - ((-t - 1) % n);
}

EVE_FN EveU32 EveWrapped(EVE_CTX EveU32 i, EveI32 tx, EveI32 ty)
{
    const EveI32 w = (EveI32)EVE_OP(i).layoutWidth, h = (EveI32)EVE_OP(i).layoutHeight;
    if (w == 0 || h == 0)
        return 0u;
    if (EVE_OP(i).wrapX != 0u)
        tx = EveWrap(tx, w);
    else if (tx < 0 || tx >= w)
        return 0u;
    if (EVE_OP(i).wrapY != 0u)
        ty = EveWrap(ty, h);
    else if (ty < 0 || ty >= h)
        return 0u;
    return EveTexel(EVE_PASS i, tx, ty);
}

EVE_FN bool EveCompare(EveU32 func, EveU32 v, EveU32 ref)
{
    switch (func)
    {
    case 0u: return false;
    case 1u: return v < ref;
    case 2u: return v <= ref;
    case 3u: return v > ref;
    case 4u: return v >= ref;
    case 5u: return v == ref;
    case 6u: return v != ref;
    default: return true;
    }
}

EVE_FN EveU32 EveStencilOp(EveU32 o, EveU32 v, EveU32 ref)
{
    switch (o)
    {
    case 0u: return 0u;
    case 2u: return ref;
    case 3u: return v == 255u ? v : v + 1u;
    case 4u: return v == 0u ? v : v - 1u;
    case 5u: return ~v & 255u;
    default: return v; // KEEP, and 6 / 7 (no wrapping stencil ops on the FT812)
    }
}

EVE_FN EveU32 EveFactor(EveU32 f, EveU32 sa, EveU32 da)
{
    switch (f)
    {
    case 0u: return 0u;
    case 1u: return 255u;
    case 2u: return sa;
    case 3u: return da;
    case 4u: return 255u - sa;
    case 5u: return 255u - da;
    default: return 0u;
    }
}

EVE_FN EveU64 EveIntSqrt(EveU64 v)
{
    EveU64 r = (EveU64)EVE_SQRTF((float)v);
    while (r * r > v)
        --r;
    while ((r + 1u) * (r + 1u) <= v)
        ++r;
    return r;
}

EVE_FN EveI64 EveFloorDivide(EveI64 v, EveI64 d)
{
    const EveI64 q = v / d;
    return (v % d != 0 && v < 0) ? q - 1 : q;
}

// eve-raster.cpp SegmentDistance: the foot of the perpendicular rounded down to 1/16
EVE_FN EveU32 EveSegmentDistance(EveI64 px, EveI64 py, EveI64 ax, EveI64 ay, EveI64 bx, EveI64 by)
{
    const EveI64 qx = px - ax, qy = py - ay, dx = bx - ax, dy = by - ay;
    const EveI64 length2 = dx * dx + dy * dy;
    EveI64 fx = 0, fy = 0;
    if (length2 > 0)
    {
        EveI64 dot = qx * dx + qy * dy;
        dot = dot < 0 ? 0 : (dot > length2 ? length2 : dot);
        fx = EveFloorDivide(dot * dx, length2);
        fy = EveFloorDivide(dot * dy, length2);
    }
    const EveI64 ex = qx - fx, ey = qy - fy;
    return (EveU32)EveIntSqrt((EveU64)(ex * ex + ey * ey));
}

EVE_FN EveU32 EveAaAlpha(EVE_CTX EveU32 radius, EveU32 distance)
{
    if (radius == 0u || distance > radius + 14u)
        return 0u;
    if (radius < 62u)
        return (EveU32)EVE_AA((radius - 1u) * 80u + distance);
    if (distance + 14u <= radius)
        return 255u;
    return (EveU32)EVE_AA(61u * 80u + radius + 14u - distance);
}

// Coverage of an antialiased point, line or rectangle at pixel (x, y), as eve-raster.cpp
EVE_FN EveU32 EvePrimitiveCoverage(EVE_CTX EveU32 i, EveI32 x, EveI32 y)
{
    const EveI64 px = (EveI64)x * 16, py = (EveI64)y * 16;
    const EveI64 ax = EVE_OP(i).kx, ay = EVE_OP(i).ky, bx = EVE_OP(i).a, by = EVE_OP(i).b;
    if (EVE_OP(i).kind == 2u)
    {
        const EveI64 ix = px - bx, iy = py - by;
        return EveAaAlpha(EVE_PASS EVE_OP(i).base, (EveU32)EveIntSqrt((EveU64)(ix * ix + iy * iy)));
    }
    if (EVE_OP(i).kind == 3u)
        return EveAaAlpha(EVE_PASS EVE_OP(i).base, EveSegmentDistance(px, py, ax, ay, bx, by));
    const EveI64 x0 = ax < bx ? ax : bx, x1 = ax < bx ? bx : ax;
    const EveI64 y0 = ay < by ? ay : by, y1 = ay < by ? by : ay;
    const EveI64 ix = px < x0 ? x0 - px : (px > x1 ? px - x1 : 0);
    const EveI64 iy = py < y0 ? y0 - py : (py > y1 ? py - y1 : 0);
    return EveAaAlpha(EVE_PASS EVE_OP(i).base, (EveU32)EveIntSqrt((EveU64)(ix * ix + iy * iy)));
}

// The bitmap sample of op i at pixel (x, y); not valid outside the bitmap's rows
EVE_FN EveSource EveBitmapSample(EVE_CTX EveU32 i, EveI32 x, EveI32 y)
{
    EveSource s;
    s.r = 0u;
    s.g = 0u;
    s.b = 0u;
    s.a = 0u;
    s.valid = false;
    const EveI32 rely = y * 16 - EVE_OP(i).vy;
    if (rely < 0 || rely >= EVE_OP(i).height16)
        return s;
    const EveI32 dx = x - EVE_OP(i).x0;
    const EveI32 sx = EVE_OP(i).kx + EVE_OP(i).a * dx + EVE_OP(i).b * y;
    const EveI32 sy = EVE_OP(i).ky + EVE_OP(i).d * dx + EVE_OP(i).e * y;
    EveU32 t = 0u;
    if (EVE_OP(i).filterMode == 0u)
        t = EveWrapped(EVE_PASS i, sx >> 8, sy >> 8);
    else
    {
        const EveI32 tx = sx >> 8, ty = sy >> 8;
        const EveU32 fx = (EveU32)(sx - tx * 256), fy = (EveU32)(sy - ty * 256);
        const EveU32 t00 = EveWrapped(EVE_PASS i, tx, ty), t10 = EveWrapped(EVE_PASS i, tx + 1, ty);
        const EveU32 t01 = EveWrapped(EVE_PASS i, tx, ty + 1), t11 = EveWrapped(EVE_PASS i, tx + 1, ty + 1);
        const EveU32 w11 = (fx * fy) >> 8;
        const EveU32 w00 = 256u - fx - fy + w11, w10 = fx - w11, w01 = fy - w11;
        for (EveI32 sh = 24; sh >= 0; sh -= 8)
        {
            const EveU32 v = ((((t00 >> sh) & 255u) * w00) >> 8) + ((((t10 >> sh) & 255u) * w10) >> 8) +
                             ((((t01 >> sh) & 255u) * w01) >> 8) + ((((t11 >> sh) & 255u) * w11) >> 8);
            t |= v << sh;
        }
    }
    // COLOR_RGB / COLOR_A modulate every bitmap pixel
    const EveU32 rgb = EVE_OP(i).colorRgb;
    s.r = EveMul(t >> 24, rgb >> 16);
    s.g = EveMul((t >> 16) & 255u, (rgb >> 8) & 255u);
    s.b = EveMul((t >> 8) & 255u, rgb & 255u);
    s.a = EveMul(t & 255u, EVE_OP(i).colorA);
    s.valid = true;
    return s;
}

// Op i on one pixel
EVE_FN EvePixel EveApply(EVE_CTX EvePixel c, EveU32 i, EveI32 x, EveI32 y)
{
    if (y < EVE_OP(i).y0 || y >= EVE_OP(i).y1 || x < EVE_OP(i).x0 || x >= EVE_OP(i).x1)
        return c;
    const EveU32 mask = EVE_OP(i).colorMask;
    if (EVE_OP(i).kind == 0u)
    {
        if ((EVE_OP(i).clearMask & 4u) != 0u)
        {
            const EveU32 v = EVE_OP(i).clearRgba;
            if ((mask & 8u) != 0u) c.r = v >> 24;
            if ((mask & 4u) != 0u) c.g = (v >> 16) & 255u;
            if ((mask & 2u) != 0u) c.b = (v >> 8) & 255u;
            if ((mask & 1u) != 0u) c.a = v & 255u;
        }
        if ((EVE_OP(i).clearMask & 2u) != 0u)
            c.stencil = (c.stencil & ~EVE_OP(i).stencilWriteMask & 255u) | (EVE_OP(i).clearStencil & EVE_OP(i).stencilWriteMask);
        return c;
    }
    EveSource s;
    if (EVE_OP(i).kind >= 2u)
    {
        // an antialiased primitive: COLOR_RGB with alpha COLOR_A x coverage
        const EveU32 coverage = EvePrimitiveCoverage(EVE_PASS i, x, y);
        if (coverage == 0u)
            return c;
        s.r = EVE_OP(i).colorRgb >> 16;
        s.g = (EVE_OP(i).colorRgb >> 8) & 255u;
        s.b = EVE_OP(i).colorRgb & 255u;
        s.a = EveMul(EVE_OP(i).colorA, coverage);
    }
    else
    {
        s = EveBitmapSample(EVE_PASS i, x, y);
        if (!s.valid)
            return c;
    }
    if (!EveCompare(EVE_OP(i).alphaFunc, s.a, EVE_OP(i).alphaRef))
        return c;
    const EveU32 fm = EVE_OP(i).stencilFuncMask, ref = EVE_OP(i).stencilRef, wm = EVE_OP(i).stencilWriteMask;
    const bool pass = EveCompare(EVE_OP(i).stencilFunc, c.stencil & fm, ref & fm);
    const EveU32 updated = EveStencilOp(pass ? EVE_OP(i).stencilPass : EVE_OP(i).stencilFail, c.stencil, ref);
    c.stencil = (c.stencil & ~wm & 255u) | (updated & wm);
    if (!pass)
        return c;
    const EveU32 sf = EveFactor(EVE_OP(i).blendSrc, s.a, c.a);
    const EveU32 df = EveFactor(EVE_OP(i).blendDst, s.a, c.a);
    const EveU32 r = EveMul(s.r, sf) + EveMul(c.r, df), g = EveMul(s.g, sf) + EveMul(c.g, df);
    const EveU32 b = EveMul(s.b, sf) + EveMul(c.b, df), a = EveMul(s.a, sf) + EveMul(c.a, df);
    if ((mask & 8u) != 0u) c.r = r > 255u ? 255u : r;
    if ((mask & 4u) != 0u) c.g = g > 255u ? 255u : g;
    if ((mask & 2u) != 0u) c.b = b > 255u ? 255u : b;
    if ((mask & 1u) != 0u) c.a = a > 255u ? 255u : a;
    return c;
}

// A whole pixel: the ops list[0 .. count) of its row band, in order; returns 0xFFRRGGBB
#ifndef EVE_LIST
#error "define EVE_LIST(k): the op index at position k of the band list"
#endif
EVE_FN EveU32 EveEvaluatePixel(EVE_CTX EveU32 from, EveU32 to, EveI32 x, EveI32 y)
{
    EvePixel c;
    c.r = 0u;
    c.g = 0u;
    c.b = 0u;
    c.a = 0u;
    c.stencil = 0u;
    for (EveU32 k = from; k < to; ++k)
        c = EveApply(EVE_PASS c, EVE_LIST(k), x, y);
    return 0xFF000000u | (c.r << 16) | (c.g << 8) | c.b;
}

#endif
