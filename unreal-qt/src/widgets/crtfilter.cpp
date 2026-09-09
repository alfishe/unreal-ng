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
    if (!pixels || width <= 0 || height <= 0)
        return;

    if (params.profile == CRTProfile::None)
        return;

    // Apply effects in order
    if (params.gamma != 1.0f)
        applyGamma(pixels, width, height, params.gamma);

    if (params.scanlineWeight > 0.001f)
        applyScanlines(pixels, width, height, params.scanlineWeight);

    if (params.maskStrength > 0.001f && params.maskType != CRTMaskType::None)
        applyPhosphorMask(pixels, width, height, params.maskType, params.maskStrength);

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

void CRTFilter::applyScanlines(uint8_t* pixels, int width, int height, float weight)
{
#if HAS_NEON
    applyScanlines_neon(pixels, width, height, weight);
#elif HAS_SSE2
    applyScanlines_sse2(pixels, width, height, weight);
#else
    applyScanlines_scalar(pixels, width, height, weight);
#endif
}

void CRTFilter::applyScanlines_scalar(uint8_t* pixels, int width, int height, float weight)
{
    // Simple alternating scanlines: darken every other line
    // Matches GPU visual appearance better than sine wave at typical scales
    float darkFactor = 1.0f - weight;
    int darkFactorFixed = static_cast<int>(darkFactor * 256);

    for (int y = 1; y < height; y += 2)
    {
        uint8_t* row = pixels + y * width * 4;
        for (int x = 0; x < width; ++x)
        {
            row[0] = static_cast<uint8_t>((row[0] * darkFactorFixed) >> 8);
            row[1] = static_cast<uint8_t>((row[1] * darkFactorFixed) >> 8);
            row[2] = static_cast<uint8_t>((row[2] * darkFactorFixed) >> 8);
            row += 4;
        }
    }
}

