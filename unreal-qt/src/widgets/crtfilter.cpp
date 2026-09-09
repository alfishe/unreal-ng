#include "crtfilter.h"

#include <cmath>
#include <cstring>
#include <algorithm>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define HAS_NEON 1
#endif

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#include <emmintrin.h>
#define HAS_SSE2 1
#endif

CRTFilter::CRTFilter()
{
    updateGammaTable(1.0f);
}

CRTFilter::~CRTFilter() = default;

void CRTFilter::updateGammaTable(float gamma)
{
    if (std::abs(_currentGamma - gamma) < 0.001f)
        return;

    _currentGamma = gamma;
    float invGamma = 1.0f / gamma;
    for (int i = 0; i < 256; ++i)
    {
        float normalized = i / 255.0f;
        float corrected = std::pow(normalized, invGamma);
        _gammaTable[i] = static_cast<uint8_t>(std::clamp(corrected * 255.0f, 0.0f, 255.0f));
    }
}

void CRTFilter::apply(uint8_t* pixels, int width, int height, const CRTProfileParams& params)
{
    // Legacy API without source dimensions - assume 1:1 scale (mask won't be applied)
    if (!pixels || width <= 0 || height <= 0)
        return;

    if (params.profile == CRTProfile::None)
        return;

    if (params.gamma != 1.0f)
        applyGamma(pixels, width, height, params.gamma);

    if (params.scanlineWeight > 0.001f)
        applyScanlines(pixels, width, height, params.scanlineWeight);

    // Mask requires scale >= 2.0, skip when source dimensions unknown
    // (pixelScale = 1.0 means no scaling info available)

    if (std::abs(params.saturation - 1.0f) > 0.001f)
        applySaturation(pixels, width, height, params.saturation);

    if (std::abs(params.brightness - 1.0f) > 0.001f || std::abs(params.contrast - 1.0f) > 0.001f)
        applyBrightnessContrast(pixels, width, height, params.brightness, params.contrast);
}

void CRTFilter::apply(const uint8_t* src, uint8_t* dst, int width, int height, const CRTProfileParams& params)
{
    if (!src || !dst || width <= 0 || height <= 0)
        return;

    size_t size = static_cast<size_t>(width) * height * 4;
    std::memcpy(dst, src, size);
    apply(dst, width, height, params);
}

void CRTFilter::apply(const uint8_t* src, uint8_t* dst, int outWidth, int outHeight,
                      int srcWidth, int srcHeight, const CRTProfileParams& params)
{
    if (!src || !dst || outWidth <= 0 || outHeight <= 0)
        return;

    if (params.profile == CRTProfile::None)
        return;

    size_t size = static_cast<size_t>(outWidth) * outHeight * 4;
    std::memcpy(dst, src, size);

    // Calculate pixel scale for resolution-aware effects
    float pixelScale = static_cast<float>(outWidth) / static_cast<float>(srcWidth);

    if (params.gamma != 1.0f)
        applyGamma(dst, outWidth, outHeight, params.gamma);

    if (params.scanlineWeight > 0.001f)
        applyScanlines(dst, outWidth, outHeight, params.scanlineWeight, srcHeight);

    // Phosphor mask - resolution-adaptive with smooth transition (matches GPU shader)
    // GPU blends: uniform + (patterned - uniform) * scaleFade
    if (params.maskStrength > 0.001f && params.maskType != CRTMaskType::None)
    {
        // Average mask brightness
        float avgMaskEffect = 1.0f - params.maskStrength * 0.55f;

        // Smoothstep transition: 0 at 2.5x, 1 at 3.5x
        float t = std::clamp((pixelScale - 2.5f) / 1.0f, 0.0f, 1.0f);
        float scaleFade = t * t * (3.0f - 2.0f * t);

        if (scaleFade > 0.99f)
        {
            // Full pattern at high resolution
            applyPhosphorMaskFull(dst, outWidth, outHeight, params.maskType, params.maskStrength);
        }
        else if (scaleFade > 0.01f)
        {
            // Transition zone: blend between uniform and patterned
            // For each pixel: result = uniform + (patterned - uniform) * fade
            //                       = original * (avgMaskEffect + (mask - avgMaskEffect) * fade)
            applyPhosphorMaskBlended(dst, outWidth, outHeight, params.maskType,
                                     params.maskStrength, avgMaskEffect, scaleFade);
        }
        else
        {
            // Below threshold: uniform darkening only
            applyBrightnessContrast(dst, outWidth, outHeight, avgMaskEffect, 1.0f);
        }
    }

    if (std::abs(params.saturation - 1.0f) > 0.001f)
        applySaturation(dst, outWidth, outHeight, params.saturation);

    if (std::abs(params.brightness - 1.0f) > 0.001f || std::abs(params.contrast - 1.0f) > 0.001f)
        applyBrightnessContrast(dst, outWidth, outHeight, params.brightness, params.contrast);
}

