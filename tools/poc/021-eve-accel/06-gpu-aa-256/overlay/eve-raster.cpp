// eve-emu - points, lines, line strips, edge strips, rectangles (spec §6.4).
//
// Each primitive is asked only for its span on the current line. The alpha of a point,
// line or rectangle pixel comes from the BT8XX table of eve-aa-table.cpp, by the radius
// and the pixel's distance to the shape's core (spec V2), and scales COLOR_A.
#include "eve-render.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace EveLib
{

namespace
{

// Reach of a shape beyond its radius, 1/16 pixel: the table's reach plus a margin for the
// rounded-down foot of SegmentDistance (the table decides the alpha).
constexpr double kSpanMargin = 2;
constexpr double kHalfRamp = kAntialiasReach + kSpanMargin;
constexpr uint32_t kRedShift = 16, kGreenShift = 8;

// Alpha of a pixel at `distance` (1/16 pixel, rounded down) from the core of a shape.
uint32_t Coverage(uint32_t radius, uint32_t distance)
{
    return AntialiasAlpha(radius, distance);
}

// --- eve-accel 06: "honest" coverage models (EVE_AA_MODEL) ---------------------------------
//
//   table     eve-emu's distance table (BT8XX), the default
//   box256    16 x 16 subsamples per pixel at (i + 0.5) / 16, alpha = inside / 256 x 255
//   box256c   the same grid shifted to the pixel's corner (i / 16)
//   box256le  box256 with a strict inside test (d < r instead of d <= r)
// The subsample grid is centered on the pixel center (eve-emu: pixel x at x * 16 in 1/16).
enum class AaModel { Table, Box, BoxCorner, BoxStrict };

AaModel Model()
{
    static const AaModel model = [] {
        const char* m = std::getenv("EVE_AA_MODEL");
        if (m == nullptr || !std::strcmp(m, "table"))
            return AaModel::Table;
        if (!std::strcmp(m, "box256c"))
            return AaModel::BoxCorner;
        if (!std::strcmp(m, "box256le"))
            return AaModel::BoxStrict;
        return AaModel::Box;
    }();
    return model;
}

// Squared distance from (px, py) to the segment ab, all in 1/16 pixel
double SegmentDistance2(double px, double py, double ax, double ay, double bx, double by)
{
    const double dx = bx - ax, dy = by - ay;
    const double length2 = dx * dx + dy * dy;
    double t = 0;
    if (length2 > 0)
    {
        t = ((px - ax) * dx + (py - ay) * dy) / length2;
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
    }
    const double ex = px - (ax + t * dx), ey = py - (ay + t * dy);
    return ex * ex + ey * ey;
}

// Coverage of pixel (x, y) by the set {p : dist(p, segment ab) <= r}, 256 subsamples;
// a point is the segment a = b, a rectangle's core is handled by the caller.
template <typename Distance2>
uint32_t Supersample(int32_t x, int32_t y, double r, Distance2 distance2)
{
    const AaModel model = Model();
    const double offset = model == AaModel::BoxCorner ? -8.0 : -7.5; // subsample i at x*16 + i + offset
    const double r2 = r * r;
    uint32_t inside = 0;
    // SIMD-CANDIDATE(256 subsample tests per pixel)
    for (int j = 0; j < 16; ++j)
    {
        const double sy = static_cast<double>(y) * kSubpixel + kPixelCenter + j + offset;
        for (int i = 0; i < 16; ++i)
        {
            const double sx = static_cast<double>(x) * kSubpixel + kPixelCenter + i + offset;
            const double d2 = distance2(sx, sy);
            inside += model == AaModel::BoxStrict ? (d2 < r2) : (d2 <= r2);
        }
    }
    return (inside * kChannelMax + 128) / 256;
}

double PixelCenter(int32_t pixel)
{
    return static_cast<double>(pixel) * kSubpixel + kPixelCenter;
}

// Pixels whose centers lie in [left, right] (1/16 pixel), clipped to the scissor.
bool PixelRange(const LineRun& run, double left, double right, int32_t& first, int32_t& last)
{
    first = static_cast<int32_t>(std::ceil((left - kPixelCenter) / kSubpixel));
    last = static_cast<int32_t>(std::floor((right - kPixelCenter) / kSubpixel)) + 1;
    return ScissorSpan(run, first, last);
}

template <LineMode Mode>
void ShadeCovered(LineRun& run, int32_t x, uint32_t coverage)
{
    if (coverage == 0)
        return;
    const GraphicsContext& ctx = run.ctx;
    Shade<Mode>(run, x, (ctx.colorRgb >> kRedShift) & kChannelMax, (ctx.colorRgb >> kGreenShift) & kChannelMax,
                ctx.colorRgb & kChannelMax, Multiply(ctx.colorA, coverage));
}

void CountFill(LineRun& run, int32_t first, int32_t last)
{
    if (last > first)
        run.fillCost += static_cast<uint64_t>(last - first) * (kFillCostScale / kPrimitivePixelsPerClock);
}

// floor(sqrt(value)) exactly.
uint32_t IntSqrt(uint64_t value)
{
    uint64_t root = static_cast<uint64_t>(std::sqrt(static_cast<double>(value)));
    while (root * root > value)
        --root;
    while ((root + 1) * (root + 1) <= value)
        ++root;
    return static_cast<uint32_t>(root);
}

int64_t FloorDivide(int64_t value, int64_t divisor) // divisor > 0
{
    const int64_t q = value / divisor;
    return (value % divisor != 0 && value < 0) ? q - 1 : q;
}

// Distance (1/16 pixel, rounded down) from p to the segment ab as BT8XX measures it: the
// foot of the perpendicular is rounded down to whole 1/16 units first (golden case lines;
// a few pixels where the foot lies next to a whole unit still differ: TO VERIFY, spec V2).
uint32_t SegmentDistance(int64_t px, int64_t py, int64_t ax, int64_t ay, int64_t bx, int64_t by)
{
    const int64_t qx = px - ax, qy = py - ay;
    const int64_t dx = bx - ax, dy = by - ay;
    const int64_t length2 = dx * dx + dy * dy;
    int64_t fx = 0, fy = 0;
    if (length2 > 0)
    {
        int64_t dot = qx * dx + qy * dy;
        dot = dot < 0 ? 0 : (dot > length2 ? length2 : dot);
        fx = FloorDivide(dot * dx, length2);
        fy = FloorDivide(dot * dy, length2);
    }
    const int64_t ex = qx - fx, ey = qy - fy;
    return IntSqrt(static_cast<uint64_t>(ex * ex + ey * ey));
}

// The x interval of a horizontal line y inside a capsule (segment ab, radius r).
bool CapsuleInterval(double y, double ax, double ay, double bx, double by, double r, double& left, double& right)
{
    bool any = false;
    left = 0;
    right = 0;
    auto include = [&](double l, double rr) {
        if (!any)
        {
            left = l;
            right = rr;
            any = true;
        }
        else
        {
            left = l < left ? l : left;
            right = rr > right ? rr : right;
        }
    };
    // The two end discs.
    for (const double* end : {&ax, &bx})
    {
        const double cy = end == &ax ? ay : by;
        const double dy = y - cy;
        if (std::fabs(dy) <= r)
        {
            const double half = std::sqrt(r * r - dy * dy);
            include(*end - half, *end + half);
        }
    }
    // The body: |n . (p - a)| <= r and 0 <= u . (p - a) <= length.
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length > 0)
    {
        const double ux = dx / length, uy = dy / length; // along
        const double nx = -uy, ny = ux;                  // across
        double lo = -INFINITY, hi = INFINITY;
        auto clampTo = [&](double coef, double constant, double low, double high) {
            // low <= coef * x + constant <= high
            if (coef == 0)
            {
                if (constant < low || constant > high)
                    lo = INFINITY;
                return;
            }
            double a = (low - constant) / coef;
            double b = (high - constant) / coef;
            if (a > b)
                std::swap(a, b);
            lo = a > lo ? a : lo;
            hi = b < hi ? b : hi;
        };
        clampTo(nx, ny * (y - ay) - nx * ax, -r, r);
        clampTo(ux, uy * (y - ay) - ux * ax, 0, length);
        if (lo <= hi)
            include(lo, hi);
    }
    return any;
}

} // namespace

