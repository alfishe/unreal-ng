// The shapes of CRTBeamModel: the single-pass libretro shaders (data/shaders/crt/<key>/pass0.frag, their default
// parameters) as the model's tables. Each one names the shader's terms it follows.

#include "crtbeammodel.h"
#include "crtmath.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kPi = 3.14159265f;

float Fract(float v)
{
    return v - std::floor(v);
}

/// The phase (0..1 right of tap 0) to the shaders' distance from the nearest pixel centre, -0.5..0.5
float NearestDistance(float phase)
{
    return phase < 0.5f ? phase : phase - 1.0f;
}

/// Bilinear taps at -1, 0, +1, +2 for a position `s` (0..1) right of tap 0
void Linear(float s, float w[4])
{
    w[0] = 0.0f;
    w[1] = 1.0f - s;
    w[2] = s;
    w[3] = 0.0f;
}

/// The texel-centre sharpening of crt-pi and zfast-crt: the position moves to the nearest centre by the cube of its
/// distance (`p = i + 4 f^3`), as the weight of the lower row / right pixel
float SharpenedWeight(float phase)
{
    const float f = NearestDistance(phase);
    const float moved = 4.0f * f * f * f;
    return phase < 0.5f ? moved : 1.0f + moved;
}

/// The lanczos2 weights of taps -1, 0, +1, +2 at `x` (0..1) right of tap 0, as crt-geom and crt-easymode compute them
void Lanczos2(float x, float w[4])
{
    const float d[4] = {1.0f + x, x, 1.0f - x, 2.0f - x};
    for (int i = 0; i < 4; ++i)
    {
        const float c = std::max(std::abs(d[i] * kPi), 1e-5f);
        w[i] = CRTMath::Sin(c) * 2.0f * CRTMath::Sin(c * 0.5f) / (c * c);
    }
}

// ---- crt-lottes (Timothy Lottes): gaussian pixels and scanlines in sRGB-linear light, bloom, slot mask, warp ----

class Lottes : public CRTBeamModel::Shape
{
public:
    float toLinear(float c) const override
    {
        c *= kBrightBoost;
        return c <= 0.04045f ? c / 12.92f : CRTMath::Pow((c + 0.055f) / 1.055f, 2.4f);
    }
    float toDisplay(float v) const override
    {
        return v < 0.0031308f ? v * 12.92f : 1.055f * CRTMath::Pow(v, 0.41666f) - 0.055f;
    }
    void horizontal(float phase, float w[4]) const override
    {
        // Horz5(): the taps by exp2(hardPix * d^2), normalised (the fifth tap is past 2 pixels: under 2^-12)
        const float d[4] = {1.0f + phase, phase, 1.0f - phase, 2.0f - phase};
        for (int i = 0; i < 4; ++i)
            w[i] = Gaus(d[i], kHardPix);
    }
    bool warp(float& u, float& v) const override
    {
        const float x = u * 2.0f - 1.0f;
        const float y = v * 2.0f - 1.0f;
        u = x * (1.0f + y * y * kWarpX) * 0.5f + 0.5f;
        v = y * (1.0f + x * x * kWarpY) * 0.5f + 0.5f;
        return true;
    }
    CRTBeamModel::Vertical vertical() const override { return CRTBeamModel::Vertical::Beams; }
    float beam(float distance, float value, float rowsPerPixel) const override
    {
        (void)value;
        (void)rowsPerPixel;
        // Scan(): exp2(hardScan * d^2), not normalised; the third row is past a pixel: under 2^-8
        return Gaus(distance, kHardScan);
    }
    void mask(int x, int y, int width, int height, float rgb[3], int& state) const override
    {
        (void)width;
        (void)height;
        // shadowMask 3: an RGB triad stepping 3 subpixels per row, over 6 columns
        float p = (static_cast<float>(x) + 0.5f) * 1.00000095f + (static_cast<float>(y) + 0.5f) * 1.00000095f * 3.0f;
        p = Fract(p * 0.16666667f);
        rgb[0] = rgb[1] = rgb[2] = kMaskDark;
        rgb[p < 0.333f ? 0 : (p < 0.666f ? 1 : 2)] = kMaskLight;
        state = 0;
    }
    // Bloom(): Horz7() rows (normalised) of 5 by BloomScan() (not), exp2(hardBloom * d^2), around the source pixel
    // the beam is in
    float glow() const override { return kBloomAmount; }
    float glowColumn(int offset, float position) const override
    {
        const int inside = position >= 0.5f ? 1 : 0;
        return std::abs(offset - inside) <= 3 ? Gaus(static_cast<float>(offset) - position, kHardBloomPix) : 0.0f;
    }
    float glowRow(int offset, float position) const override
    {
        const int inside = position >= 0.5f ? 1 : 0;
        return std::abs(offset - inside) <= 2 ? Gaus(static_cast<float>(offset) - position, kHardBloomScan) : 0.0f;
    }

private:
    static float Gaus(float d, float scale) { return CRTMath::Exp2(scale * d * d); }