QImage CRTFilter::apply(const QImage& source, const CRTProfileParams& params)
{
    if (source.isNull())
        return source;

    QImage result = source.convertToFormat(QImage::Format_RGBA8888);
    apply(result.bits(), result.width(), result.height(), params);
    return result;
}

// ============================================================================
// Scanlines
// ============================================================================

void CRTFilter::applyScanlines(uint8_t* pixels, int width, int height, float weight, int srcHeight)
{
#if HAS_NEON
    applyScanlines_neon(pixels, width, height, weight, srcHeight);
#elif HAS_SSE2
    applyScanlines_sse2(pixels, width, height, weight, srcHeight);
#else
    applyScanlines_scalar(pixels, width, height, weight, srcHeight);
#endif
}

void CRTFilter::applyScanlines_scalar(uint8_t* pixels, int width, int height, float weight, int srcHeight)
{
    // Sine-wave scanlines matching GPU shader:
    // scanline = sin(scanY * PI / (outputSize.y / texSize.y)) * 0.5 + 0.5
    // = sin(y * PI / scale) where scale = height / srcHeight
    constexpr float PI = 3.14159265f;

    // Scale factor: how many output lines per source line
    float scale = (srcHeight > 0) ? static_cast<float>(height) / static_cast<float>(srcHeight) : 1.0f;

    for (int y = 0; y < height; ++y)
    {
        // Sine wave aligned to source pixel boundaries
        float scanline = std::sin(static_cast<float>(y) * PI / scale) * 0.5f + 0.5f;
        float factor = 1.0f - weight * (1.0f - scanline);
        int factorFixed = static_cast<int>(factor * 256);

        uint8_t* row = pixels + y * width * 4;
        for (int x = 0; x < width; ++x)
        {
            row[0] = static_cast<uint8_t>((row[0] * factorFixed) >> 8);
            row[1] = static_cast<uint8_t>((row[1] * factorFixed) >> 8);
            row[2] = static_cast<uint8_t>((row[2] * factorFixed) >> 8);
            row += 4;
        }
    }
}

#if HAS_NEON
void CRTFilter::applyScanlines_neon(uint8_t* pixels, int width, int height, float weight, int srcHeight)
{
    constexpr float PI = 3.14159265f;
    float scale = (srcHeight > 0) ? static_cast<float>(height) / static_cast<float>(srcHeight) : 1.0f;

    for (int y = 0; y < height; ++y)
    {
        float scanline = std::sin(static_cast<float>(y) * PI / scale) * 0.5f + 0.5f;
        float factor = 1.0f - weight * (1.0f - scanline);
        uint16_t factorFixed = static_cast<uint16_t>(factor * 256);
        uint16x8_t vfactor = vdupq_n_u16(factorFixed);

        uint8_t* row = pixels + y * width * 4;
        int x = 0;

        for (; x + 4 <= width; x += 4)
        {
            uint8x16_t src = vld1q_u8(row);
            uint8x8_t lo = vget_low_u8(src);
            uint8x8_t hi = vget_high_u8(src);
            uint16x8_t lo16 = vmulq_u16(vmovl_u8(lo), vfactor);
            uint16x8_t hi16 = vmulq_u16(vmovl_u8(hi), vfactor);
            uint8x8_t lo_result = vmovn_u16(vshrq_n_u16(lo16, 8));
            uint8x8_t hi_result = vmovn_u16(vshrq_n_u16(hi16, 8));
            uint8x16_t result = vcombine_u8(lo_result, hi_result);
            uint8x16_t mask = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
            result = vbslq_u8(mask, src, result);
            vst1q_u8(row, result);
            row += 16;
        }

        for (; x < width; ++x)
        {
            row[0] = static_cast<uint8_t>((row[0] * factorFixed) >> 8);
            row[1] = static_cast<uint8_t>((row[1] * factorFixed) >> 8);
            row[2] = static_cast<uint8_t>((row[2] * factorFixed) >> 8);
            row += 4;
        }
    }
}
#endif