template <LineMode Mode>
void DrawPoint(LineRun& run, const Vertex& v)
{
    const double r = run.ctx.pointSize;
    const double outer = r + kHalfRamp;
    const double y = PixelCenter(static_cast<int32_t>(run.y));
    const double dy = y - v.y;
    if (std::fabs(dy) > outer)
        return;
    const double half = std::sqrt(outer * outer - dy * dy);
    int32_t first = 0, last = 0;
    if (!PixelRange(run, v.x - half, v.x + half, first, last))
        return;
    CountFill(run, first, last);
    const int64_t iy = static_cast<int64_t>(y) - v.y;
    for (int32_t x = first; x < last; ++x)
    {
        const int64_t ix = static_cast<int64_t>(PixelCenter(x)) - v.x;
        if (Model() != AaModel::Table)
        {
            const double cx = v.x, cy = v.y;
            ShadeCovered<Mode>(run, x, Supersample(x, static_cast<int32_t>(run.y), r, [=](double sx, double sy) {
                                   return (sx - cx) * (sx - cx) + (sy - cy) * (sy - cy);
                               }));
            continue;
        }
        ShadeCovered<Mode>(run, x, Coverage(run.ctx.pointSize, IntSqrt(static_cast<uint64_t>(ix * ix + iy * iy))));
    }
}