    static constexpr float kHardScan = -8.0f;
    static constexpr float kHardPix = -3.0f;
    static constexpr float kWarpX = 0.031f;
    static constexpr float kWarpY = 0.041f;
    static constexpr float kMaskDark = 0.5f;
    static constexpr float kMaskLight = 1.5f;
    static constexpr float kBrightBoost = 1.0f;
    static constexpr float kHardBloomPix = -1.5f;
    static constexpr float kHardBloomScan = -2.0f;
    static constexpr float kBloomAmount = 0.15f;
};

// ---- crt-gdv-mini (guest.r, Dr. Venom): rows mixed in the source gamma then squared, beams by the row's luminance ----

class GdvMini : public CRTBeamModel::Shape
{
public:
    float toLinear(float c) const override { return c; }
    float transfer(float v) const override { return v * v; }  // "color1 *= color1"
    float toDisplay(float v) const override { return std::sqrt(v); }  // gamma_out 0.5
    // The shader mixes the pixel the beam is in with the one to its left ("ur" is pC4 - dx) and puts the row's centre
    // at the pixel's top: half a pixel left and down of the usual centres
    void offset(float& x, float& y) const override
    {
        x = -0.5f;
        y = 0.5f;
    }
    bool warp(float& u, float& v) const override
    {
        // "TexCoord * 1.0001": a few hundredths of a pixel at the far edges, the sharp mix shows them
        u *= 1.0001f;
        v *= 1.0001f;
        return true;
    }
    void horizontal(float phase, float w[4]) const override
    {
        // pow(fp, h_sharp) towards the pixel, pow(1 - fp, h_sharp) towards its left neighbour
        w[0] = 0.0f;
        w[1] = CRTMath::Pow(1.0f - phase, kHSharp);
        w[2] = CRTMath::Pow(phase, kHSharp);
        w[3] = 0.0f;
    }
    CRTBeamModel::Vertical vertical() const override { return CRTBeamModel::Vertical::Beams; }
    bool beamByBrightness() const override { return true; }
    void brightness(float w[4]) const override
    {
        w[0] = 0.3f;
        w[1] = 0.6f;
        w[2] = 0.1f;
        w[3] = 0.0f;
    }
    float beam(float distance, float value, float rowsPerPixel) const override
    {
        (void)rowsPerPixel;
        // sw(): the scanline exponent grows with the distance, the beam narrows with the luminance
        const float scan = kScanline - 2.0f + 2.0f * distance;
        const float width = distance * (kBeamMin + (kBeamMax - kBeamMin) * value);
        return CRTMath::Exp2(-scan * width * width);
    }
    void mask(int x, int y, int width, int height, float rgb[3], int& state) const override
    {
        (void)y;
        (void)width;
        (void)height;
        // shadowMask 0: magenta and green columns, 1.1 lit, 1 - CGWG dark
        const bool magenta = (x & 1) == 0;
        rgb[0] = rgb[2] = magenta ? 1.1f : 1.0f - kCgwg;
        rgb[1] = magenta ? 1.0f - kCgwg : 1.1f;
        state = 0;
    }

private:
    static constexpr float kHSharp = 2.0f;
    static constexpr float kScanline = 8.0f;
    static constexpr float kBeamMin = 1.35f;
    static constexpr float kBeamMax = 1.05f;
    static constexpr float kCgwg = 0.3f;
};

// ---- crt-consumer (DariusG): converged guns in the source gamma, scanlines, a mask that fades with the light, glow ----