#if HAS_SSE2
void CRTFilter::applyScanlines_sse2(uint8_t* pixels, int width, int height, float weight, int srcHeight)
{
    constexpr float PI = 3.14159265f;
    float scale = (srcHeight > 0) ? static_cast<float>(height) / static_cast<float>(srcHeight) : 1.0f;
    __m128i zero = _mm_setzero_si128();
    __m128i alphaMask = _mm_set_epi8(-1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0);

    for (int y = 0; y < height; ++y)
    {
        float scanline = std::sin(static_cast<float>(y) * PI / scale) * 0.5f + 0.5f;
        float factor = 1.0f - weight * (1.0f - scanline);
        int factorFixed = static_cast<int>(factor * 256);
        __m128i vfactor = _mm_set1_epi16(static_cast<short>(factorFixed));

        uint8_t* row = pixels + y * width * 4;
        int x = 0;

        for (; x + 4 <= width; x += 4)
        {
            __m128i src = _mm_loadu_si128(reinterpret_cast<__m128i*>(row));
            __m128i lo = _mm_unpacklo_epi8(src, zero);
            __m128i hi = _mm_unpackhi_epi8(src, zero);
            lo = _mm_srli_epi16(_mm_mullo_epi16(lo, vfactor), 8);
            hi = _mm_srli_epi16(_mm_mullo_epi16(hi, vfactor), 8);
            __m128i result = _mm_packus_epi16(lo, hi);
            result = _mm_or_si128(_mm_and_si128(src, alphaMask), _mm_andnot_si128(alphaMask, result));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(row), result);
            row += 16;
        }

        for (; x < width; ++x)
        {
            row[0] = static_cast<uint8_t>((row[0] * factorFixed) >> 8);
            row[1] = static_cast<uint8_t>((row[1] * factorFixed) >> 8);
            row[2] = static_cast<uint8_t>((row[2] * factorFixed) >> 8);
            row += 4;
        }
    }
}
#endif

// ============================================================================
// Phosphor Mask - Full strength (for high resolution)
// ============================================================================

static inline void computeApertureMask(float x, float pitch, float strength, float& rMask, float& gMask, float& bMask)
{
    float period = pitch * 3.0f;
    float stripe = std::fmod(x, period) / pitch;
    float dim = 1.0f - 0.8f * strength;  // GPU: mix(1.0, 0.2, s) = 1 - 0.8*s

    if (stripe < 1.0f) { rMask = 1.0f; gMask = dim; bMask = dim; }
    else if (stripe < 2.0f) { rMask = dim; gMask = 1.0f; bMask = dim; }
    else { rMask = dim; gMask = dim; bMask = 1.0f; }
}

void CRTFilter::applyPhosphorMaskFull(uint8_t* pixels, int width, int height, CRTMaskType maskType, float strength)
{
    float pitch = std::max(2.0f, static_cast<float>(width) / 640.0f);

    for (int y = 0; y < height; ++y)
    {
        uint8_t* row = pixels + y * width * 4;
        for (int x = 0; x < width; ++x)
        {
            float rMask = 1.0f, gMask = 1.0f, bMask = 1.0f;
            computeApertureMask(static_cast<float>(x), pitch, strength, rMask, gMask, bMask);

            row[0] = static_cast<uint8_t>(row[0] * rMask);
            row[1] = static_cast<uint8_t>(row[1] * gMask);
            row[2] = static_cast<uint8_t>(row[2] * bMask);
            row += 4;
        }
    }
}

