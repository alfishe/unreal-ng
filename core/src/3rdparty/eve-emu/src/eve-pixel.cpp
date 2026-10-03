// eve-emu - scissor, alpha test, stencil, blend, color mask, tag (spec §6.6).
#include "eve-render.h"
#include "eve-simd.h"

namespace EveLib
{

namespace
{

constexpr uint32_t kChannelAlpha = 3; // line buffer channel order R, G, B, A

bool Compare(uint8_t func, uint32_t value, uint32_t reference)
{
    switch (func)
    {
    case kFuncNever: return false;
    case kFuncLess: return value < reference;
    case kFuncLequal: return value <= reference;
    case kFuncGreater: return value > reference;
    case kFuncGequal: return value >= reference;
    case kFuncEqual: return value == reference;
    case kFuncNotequal: return value != reference;
    default: return true;
    }
}

uint8_t StencilResult(uint8_t op, uint8_t value, uint8_t reference)
{
    switch (op)
    {
    case kStencilZero: return 0;
    case kStencilKeep: return value;
    case kStencilReplace: return reference;
    case kStencilIncr: return value == kChannelMax ? value : static_cast<uint8_t>(value + 1);
    case kStencilDecr: return value == 0 ? value : static_cast<uint8_t>(value - 1);
    case kStencilInvert: return static_cast<uint8_t>(~value);
    case kStencilIncrWrap: return kStencilWrapOps ? static_cast<uint8_t>(value + 1) : value;
    case kStencilDecrWrap: return kStencilWrapOps ? static_cast<uint8_t>(value - 1) : value;
    default: return value;
    }
}

uint32_t Factor(uint8_t factor, uint32_t srcAlpha, uint32_t dstAlpha)
{
    switch (factor)
    {
    case kBlendZero: return 0;
    case kBlendOne: return kChannelMax;
    case kBlendSrcAlpha: return srcAlpha;
    case kBlendDstAlpha: return dstAlpha;
    case kBlendOneMinusSrcAlpha: return kChannelMax - srcAlpha;
    case kBlendOneMinusDstAlpha: return kChannelMax - dstAlpha;
    default: return 0;
    }
}

uint8_t Saturate(uint32_t value)
{
    return static_cast<uint8_t>(value > kChannelMax ? kChannelMax : value);
}

// The blends a span takes in a tight loop when no test can reject a pixel; false for the
// others (the general loop then runs). Exact: Multiply(x, 255) == x and Multiply(x, 0) == 0
bool BlendSpanFast(LineRun& run, int32_t first, const uint32_t* rgba, uint32_t count)
{
    const GraphicsContext& ctx = run.ctx;
    constexpr uint8_t kRgb = kMaskRed | kMaskGreen | kMaskBlue;
    constexpr uint32_t kPackRed = 24, kPackGreen = 16, kPackBlue = 8;
    uint8_t* dst = run.color + kChannels * static_cast<uint32_t>(first);
    const uint8_t src = ctx.blendSrc;
    const uint8_t dstFactor = ctx.blendDst;
    if (src == kBlendOne && dstFactor == kBlendOne)
    {
        // dst = min(src + dst, 255) on the channels COLOR_MASK lets through (R-Type's masks
        // accumulate alpha alone, the SDK's test6 adds whole pixels)
        Simd::AddSaturate(dst, rgba, count, ctx.colorMask);
    }
    else if (src == kBlendDstAlpha && dstFactor == kBlendZero && ctx.colorMask == kRgb && kMultiplyRoundDiv255)
    {
        // dst.rgb = src.rgb x dst.a
        Simd::MultiplyRgbByDstAlpha(dst, rgba, count);
    }
    else if ((src == kBlendOneMinusDstAlpha || src == kBlendDstAlpha) && dstFactor == kBlendOne && ctx.colorMask == kRgb &&
             kMultiplyRoundDiv255)
    {
        // dst.rgb = min(src.rgb x (255 - dst.a) + dst.rgb, 255), or x dst.a (the SDK's
        // CMD_GRADIENT test draws its ramp through the alpha this way)
        Simd::AddRgbTimesDstAlpha(dst, rgba, count, src == kBlendOneMinusDstAlpha);
    }
    else if (src == kBlendOne && dstFactor == kBlendZero)
    {
        // dst = src on the channels COLOR_MASK lets through (x 255 / 255 and x 0 are exact);
        // Zuma's masks write alpha alone this way
        if (ctx.colorMask == kMaskAlpha)
        {
            for (uint32_t i = 0; i < count; ++i, dst += kChannels)
                dst[kChannelAlpha] = static_cast<uint8_t>(rgba[i] & kChannelMax);
        }
        else
        {
            const uint8_t mask[kChannels] = {kMaskRed, kMaskGreen, kMaskBlue, kMaskAlpha};
            const uint32_t shift[kChannels] = {kPackRed, kPackGreen, kPackBlue, 0};
            for (uint32_t i = 0; i < count; ++i, dst += kChannels)
                for (uint32_t c = 0; c < kChannels; ++c)
                    if (ctx.colorMask & mask[c])
                        dst[c] = static_cast<uint8_t>((rgba[i] >> shift[c]) & kChannelMax);
        }
    }
    else
        return false;
    if (WritesTag(run))
        std::memset(run.tag + first, ctx.tag, count);
    return true;
}

} // namespace

bool ScissorSpan(const LineRun& run, int32_t& first, int32_t& last)
{
    const GraphicsContext& ctx = run.ctx;
    if (run.y < ctx.scissorY || run.y >= static_cast<uint32_t>(ctx.scissorY) + ctx.scissorHeight)
        return false;
    const int32_t left = ctx.scissorX;
    const int32_t right = ctx.scissorX + ctx.scissorWidth;
    first = first > left ? first : left;
    last = last < right ? last : right;
    if (first < 0)
        first = 0;
    if (last > static_cast<int32_t>(run.width))
        last = static_cast<int32_t>(run.width);
    return first < last;
}

template <LineMode Mode>
void Shade(LineRun& run, int32_t x, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    const GraphicsContext& ctx = run.ctx;
    // Alpha test on the source alpha.
    if (!Compare(ctx.alphaFunc, a, ctx.alphaRef))
        return;
    // Stencil test and update.
    uint8_t& stencil = run.stencil[x];
    const bool pass = Compare(ctx.stencilFunc, stencil & ctx.stencilFuncMask, ctx.stencilRef & ctx.stencilFuncMask);
    const uint8_t updated = StencilResult(pass ? ctx.stencilPass : ctx.stencilFail, stencil, ctx.stencilRef);
    stencil = static_cast<uint8_t>((stencil & ~ctx.stencilWriteMask) | (updated & ctx.stencilWriteMask));
    if (!pass)
        return;
    // Blend: result = source x src factor + destination x dst factor, saturated.
    uint8_t* dst = run.color + kChannels * static_cast<uint32_t>(x);
    const uint32_t sf = Factor(ctx.blendSrc, a, dst[kChannelAlpha]);
    const uint32_t df = Factor(ctx.blendDst, a, dst[kChannelAlpha]);
    const uint32_t source[kChannels] = {r, g, b, a};
    const uint8_t mask[kChannels] = {kMaskRed, kMaskGreen, kMaskBlue, kMaskAlpha};
    uint8_t out[kChannels];
    for (uint32_t c = 0; c < kChannels; ++c)
        out[c] = Saturate(static_cast<uint32_t>(Multiply(source[c], sf)) + Multiply(dst[c], df));
    for (uint32_t c = 0; c < kChannels; ++c)
        if (ctx.colorMask & mask[c])
            dst[c] = out[c];
    if (WritesTag(run))
        run.tag[x] = ctx.tag;
    if (Mode == LineMode::Probe && x == run.probeX)
    {
        EvePixelSource& p = *run.probe;
        p.written = 1;
        p.commandIndex = run.commandIndex;
        p.command = run.commandWord;
        p.primitive = run.primitive;
    }
}

void ShadeSpan(LineRun& run, int32_t first, const uint32_t* rgba, uint32_t count)
{
    const GraphicsContext& ctx = run.ctx;
    constexpr uint8_t kAllChannels = kMaskRed | kMaskGreen | kMaskBlue | kMaskAlpha;
    constexpr uint32_t kPackRed = 24, kPackGreen = 16, kPackBlue = 8;
    const bool alphaTest = ctx.alphaFunc != kFuncAlways;
    // The stencil buffer stays as it is, and every pixel passes, when the test always
    // passes and its pass operation keeps the value (or nothing may be written)
    const bool stencilActive =
        !(ctx.stencilFunc == kFuncAlways && (ctx.stencilPass == kStencilKeep || ctx.stencilWriteMask == 0));
    const bool fullMask = ctx.colorMask == kAllChannels;
    const bool copy = ctx.blendSrc == kBlendOne && ctx.blendDst == kBlendZero;  // dst = src exactly
    const bool keep = ctx.blendSrc == kBlendZero && ctx.blendDst == kBlendOne;  // dst stays exactly
    // Every pixel reaches the blend: the common masking blends of a span in tight loops
    // (R-Type's masks: alpha accumulated with ONE / ONE, the picture drawn through it with
    // DST_ALPHA / ZERO and ONE_MINUS_DST_ALPHA / ONE), the same arithmetic per pixel
    if (!alphaTest && !stencilActive && BlendSpanFast(run, first, rgba, count))
        return;
    const uint8_t mask[kChannels] = {kMaskRed, kMaskGreen, kMaskBlue, kMaskAlpha};
    // SIMD-CANDIDATE: the blend of a span, four channels per pixel.
    for (uint32_t i = 0; i < count; ++i)
    {
        const int32_t x = first + static_cast<int32_t>(i);
        const uint32_t p = rgba[i];
        const uint32_t a = p & kChannelMax;
        if (alphaTest && !Compare(ctx.alphaFunc, a, ctx.alphaRef))
            continue;
        if (stencilActive)
        {
            uint8_t& stencil = run.stencil[x];
            const bool pass =
                Compare(ctx.stencilFunc, stencil & ctx.stencilFuncMask, ctx.stencilRef & ctx.stencilFuncMask);
            const uint8_t updated = StencilResult(pass ? ctx.stencilPass : ctx.stencilFail, stencil, ctx.stencilRef);
            stencil = static_cast<uint8_t>((stencil & ~ctx.stencilWriteMask) | (updated & ctx.stencilWriteMask));
            if (!pass)
                continue;
        }
        uint8_t* dst = run.color + kChannels * static_cast<uint32_t>(x);
        const uint32_t source[kChannels] = {(p >> kPackRed) & kChannelMax, (p >> kPackGreen) & kChannelMax,
                                            (p >> kPackBlue) & kChannelMax, a};
        if (keep)
        {
        }
        else if (copy && fullMask)
        {
            for (uint32_t c = 0; c < kChannels; ++c)
                dst[c] = static_cast<uint8_t>(source[c]);
        }
        else
        {
            const uint32_t sf = Factor(ctx.blendSrc, a, dst[kChannelAlpha]);
            const uint32_t df = Factor(ctx.blendDst, a, dst[kChannelAlpha]);
            uint8_t out[kChannels];
            for (uint32_t c = 0; c < kChannels; ++c)
                out[c] = Saturate(static_cast<uint32_t>(Multiply(source[c], sf)) + Multiply(dst[c], df));
            if (fullMask)
                std::memcpy(dst, out, kChannels);
            else
                for (uint32_t c = 0; c < kChannels; ++c)
                    if (ctx.colorMask & mask[c])
                        dst[c] = out[c];
        }
        if (WritesTag(run))
            run.tag[x] = ctx.tag;
    }
}

template <LineMode Mode>
void ClearLine(LineRun& run, uint32_t mask)
{
    constexpr uint32_t kClearColor = 4, kClearStencil = 2, kClearTag = 1;
    int32_t first = 0;
    int32_t last = static_cast<int32_t>(run.width);
    if (!ScissorSpan(run, first, last))
        return;
    const GraphicsContext& ctx = run.ctx;
    const uint8_t clear[kChannels] = {static_cast<uint8_t>(ctx.clearColorRgb >> 16),
                                      static_cast<uint8_t>(ctx.clearColorRgb >> 8),
                                      static_cast<uint8_t>(ctx.clearColorRgb), ctx.clearColorA};
    const uint8_t channelMask[kChannels] = {kMaskRed, kMaskGreen, kMaskBlue, kMaskAlpha};
    constexpr uint8_t kAllChannels = kMaskRed | kMaskGreen | kMaskBlue | kMaskAlpha;
    const size_t count = static_cast<size_t>(last - first);
    // The write masks apply; alpha test, blend and stencil test do not [PG §4.21].
    if (mask & kClearColor)
    {
        uint8_t* dst = run.color + kChannels * static_cast<uint32_t>(first);
        if (ctx.colorMask == kAllChannels)
        {
            // SIMD-CANDIDATE: fill a line with one RGBA value.
            for (size_t i = 0; i < count; ++i, dst += kChannels)
                std::memcpy(dst, clear, kChannels);
        }
        else
        {
            for (size_t i = 0; i < count; ++i, dst += kChannels)
                for (uint32_t c = 0; c < kChannels; ++c)
                    if (ctx.colorMask & channelMask[c])
                        dst[c] = clear[c];
        }
    }
    if (mask & kClearStencil)
    {
        if (ctx.stencilWriteMask == kChannelMax)
            std::memset(run.stencil + first, ctx.clearStencil, count);
        else
            for (int32_t x = first; x < last; ++x)
                run.stencil[x] = static_cast<uint8_t>((run.stencil[x] & ~ctx.stencilWriteMask) |
                                                      (ctx.clearStencil & ctx.stencilWriteMask));
    }
    if ((mask & kClearTag) && WritesTag(run))
        std::memset(run.tag + first, ctx.clearTag, count);
    if (Mode == LineMode::Probe && (mask & kClearColor) && run.probeX >= first && run.probeX < last)
    {
        EvePixelSource& p = *run.probe;
        p.written = 1;
        p.commandIndex = run.commandIndex;
        p.command = run.commandWord;
        p.primitive = kPrimNone;
    }
    run.fillCost += static_cast<uint64_t>(last - first) * (kFillCostScale / kPrimitivePixelsPerClock);
}

template void Shade<LineMode::Draw>(LineRun&, int32_t, uint32_t, uint32_t, uint32_t, uint32_t);
template void Shade<LineMode::Probe>(LineRun&, int32_t, uint32_t, uint32_t, uint32_t, uint32_t);
template void ClearLine<LineMode::Draw>(LineRun&, uint32_t);
template void ClearLine<LineMode::Probe>(LineRun&, uint32_t);

} // namespace EveLib