class Consumer : public CRTBeamModel::Shape
{
public:
    float toLinear(float c) const override { return c; }
    // "pow(color, 2.8) * 2 - pow(color, 3.6)" on the mixed colour
    float transfer(float v) const override { return 2.0f * CRTMath::Pow(v, 2.8f) - CRTMath::Pow(v, 3.6f); }
    float toDisplay(float v) const override { return CRTMath::Pow(v, 1.0f / kGammaOut); }
    bool warp(float& u, float& v) const override
    {
        // "TexCoord * 1.0001", then Warp()
        const float x = u * 1.0001f * 2.0f - 1.0f;
        const float y = v * 1.0001f * 2.0f - 1.0f;
        u = x * (1.0f + y * y * kWarpX) * 0.5f + 0.5f;
        v = y * (1.0f + x * x * kWarpY) * 0.5f + 0.5f;
        return true;
    }
    void horizontal(float phase, float w[4]) const override { horizontal(phase, 1, w); }
    void horizontal(float phase, int channel, float w[4]) const override
    {
        // Three bilinear samples of the sharpened position, blurx apart: red the right two, blue the left two
        const float s = Sharpened(phase);
        w[0] = w[1] = w[2] = w[3] = 0.0f;
        AddLinear(s + kBlurX, channel == 0 ? 0.5f : (channel == 1 ? 0.25f : 0.0f), w);
        AddLinear(s, 0.5f, w);
        AddLinear(s - kBlurX, channel == 2 ? 0.5f : (channel == 1 ? 0.25f : 0.0f), w);
    }
    CRTBeamModel::Vertical vertical() const override { return CRTBeamModel::Vertical::Interpolation; }
    void interpolation(float phase, int channel, float w[4]) const override
    {
        // The same samples blury apart, red down, blue up: bilinear over the rows, the row above or below included
        const float s = Sharpened(phase);
        w[0] = w[1] = w[2] = w[3] = 0.0f;
        AddLinear(s - kBlurY, channel == 0 ? 0.5f : (channel == 1 ? 0.25f : 0.0f), w);
        AddLinear(s, 0.5f, w);
        AddLinear(s + kBlurY, channel == 2 ? 0.5f : (channel == 1 ? 0.25f : 0.0f), w);
    }
    void brightness(float w[4]) const override
    {
        w[0] = 0.3f;
        w[1] = 0.6f;
        w[2] = 0.1f;
        w[3] = 0.0f;
    }
    float factor(int state, int channel, float phase, float bright, float rowsPerPixel) const override
    {
        (void)rowsPerPixel;
        // sw() of both rows on the one colour, then the mask: mix(mask, 1, lum1 * preserve)
        const float scan = Scan(phase, bright) + Scan(1.0f - phase, bright);
        const bool magenta = state != 0;
        const float m = (channel == 1) == magenta ? kMaskDark : 1.0f;
        return scan * (m + (1.0f - m) * bright * scan * kPreserve);
    }
    float boost(float top) const override { return kBrightBoost1 + (kBrightBoost2 - kBrightBoost1) * top; }
    void mask(int x, int y, int width, int height, float rgb[3], int& state) const override
    {
        (void)y;
        (void)width;
        (void)height;
        // Shadowmask 0: "fract(x * 0.4999) < 0.4999" magenta, else green (x = floor(TexCoord * 1.0001 * OutputSize))
        const float column = std::floor((static_cast<float>(x) + 0.5f) * 1.0001f);
        rgb[0] = rgb[1] = rgb[2] = 1.0f;
        state = Fract(column * 0.4999f) < 0.4999f ? 1 : 0;
    }
    float edge(float u, float v, int width, int height) const override
    {
        // corner0(pC4): pC4 is half a source pixel right and down of the warped position
        float x = u + 0.5f / static_cast<float>(width);
        float y = v + 0.5f / static_cast<float>(height);
        x = kCorner - std::min(std::min(x, 1.0f - x), kCorner);
        y = kCorner - std::min(std::min(y, 1.0f - y) * static_cast<float>(height) / static_cast<float>(width), kCorner);
        return std::clamp((kCorner - std::sqrt(x * x + y * y)) * kSmoothness, 0.0f, 1.0f);
    }
    // glow0(): squared samples over 7 x 7 pixels around the sharpened position, added to the display value
    float glow() const override { return kGlowStrength / (kGlow * kGlow * kGlow * kGlow); }
    bool glowSquared() const override { return true; }
    float glowPosition(float phase) const override { return Sharpened(phase); }
    bool glowInDisplay() const override { return true; }

private:
    /// PRE_SCALE: the position within a pixel to its centre region, sharpened (0..1 right of tap 0)
    static float Sharpened(float phase)
    {
        const float nearest = phase < 0.5f ? 0.0f : 1.0f;
        const float d = phase - nearest;
        const float range = 0.5f - 0.5f / kPreScale;
        return nearest + (d - std::clamp(d, -range, range)) * kPreScale;
    }
    /// A bilinear sample at `s` (from tap 0) into the four taps -1..+2, times `amount`
    static void AddLinear(float s, float amount, float w[4])
    {
        if (amount == 0.0f)
            return;
        const float base = std::floor(s);
        const int tap = static_cast<int>(base) + 1;
        const float f = s - base;
        w[std::clamp(tap, 0, 3)] += amount * (1.0f - f);
        w[std::clamp(tap + 1, 0, 3)] += amount * f;
    }
    static float Scan(float y, float l)
    {
        const float beam = kScanLow + (kScanHigh - kScanLow) * y;
        const float scan = kBeamLow + (kBeamHigh - kBeamLow) * l;
        const float ex = y * scan;
        return CRTMath::Exp2(-beam * ex * ex);
    }