#if HAS_NEON
void CRTFilter::applyScanlines_neon(uint8_t* pixels, int width, int height, float weight)
{
    float darkFactor = 1.0f - weight;
    uint16_t darkFactorFixed = static_cast<uint16_t>(darkFactor * 256);
    uint16x8_t vfactor = vdupq_n_u16(darkFactorFixed);

    for (int y = 1; y < height; y += 2)
    {
        uint8_t* row = pixels + y * width * 4;
        int x = 0;

        // Process 4 pixels (16 bytes) at a time
        for (; x + 4 <= width; x += 4)
        {
            uint8x16_t src = vld1q_u8(row);

            uint8x8_t lo = vget_low_u8(src);
            uint8x8_t hi = vget_high_u8(src);

            uint16x8_t lo16 = vmovl_u8(lo);
            uint16x8_t hi16 = vmovl_u8(hi);

            lo16 = vmulq_u16(lo16, vfactor);
            hi16 = vmulq_u16(hi16, vfactor);

            lo16 = vshrq_n_u16(lo16, 8);
            hi16 = vshrq_n_u16(hi16, 8);

            uint8x8_t lo_result = vmovn_u16(lo16);
            uint8x8_t hi_result = vmovn_u16(hi16);

            uint8x16_t result = vcombine_u8(lo_result, hi_result);

            // Preserve alpha channel
            uint8x16_t mask = {0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
            result = vbslq_u8(mask, src, result);

            vst1q_u8(row, result);
            row += 16;
        }

        // Scalar remainder
        for (; x < width; ++x)
        {
            row[0] = static_cast<uint8_t>((row[0] * darkFactorFixed) >> 8);
            row[1] = static_cast<uint8_t>((row[1] * darkFactorFixed) >> 8);
            row[2] = static_cast<uint8_t>((row[2] * darkFactorFixed) >> 8);
            row += 4;
        }
    }
}
#endif

#if HAS_SSE2
void CRTFilter::applyScanlines_sse2(uint8_t* pixels, int width, int height, float weight)
{
    float darkFactor = 1.0f - weight;
    int darkFactorFixed = static_cast<int>(darkFactor * 256);
    __m128i vfactor = _mm_set1_epi16(static_cast<short>(darkFactorFixed));
    __m128i zero = _mm_setzero_si128();
    __m128i alphaMask = _mm_set_epi8(-1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 0, 0);

    for (int y = 1; y < height; y += 2)
    {
        uint8_t* row = pixels + y * width * 4;
        int x = 0;

        // Process 4 pixels (16 bytes) at a time
        for (; x + 4 <= width; x += 4)
        {
            __m128i src = _mm_loadu_si128(reinterpret_cast<__m128i*>(row));

            __m128i lo = _mm_unpacklo_epi8(src, zero);
            __m128i hi = _mm_unpackhi_epi8(src, zero);

            lo = _mm_mullo_epi16(lo, vfactor);
            hi = _mm_mullo_epi16(hi, vfactor);
            lo = _mm_srli_epi16(lo, 8);
            hi = _mm_srli_epi16(hi, 8);

            __m128i result = _mm_packus_epi16(lo, hi);
            result = _mm_or_si128(_mm_and_si128(src, alphaMask), _mm_andnot_si128(alphaMask, result));

            _mm_storeu_si128(reinterpret_cast<__m128i*>(row), result);
            row += 16;
        }

        // Scalar remainder
        for (; x < width; ++x)
        {
            row[0] = static_cast<uint8_t>((row[0] * darkFactorFixed) >> 8);
            row[1] = static_cast<uint8_t>((row[1] * darkFactorFixed) >> 8);
            row[2] = static_cast<uint8_t>((row[2] * darkFactorFixed) >> 8);
            row += 4;
        }
    }
}
#endif

// ============================================================================
// Phosphor Mask
// ============================================================================

void CRTFilter::applyPhosphorMask(uint8_t* pixels, int width, int height, CRTMaskType maskType, float strength)
{
#if HAS_NEON
    applyPhosphorMask_neon(pixels, width, height, maskType, strength);
#elif HAS_SSE2
    applyPhosphorMask_sse2(pixels, width, height, maskType, strength);
#else
    applyPhosphorMask_scalar(pixels, width, height, maskType, strength);
#endif
}

void CRTFilter::applyPhosphorMask_scalar(uint8_t* pixels, int width, int height, CRTMaskType maskType, float strength)
{
    // Aperture grille: vertical RGB stripes
    // Shadow mask: 2x2 RGB pattern
    // Slot mask: 3x2 RGB pattern

    float invStrength = 1.0f - strength;

    for (int y = 0; y < height; ++y)
    {
        uint8_t* row = pixels + y * width * 4;

        for (int x = 0; x < width; ++x)
        {
            float rMask = 1.0f, gMask = 1.0f, bMask = 1.0f;

            switch (maskType)
            {
                case CRTMaskType::Aperture:
                {
                    // Vertical RGB stripes (3-pixel period)
                    int phase = x % 3;
                    if (phase == 0) { rMask = 1.0f; gMask = invStrength; bMask = invStrength; }
                    else if (phase == 1) { rMask = invStrength; gMask = 1.0f; bMask = invStrength; }
                    else { rMask = invStrength; gMask = invStrength; bMask = 1.0f; }
                    break;
                }
                case CRTMaskType::ShadowMask:
                {
                    // 2x2 RGB dot pattern
                    int px = x % 3;
                    int py = y % 2;
                    if (py == 0)
                    {
                        if (px == 0) { rMask = 1.0f; gMask = invStrength; bMask = invStrength; }
                        else if (px == 1) { rMask = invStrength; gMask = 1.0f; bMask = invStrength; }
                        else { rMask = invStrength; gMask = invStrength; bMask = 1.0f; }
                    }
                    else
                    {
                        // Offset by 1.5 pixels
                        px = (x + 1) % 3;
                        if (px == 0) { rMask = invStrength; gMask = invStrength; bMask = 1.0f; }
                        else if (px == 1) { rMask = 1.0f; gMask = invStrength; bMask = invStrength; }
                        else { rMask = invStrength; gMask = 1.0f; bMask = invStrength; }
                    }
                    break;
                }
                case CRTMaskType::SlotMask:
                {
                    // Slot mask: 3x3 pattern with slots
                    int px = x % 6;
                    int py = y % 2;
                    float slot = ((px / 2) == 1) ? invStrength * 0.5f : 1.0f;
                    int phase = px % 3;
                    if (py == 0)
                    {
                        if (phase == 0) { rMask = slot; gMask = invStrength; bMask = invStrength; }
                        else if (phase == 1) { rMask = invStrength; gMask = slot; bMask = invStrength; }
                        else { rMask = invStrength; gMask = invStrength; bMask = slot; }
                    }
                    else
                    {
                        phase = (px + 1) % 3;
                        if (phase == 0) { rMask = invStrength; gMask = invStrength; bMask = slot; }
                        else if (phase == 1) { rMask = slot; gMask = invStrength; bMask = invStrength; }
                        else { rMask = invStrength; gMask = slot; bMask = invStrength; }
                    }
                    break;
                }
                default:
                    break;
            }

            row[0] = static_cast<uint8_t>(row[0] * rMask);
            row[1] = static_cast<uint8_t>(row[1] * gMask);
            row[2] = static_cast<uint8_t>(row[2] * bMask);
            row += 4;
        }
    }
}

#if HAS_NEON
void CRTFilter::applyPhosphorMask_neon(uint8_t* pixels, int width, int height, CRTMaskType maskType, float strength)
{
    // For aperture grille, we can process 3 pixels at a time (12 bytes RGB, 16 with alpha)
    // But pattern alignment is tricky - use scalar for now with NEON color multiply
    applyPhosphorMask_scalar(pixels, width, height, maskType, strength);
}
#endif

#if HAS_SSE2
void CRTFilter::applyPhosphorMask_sse2(uint8_t* pixels, int width, int height, CRTMaskType maskType, float strength)
{
    // Pattern-aligned SIMD is complex - use scalar for correctness
    applyPhosphorMask_scalar(pixels, width, height, maskType, strength);
}
#endif

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
