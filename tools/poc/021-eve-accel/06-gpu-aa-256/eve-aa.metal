// eve-accel 06: antialiased lines on the GPU, two coverage models.
//   AaTable  eve-emu's model: distance to the segment (foot rounded down to 1/16, integer
//            square root), alpha from the BT8XX table - integer math, as eve-raster.cpp
//   Aa256    "honest" coverage: 16 x 16 subsamples per pixel tested against the capsule
// Both: one dispatch per frame, each pixel runs the shapes of its 16-row band and blends
// COLOR_RGB with alpha = Multiply(COLOR_A, coverage) by SRC_ALPHA / ONE_MINUS_SRC_ALPHA.
#include <metal_stdlib>
using namespace metal;

struct Shape
{
    int ax, ay, bx, by;   // 1/16 pixel
    uint radius;          // LINE_WIDTH / POINT_SIZE, 1/16 pixel
    uint color;           // 0xRRGGBB
    uint alpha;           // COLOR_A
    int x0, y0, x1, y1;   // pixel bounds (inclusive-exclusive) of the shape and its ramp
    uint pad0;
};

struct Params
{
    uint width, height, band, model;
};

static inline uint Mul(uint a, uint b)
{
    const uint t = a * b + 127;
    return (t + 1 + (t >> 8)) >> 8;
}

static inline long FloorDivide(long v, long d)
{
    const long q = v / d;
    return (v % d != 0 && v < 0) ? q - 1 : q;
}

static inline uint IntSqrt(ulong v)
{
    ulong r = ulong(sqrt(float(v)));
    while (r * r > v)
        --r;
    while ((r + 1) * (r + 1) <= v)
        ++r;
    return uint(r);
}

static inline uint SegmentDistance(long px, long py, long ax, long ay, long bx, long by)
{
    const long qx = px - ax, qy = py - ay, dx = bx - ax, dy = by - ay;
    const long length2 = dx * dx + dy * dy;
    long fx = 0, fy = 0;
    if (length2 > 0)
    {
        long dot = qx * dx + qy * dy;
        dot = dot < 0 ? 0 : (dot > length2 ? length2 : dot);
        fx = FloorDivide(dot * dx, length2);
        fy = FloorDivide(dot * dy, length2);
    }
    const long ex = qx - fx, ey = qy - fy;
    return IntSqrt(ulong(ex * ex + ey * ey));
}

static inline uint TableAlpha(device const uchar* table, uint radius, uint distance)
{
    // table: rows r = 1..61 of 80 entries (d = 0..r + 14), then the r >= 62 edge (29 entries)
    if (radius == 0 || distance > radius + 14)
        return 0;
    if (radius < 62)
        return table[(radius - 1) * 80 + distance];
    if (distance + 14 <= radius)
        return 255;
    return table[61 * 80 + radius + 14 - distance];
}

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

kernel void RenderLines(device const Shape* shapes [[buffer(0)]], device const uint* bandStart [[buffer(1)]],
                        device const uint* bandShapes [[buffer(2)]], device const uchar* table [[buffer(3)]],
                        device uint* out [[buffer(4)]], constant Params& p [[buffer(5)]],
                        uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= p.width || gid.y >= p.height)
        return;
    const int x = int(gid.x), y = int(gid.y);
    const uint band = gid.y / p.band;
    uint3 c = uint3(0);
    for (uint k = bandStart[band]; k < bandStart[band + 1]; ++k)
    {
        const Shape s = shapes[bandShapes[k]];
        if (x < s.x0 || x >= s.x1 || y < s.y0 || y >= s.y1)
            continue;
        uint coverage;
        if (p.model == 0)
            coverage = TableAlpha(table, s.radius, SegmentDistance(x * 16, y * 16, s.ax, s.ay, s.bx, s.by));
        else
        {
            // 256 subsamples at (i + 0.5) / 16 around the pixel center
            const float r2 = float(s.radius) * float(s.radius);
            uint inside = 0;
            for (int j = 0; j < 16; ++j)
                for (int i = 0; i < 16; ++i)
                    inside += Distance2(float(x * 16 + i) - 7.5f, float(y * 16 + j) - 7.5f, float(s.ax), float(s.ay),
                                        float(s.bx), float(s.by)) <= r2;
            coverage = (inside * 255 + 128) / 256;
        }
        if (coverage == 0)
            continue;
        const uint a = Mul(s.alpha, coverage), inv = 255 - a;
        const uint3 src = uint3(s.color >> 16, (s.color >> 8) & 255, s.color & 255);
        c = min(uint3(Mul(src.x, a) + Mul(c.x, inv), Mul(src.y, a) + Mul(c.y, inv), Mul(src.z, a) + Mul(c.z, inv)),
                uint3(255));
    }
    out[gid.y * p.width + gid.x] = 0xFF000000u | (c.x << 16) | (c.y << 8) | c.z;
}