    static constexpr float kWarpX = 0.03f;
    static constexpr float kWarpY = 0.04f;
    static constexpr float kBlurX = 0.25f;
    static constexpr float kBlurY = -0.1f;  // "coords.y - blury": sample1 below
    static constexpr float kPreScale = 1.5f;
    static constexpr float kScanLow = 6.0f;
    static constexpr float kScanHigh = 8.0f;
    static constexpr float kBeamLow = 1.45f;
    static constexpr float kBeamHigh = 1.05f;
    static constexpr float kMaskDark = 0.2f;
    static constexpr float kPreserve = 0.98f;
    static constexpr float kBrightBoost1 = 1.25f;
    static constexpr float kBrightBoost2 = 1.0f;
    static constexpr float kGammaOut = 2.25f;
    static constexpr float kGlow = 3.0f;
    static constexpr float kGlowStrength = 0.3f;
    static constexpr float kCorner = 0.03f;
    static constexpr float kSmoothness = 400.0f;
};

// ---- crt-geom (cgwg, Themaister, DOLLS): a raytraced spherical tube, lanczos, gaussian beams wider when brighter ----

class Geom : public CRTBeamModel::Shape
{
public:
    Geom()
    {
        // maxscale(): the tube's picture centre and its scale to fill the screen (no tilt: sin 0, cos 1)
        const float c[2] = {BackwardX(0.0f, 0.0f), BackwardY(0.0f, 0.0f)};
        const float a = 0.5f * kAspect;
        const float loX = Forward(-a, c[1], 0) / kAspect;
        const float loY = Forward(c[0], -a, 1) / kAspect;
        const float hiX = Forward(a, c[1], 0) / kAspect;
        const float hiY = Forward(c[0], a, 1) / kAspect;
        _centre[0] = (hiX + loX) * kAspect * 0.5f;
        _centre[1] = (hiY + loY) * kAspect * 0.5f;
        _scale = std::max(hiX - loX, hiY - loY);
    }