// ============================================================================
// Phosphor Mask - Blended (for transition zone)
// GPU does: uniform + (patterned - uniform) * fade
// Which is: pixel * (avgMaskEffect + (mask - avgMaskEffect) * fade)
// ============================================================================

void CRTFilter::applyPhosphorMaskBlended(uint8_t* pixels, int width, int height, CRTMaskType maskType,
                                          float strength, float avgMaskEffect, float scaleFade)
{
    float pitch = std::max(2.0f, static_cast<float>(width) / 640.0f);

    for (int y = 0; y < height; ++y)
    {
        uint8_t* row = pixels + y * width * 4;
        for (int x = 0; x < width; ++x)
        {
            float rMask = 1.0f, gMask = 1.0f, bMask = 1.0f;
            computeApertureMask(static_cast<float>(x), pitch, strength, rMask, gMask, bMask);

            // Blend: avgMaskEffect + (mask - avgMaskEffect) * fade
            rMask = avgMaskEffect + (rMask - avgMaskEffect) * scaleFade;
            gMask = avgMaskEffect + (gMask - avgMaskEffect) * scaleFade;
            bMask = avgMaskEffect + (bMask - avgMaskEffect) * scaleFade;

            row[0] = static_cast<uint8_t>(row[0] * rMask);
            row[1] = static_cast<uint8_t>(row[1] * gMask);
            row[2] = static_cast<uint8_t>(row[2] * bMask);
            row += 4;
        }
    }
}

// ============================================================================
// Brightness / Contrast
// ============================================================================

void CRTFilter::applyBrightnessContrast(uint8_t* pixels, int width, int height, float brightness, float contrast)
{
#if HAS_NEON
    applyBrightnessContrast_neon(pixels, width, height, brightness, contrast);
#elif HAS_SSE2
    applyBrightnessContrast_sse2(pixels, width, height, brightness, contrast);
#else
    // Scalar fallback
    float offset = (1.0f - contrast) * 128.0f + (brightness - 1.0f) * 255.0f;
    size_t count = static_cast<size_t>(width) * height * 4;
    for (size_t i = 0; i < count; i += 4)
    {
        for (int c = 0; c < 3; ++c)
        {
            float v = pixels[i + c] * contrast + offset;
            pixels[i + c] = static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
        }
    }
#endif
}

#if HAS_NEON
void CRTFilter::applyBrightnessContrast_neon(uint8_t* pixels, int width, int height, float brightness, float contrast)
{
    float offset = (1.0f - contrast) * 128.0f + (brightness - 1.0f) * 255.0f;
    int contrastFixed = static_cast<int>(contrast * 256);
    int offsetFixed = static_cast<int>(offset);

    int16x8_t vcontrast = vdupq_n_s16(static_cast<int16_t>(contrastFixed));
    int16x8_t voffset = vdupq_n_s16(static_cast<int16_t>(offsetFixed));

    size_t count = static_cast<size_t>(width) * height;
    uint8_t* p = pixels;

    for (size_t i = 0; i + 4 <= count; i += 4)
    {
        uint8x16_t src = vld1q_u8(p);

        // Process as 16-bit
        int16x8_t lo = vreinterpretq_s16_u16(vmovl_u8(vget_low_u8(src)));
        int16x8_t hi = vreinterpretq_s16_u16(vmovl_u8(vget_high_u8(src)));

        // Multiply by contrast, add offset
        lo = vaddq_s16(vshrq_n_s16(vmulq_s16(lo, vcontrast), 8), voffset);
        hi = vaddq_s16(vshrq_n_s16(vmulq_s16(hi, vcontrast), 8), voffset);

        // Clamp and pack
        uint8x8_t lo_result = vqmovun_s16(lo);
        uint8x8_t hi_result = vqmovun_s16(hi);
        uint8x16_t result = vcombine_u8(lo_result, hi_result);

        // Preserve alpha
        uint8x16_t mask = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
        result = vbslq_u8(mask, src, result);

        vst1q_u8(p, result);
        p += 16;
    }

    // Handle remainder
    size_t remaining = count % 4;
    for (size_t i = 0; i < remaining; ++i)
    {
        for (int c = 0; c < 3; ++c)
        {
            float v = p[c] * contrast + offset;
            p[c] = static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
        }
        p += 4;
    }
}
#endif