template <LineMode Mode>
void DrawLine(LineRun& run, const Vertex& a, const Vertex& b)
{
    const double r = run.ctx.lineWidth;
    const double y = PixelCenter(static_cast<int32_t>(run.y));
    double left = 0, right = 0;
    if (!CapsuleInterval(y, a.x, a.y, b.x, b.y, r + kHalfRamp, left, right))
        return;
    int32_t first = 0, last = 0;
    if (!PixelRange(run, left, right, first, last))
        return;
    CountFill(run, first, last);
    const int64_t iy = static_cast<int64_t>(y);
    if (Model() != AaModel::Table)
    {
        const double ax = a.x, ay = a.y, bx = b.x, by = b.y;
        for (int32_t x = first; x < last; ++x)
            ShadeCovered<Mode>(run, x, Supersample(x, static_cast<int32_t>(run.y), r, [=](double sx, double sy) {
                                   return SegmentDistance2(sx, sy, ax, ay, bx, by);
                               }));
        return;
    }
    for (int32_t x = first; x < last; ++x)
        ShadeCovered<Mode>(run, x, Coverage(run.ctx.lineWidth, SegmentDistance(static_cast<int64_t>(PixelCenter(x)), iy,
                                                                              a.x, a.y, b.x, b.y)));
}

template <LineMode Mode>
void DrawRect(LineRun& run, const Vertex& a, const Vertex& b)
{
    // LINE_WIDTH rounds the corners and grows the rectangle by that amount on each side
    // [PG §2.5.4].
    const double r = run.ctx.lineWidth;
    const double x0 = a.x < b.x ? a.x : b.x, x1 = a.x < b.x ? b.x : a.x;
    const double y0 = a.y < b.y ? a.y : b.y, y1 = a.y < b.y ? b.y : a.y;
    const double y = PixelCenter(static_cast<int32_t>(run.y));
    const double outer = r + kHalfRamp;
    const double dy = y < y0 ? y0 - y : (y > y1 ? y - y1 : 0);
    if (dy > outer)
        return;
    const double half = std::sqrt(outer * outer - dy * dy);
    int32_t first = 0, last = 0;
    if (!PixelRange(run, x0 - half, x1 + half, first, last))
        return;
    CountFill(run, first, last);
    const int64_t iy = static_cast<int64_t>(dy);
    for (int32_t x = first; x < last; ++x)
    {
        const double px = PixelCenter(x);
        if (Model() != AaModel::Table)
        {
            ShadeCovered<Mode>(run, x, Supersample(x, static_cast<int32_t>(run.y), r, [=](double sx, double sy) {
                                   const double ex = sx < x0 ? x0 - sx : (sx > x1 ? sx - x1 : 0);
                                   const double ey = sy < y0 ? y0 - sy : (sy > y1 ? sy - y1 : 0);
                                   return ex * ex + ey * ey;
                               }));
            continue;
        }
        const int64_t ix = static_cast<int64_t>(px < x0 ? x0 - px : (px > x1 ? px - x1 : 0));
        ShadeCovered<Mode>(run, x, Coverage(run.ctx.lineWidth, IntSqrt(static_cast<uint64_t>(ix * ix + iy * iy))));
    }
}