    float toLinear(float c) const override { return CRTMath::Pow(c, kCrtGamma); }
    float toDisplay(float v) const override { return CRTMath::Pow(v, 1.0f / kMonitorGamma); }
    void horizontal(float phase, float w[4]) const override { Lanczos2(phase, w); }
    bool warp(float& u, float& v) const override
    {
        // transform(): the picture position on the sphere, back to the flat source ("TexCoord * 1.00001")
        const float x = (u * 1.00001f - 0.5f) * kAspect * _scale + _centre[0];
        const float y = (v * 1.00001f - 0.5f) * kAspect * _scale + _centre[1];
        u = BackwardX(x, y) / kAspect + 0.5f;
        v = BackwardY(x, y) / kAspect + 0.5f;
        return std::isfinite(u) && std::isfinite(v);
    }
    float edge(float u, float v, int width, int height) const override
    {
        (void)width;
        (void)height;
        // corner(): rounded corners, a sharp edge (cornersmooth 1000)
        const float cx = kCornerSize - std::min(std::min(u, 1.0f - u) * kAspect, kCornerSize);
        const float cy = kCornerSize - std::min(std::min(v, 1.0f - v) * kAspect, kCornerSize);
        return std::clamp((kCornerSize - std::sqrt(cx * cx + cy * cy)) * kCornerSmooth, 0.0f, 1.0f);
    }
    CRTBeamModel::Vertical vertical() const override { return CRTBeamModel::Vertical::Beams; }
    float beam(float distance, float value, float rowsPerPixel) const override
    {
        // scanlineWeights() averaged at the distance and a third of the pixel's footprint either side
        const float spread = rowsPerPixel / 3.0f;
        return (Scanline(distance, value) + Scanline(std::abs(distance + spread), value) +
                Scanline(std::abs(distance - spread), value)) /
               3.0f;
    }
    void mask(int x, int y, int width, int height, float rgb[3], int& state) const override
    {
        (void)y;
        (void)width;
        (void)height;
        // Aperture grille (mask_type 1): magenta and green columns. With DOTMASK_brightboost 0 the "chi" term is the
        // colour itself, so a lit channel keeps it and an unlit one is (1 - DOTMASK)
        const bool green = (x & 1) != 0;
        rgb[0] = rgb[2] = green ? 1.0f - kDotMask : 1.0f;
        rgb[1] = green ? 1.0f : 1.0f - kDotMask;
        state = 0;
    }

private:
    static float Scanline(float distance, float value)
    {
        const float wid = 2.0f + 2.0f * value * value * value * value;
        const float w = distance / kScanlineWeight / std::sqrt(wid * 0.5f);
        return CRTMath::Exp(-CRTMath::Pow(w, wid)) * (kLum + 1.4f) / (0.6f + 0.2f * wid);
    }

    /// intersect() and bkwtrans() without tilt: the screen position (x, y) to the position on the flat picture
    static void Backward(float x, float y, float out[2])
    {
        const float a = x * x + y * y + kD * kD;
        const float b = 2.0f * (kR * -kD - kD * kD);
        const float c = kD * kD + 2.0f * kR * kD;
        const float hit = (-b - std::sqrt(b * b - 4.0f * a * c)) / (2.0f * a);
        const float px = hit * x / kR;
        const float py = hit * y / kR;
        const float aa = 1.0f;
        const float cc = px * px + py * py - 1.0f;
        const float along = std::sqrt(-4.0f * aa * cc) / (2.0f * aa);
        const float r = std::max(std::abs(kR * CRTMath::Acos(along)), 1e-5f);
        const float k = r / CRTMath::Sin(r / kR);
        out[0] = px * k;
        out[1] = py * k;
    }
    static float BackwardX(float x, float y)
    {
        float out[2];
        Backward(x, y, out);
        return out[0];
    }
    static float BackwardY(float x, float y)
    {
        float out[2];
        Backward(x, y, out);
        return out[1];
    }
    /// fwtrans() without tilt, component `axis`
    static float Forward(float x, float y, int axis)
    {
        const float r = std::max(std::sqrt(x * x + y * y), 1e-5f);
        const float k = CRTMath::Sin(r / kR) / r;
        const float cx = x * k;
        const float cy = y * k;
        const float d = kD / kR + (1.0f - CRTMath::Cos(r / kR));
        return (axis == 0 ? cx : cy) * kD / d;
    }

    static constexpr float kCrtGamma = 2.4f;
    static constexpr float kMonitorGamma = 2.2f;
    static constexpr float kD = 1.5f;
    static constexpr float kR = 2.0f;
    static constexpr float kCornerSize = 0.03f;
    static constexpr float kCornerSmooth = 1000.0f;
    static constexpr float kAspect = 0.75f;
    static constexpr float kDotMask = 0.3f;
    static constexpr float kScanlineWeight = 0.3f;
    static constexpr float kLum = 0.0f;

    float _centre[2] = {};
    float _scale = 1.0f;
};

// ---- crt-easymode (EasyMode): sharpened lanczos, a "dilated" square gamma, scanlines that fade with brightness ----