#if HAS_SSE2
void CRTFilter::applyBrightnessContrast_sse2(uint8_t* pixels, int width, int height, float brightness, float contrast)
{
    float offset = (1.0f - contrast) * 128.0f + (brightness - 1.0f) * 255.0f;
    int contrastFixed = static_cast<int>(contrast * 256);
    int offsetFixed = static_cast<int>(offset);

    __m128i vcontrast = _mm_set1_epi16(static_cast<short>(contrastFixed));
    __m128i voffset = _mm_set1_epi16(static_cast<short>(offsetFixed));
    __m128i zero = _mm_setzero_si128();

    size_t count = static_cast<size_t>(width) * height;
    uint8_t* p = pixels;

    for (size_t i = 0; i + 4 <= count; i += 4)
    {
        __m128i src = _mm_loadu_si128(reinterpret_cast<__m128i*>(p));

        // Unpack to 16-bit
        __m128i lo = _mm_unpacklo_epi8(src, zero);
        __m128i hi = _mm_unpackhi_epi8(src, zero);

        // Multiply by contrast, shift, add offset
        lo = _mm_add_epi16(_mm_srli_epi16(_mm_mullo_epi16(lo, vcontrast), 8), voffset);
        hi = _mm_add_epi16(_mm_srli_epi16(_mm_mullo_epi16(hi, vcontrast), 8), voffset);

        // Pack with saturation
        __m128i result = _mm_packus_epi16(lo, hi);

        // Preserve alpha
        __m128i alphaMask = _mm_set_epi8(-1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0);
        result = _mm_or_si128(_mm_and_si128(src, alphaMask), _mm_andnot_si128(alphaMask, result));

        _mm_storeu_si128(reinterpret_cast<__m128i*>(p), result);
        p += 16;
    }

    // Handle remainder
    size_t remaining = count % 4;
    for (size_t i = 0; i < remaining; ++i)
    {
        for (int c = 0; c < 3; ++c)
        {
            float v = p[c] * contrast + offset;
            p[c] = static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
        }
        p += 4;
    }
}
#endif

// ============================================================================
// Saturation
// ============================================================================

void CRTFilter::applySaturation(uint8_t* pixels, int width, int height, float saturation)
{
    // Use luminance-preserving saturation adjustment
    float invSat = 1.0f - saturation;
    size_t count = static_cast<size_t>(width) * height * 4;

    for (size_t i = 0; i < count; i += 4)
    {
        float r = pixels[i];
        float g = pixels[i + 1];
        float b = pixels[i + 2];

        // Calculate luminance (BT.601)
        float luma = r * 0.299f + g * 0.587f + b * 0.114f;

        // Interpolate between grayscale and original
        r = luma * invSat + r * saturation;
        g = luma * invSat + g * saturation;
        b = luma * invSat + b * saturation;

        pixels[i] = static_cast<uint8_t>(std::clamp(r, 0.0f, 255.0f));
        pixels[i + 1] = static_cast<uint8_t>(std::clamp(g, 0.0f, 255.0f));
        pixels[i + 2] = static_cast<uint8_t>(std::clamp(b, 0.0f, 255.0f));
    }
}

// ============================================================================
// Gamma
// ============================================================================

void CRTFilter::applyGamma(uint8_t* pixels, int width, int height, float gamma)
{
    updateGammaTable(gamma);

    size_t count = static_cast<size_t>(width) * height * 4;
    for (size_t i = 0; i < count; i += 4)
    {
        pixels[i] = _gammaTable[pixels[i]];
        pixels[i + 1] = _gammaTable[pixels[i + 1]];
        pixels[i + 2] = _gammaTable[pixels[i + 2]];
    }
}