template <LineMode Mode>
void DrawEdge(LineRun& run, const Vertex& a, const Vertex& b, uint8_t primitive)
{
    // Not antialiased: the edge is truncated to whole pixels and a pixel is filled when its
    // far side reaches it (kEdgeStripHard, spec V2).
    const double y = PixelCenter(static_cast<int32_t>(run.y));
    if (primitive == kPrimEdgeStripR || primitive == kPrimEdgeStripL)
    {
        // The segment's x on this line; half-open in y so joined segments do not overlap.
        const double ylo = a.y < b.y ? a.y : b.y, yhi = a.y < b.y ? b.y : a.y;
        if (y < ylo || y >= yhi)
            return;
        const double edge = a.x + (b.x - a.x) * (y - a.y) / (static_cast<double>(b.y) - a.y);
        const int32_t edgePixel = static_cast<int32_t>(std::floor(edge / kSubpixel));
        int32_t first = primitive == kPrimEdgeStripR ? edgePixel - 1 : 0;
        int32_t last = primitive == kPrimEdgeStripR ? static_cast<int32_t>(run.width) : edgePixel;
        if (!ScissorSpan(run, first, last))
            return;
        CountFill(run, first, last);
        for (int32_t x = first; x < last; ++x)
            ShadeCovered<Mode>(run, x, kChannelMax);
        return;
    }
    // Above / below: a pixel is filled when its sample is on the fill side of the segment
    // or on it, the segment half-open in x. BT8XX samples EDGE_STRIP_B at the pixel's corner
    // and EDGE_STRIP_A half a pixel to the right (golden case edge-strips).
    const bool above = primitive == kPrimEdgeStripA;
    const double sampleOffset = above ? kSubpixel / 2.0 : 0;
    const double xlo = a.x < b.x ? a.x : b.x, xhi = a.x < b.x ? b.x : a.x;
    int32_t first = static_cast<int32_t>(std::ceil((xlo - sampleOffset - kPixelCenter) / kSubpixel));
    int32_t last = static_cast<int32_t>(std::ceil((xhi - sampleOffset - kPixelCenter) / kSubpixel));
    if (!ScissorSpan(run, first, last))
        return;
    int32_t covered = 0;
    for (int32_t x = first; x < last; ++x)
    {
        const double px = PixelCenter(x) + sampleOffset;
        const double edge = a.y + (b.y - a.y) * (px - a.x) / (static_cast<double>(b.x) - a.x);
        if (above ? y <= edge : y >= edge)
        {
            ++covered;
            ShadeCovered<Mode>(run, x, kChannelMax);
        }
    }
    CountFill(run, 0, covered);
}

template void DrawPoint<LineMode::Draw>(LineRun&, const Vertex&);
template void DrawPoint<LineMode::Probe>(LineRun&, const Vertex&);
template void DrawLine<LineMode::Draw>(LineRun&, const Vertex&, const Vertex&);
template void DrawLine<LineMode::Probe>(LineRun&, const Vertex&, const Vertex&);
template void DrawRect<LineMode::Draw>(LineRun&, const Vertex&, const Vertex&);
template void DrawRect<LineMode::Probe>(LineRun&, const Vertex&, const Vertex&);
template void DrawEdge<LineMode::Draw>(LineRun&, const Vertex&, const Vertex&, uint8_t);
template void DrawEdge<LineMode::Probe>(LineRun&, const Vertex&, const Vertex&, uint8_t);

} // namespace EveLib