class Easymode : public CRTBeamModel::Shape
{
public:
    // dilate() squares the taps (DILATION 1), the pow(GAMMA_INPUT / (DILATION + 1)) after the filter is 1
    float toLinear(float c) const override { return c * c; }
    float toDisplay(float v) const override { return CRTMath::Pow(v, 1.0f / kGammaOutput) * kBrightBoost; }
    void horizontal(float phase, float w[4]) const override { Lanczos2(Curve(phase, kSharpnessH * kSharpnessH), w); }
    bool clampToCentre() const override { return true; }
    CRTBeamModel::Vertical vertical() const override { return CRTBeamModel::Vertical::Interpolation; }
    void interpolation(float phase, int channel, float w[4]) const override
    {
        (void)channel;
        Linear(Curve(phase, kSharpnessV), w);
    }
    void brightness(float w[4]) const override
    {
        // "bright = (max(col.r, col.g, col.b) + luma) / 2"
        w[0] = 0.2126f * 0.5f;
        w[1] = 0.7152f * 0.5f;
        w[2] = 0.0722f * 0.5f;
        w[3] = 0.5f;
    }
    float factor(int state, int channel, float phase, float bright, float rowsPerPixel) const override
    {
        (void)state;
        (void)channel;
        (void)rowsPerPixel;
        // "cos(uv.y * 2 pi * height)": the output row's own position, half a pixel from the phase
        const float scanBright = std::clamp(bright, kScanlineBrightMin, kScanlineBrightMax);
        const float scanBeam = std::clamp(bright * kBeamWidthMax, kBeamWidthMin, kBeamWidthMax);
        const float wave = CRTMath::Cos((phase + 0.5f) * 2.0f * kPi) * 0.5f + 0.5f;
        const float scanWeight = 1.0f - CRTMath::Pow(wave, scanBeam) * kScanlineStrength;
        return scanWeight * (1.0f - scanBright) + scanBright;
    }
    void mask(int x, int y, int width, int height, float rgb[3], int& state) const override
    {
        (void)y;
        (void)width;
        (void)height;
        rgb[0] = rgb[1] = rgb[2] = 1.0f - kMaskStrength;
        rgb[x % 3] = 1.0f;
        state = 0;
    }

private:
    /// curve_distance(): the position pulled towards the nearest pixel along a circle, by `sharp`
    static float Curve(float x, float sharp)
    {
        const float step = x >= 0.5f ? 1.0f : 0.0f;
        const float sign = x < 0.5f ? 1.0f : (x > 0.5f ? -1.0f : 0.0f);
        const float curve = 0.5f - std::sqrt(std::max(0.0f, 0.25f - (x - step) * (x - step))) * sign;
        return x + (curve - x) * sharp;
    }

    static constexpr float kSharpnessH = 0.5f;
    static constexpr float kSharpnessV = 1.0f;
    static constexpr float kMaskStrength = 0.3f;
    static constexpr float kScanlineStrength = 1.0f;
    static constexpr float kBeamWidthMin = 1.5f;
    static constexpr float kBeamWidthMax = 1.5f;
    static constexpr float kScanlineBrightMin = 0.35f;
    static constexpr float kScanlineBrightMax = 0.65f;
    static constexpr float kGammaOutput = 1.8f;
    static constexpr float kBrightBoost = 1.2f;
};

// ---- crt-pi (davej): bilinear rows sharpened to their centres, scanline weight, RGB mask, barrel ----

class Pi : public CRTBeamModel::Shape
{
public:
    float toLinear(float c) const override { return c; }
    float toDisplay(float v) const override { return v; }
    void horizontal(float phase, float w[4]) const override { Linear(phase, w); }
    bool warp(float& u, float& v) const override
    {
        // Distort(): barrel by the squared radius, scaled back to the screen; off the screen is black
        float x = u - 0.5f;
        float y = v - 0.5f;
        const float rsq = x * x + y * y;
        x = (x + x * kCurvatureX * rsq) * (1.0f - kCurvatureX * 0.23f);
        y = (y + y * kCurvatureY * rsq) * (1.0f - kCurvatureY * 0.23f);
        if (std::abs(x) >= 0.5f || std::abs(y) >= 0.5f)
            return false;
        u = x + 0.5f;
        v = y + 0.5f;
        return true;
    }
    CRTBeamModel::Vertical vertical() const override { return CRTBeamModel::Vertical::Interpolation; }
    void interpolation(float phase, int channel, float w[4]) const override
    {
        (void)channel;
        // "deltas = abs(deltas) * 2; deltas = deltas^3 * 0.5 / height * sign": 4 |d|^3 towards the next row
        const float d = NearestDistance(phase);
        const float moved = 4.0f * std::abs(d * d * d);
        Linear(phase < 0.5f ? moved : 1.0f - moved, w);
    }
    float factor(int state, int channel, float phase, float bright, float rowsPerPixel) const override
    {
        (void)state;
        (void)channel;
        (void)bright;
        // CalcScanLine(): the weight averaged over the pixel's footprint (a third of it either side), then the bloom
        const float d = NearestDistance(phase);
        const float spread = rowsPerPixel / 3.0f;
        const float weight = (Weight(d) + Weight(d - spread) + Weight(d + spread)) / 3.0f;
        return weight * kBloom;
    }
    void mask(int x, int y, int width, int height, float rgb[3], int& state) const override
    {
        (void)y;
        (void)width;
        (void)height;
        // One full channel per output column, red, green, blue; the others at MASK_BRIGHTNESS
        rgb[0] = rgb[1] = rgb[2] = kMaskBrightness;
        rgb[x % 3] = 1.0f;
        state = 0;
    }

private:
    static float Weight(float d) { return std::max(1.0f - d * d * kScanlineWeight, kScanlineGap); }

    static constexpr float kCurvatureX = 0.1f;
    static constexpr float kCurvatureY = 0.15f;
    static constexpr float kMaskBrightness = 0.7f;
    static constexpr float kScanlineWeight = 6.0f;
    static constexpr float kScanlineGap = 0.12f;
    static constexpr float kBloom = 1.5f;
};

// ---- zfast-crt (SoltanGris42, fine mask): sharpened bilinear sample, scanlines that thin out with brightness ----

class Zfast : public CRTBeamModel::Shape
{
public:
    float toLinear(float c) const override { return c; }
    float toDisplay(float v) const override { return v; }
    void horizontal(float phase, float w[4]) const override
    {
        // "p.x = mix(p.x, uv.x, BLURSCALEX)": the sharpened position blended with the plain one
        Linear((1.0f - kBlurX) * SharpenedWeight(phase) + kBlurX * phase, w);
    }
    CRTBeamModel::Vertical vertical() const override { return CRTBeamModel::Vertical::Interpolation; }
    void interpolation(float phase, int channel, float w[4]) const override
    {
        (void)channel;
        Linear(SharpenedWeight(phase), w);
    }
    void brightness(float w[4]) const override
    {
        // "dot(colour, vec3(0.3333 * MASK_FADE))"
        w[0] = w[1] = w[2] = 0.3333f * kMaskFade;
        w[3] = 0.0f;
    }
    float factor(int state, int channel, float phase, float bright, float rowsPerPixel) const override
    {
        (void)channel;
        (void)rowsPerPixel;
        const float f = NearestDistance(phase);
        const float y = f * f;
        const float yy = y * y;
        const float mask = state ? 1.0f - kMaskDark : 1.0f;
        const float low = kBrightBoost - kLowLumScan * (y - 2.05f * yy);
        const float high = 1.0f - kHighLumScan * (yy - 2.8f * yy * y);
        return low * mask + (high - low * mask) * bright;
    }
    void mask(int x, int y, int width, int height, float rgb[3], int& state) const override
    {
        (void)y;
        (void)width;
        (void)height;
        // "whichmask = fract(floor(x) * -0.4999)": the even columns are the dark ones
        rgb[0] = rgb[1] = rgb[2] = 1.0f;
        state = Fract(static_cast<float>(x) * -0.4999f) < 0.5f ? 1 : 0;
    }

private:
    static constexpr float kBlurX = 0.3f;
    static constexpr float kLowLumScan = 6.0f;
    static constexpr float kHighLumScan = 8.0f;
    static constexpr float kBrightBoost = 1.25f;
    static constexpr float kMaskDark = 0.25f;
    static constexpr float kMaskFade = 0.8f;
};
}  // namespace

std::unique_ptr<CRTBeamModel::Shape> CRTBeamModel::shapeOf(CRTProfile profile)
{
    switch (profile)
    {
        case CRTProfile::Lottes:
            return std::make_unique<Lottes>();
        case CRTProfile::Consumer:
            return std::make_unique<Consumer>();
        case CRTProfile::GdvMini:
            return std::make_unique<GdvMini>();
        case CRTProfile::Geom:
            return std::make_unique<Geom>();
        case CRTProfile::Easymode:
            return std::make_unique<Easymode>();
        case CRTProfile::Pi:
            return std::make_unique<Pi>();
        case CRTProfile::Zfast:
            return std::make_unique<Zfast>();
        default:
            return nullptr;
    }
}
