#pragma once

#include <cstdint>
#include <cstddef>
#include <QImage>

#include "crtprofiles.h"

/// @brief SIMD-accelerated CRT filter for software rendering path
/// Implements scanlines, phosphor mask, and color adjustments
class CRTFilter
{
public:
    CRTFilter();
    ~CRTFilter();

    /// @brief Apply CRT effects to framebuffer in-place
    /// @param pixels RGBA8888 pixel data (modified in place)
    /// @param width Frame width
    /// @param height Frame height
    /// @param params CRT profile parameters
    void apply(uint8_t* pixels, int width, int height, const CRTProfileParams& params);

    /// @brief Apply CRT effects, output to separate buffer
    /// @param src Source RGBA8888 pixels
    /// @param dst Destination buffer (must be same size)
    /// @param width Frame width (output resolution)
    /// @param height Frame height (output resolution)
    /// @param params CRT profile parameters
    void apply(const uint8_t* src, uint8_t* dst, int width, int height, const CRTProfileParams& params);

    /// @brief Apply CRT effects with source dimensions for scale-aware effects
    /// @param src Source RGBA8888 pixels (already scaled to output size)
    /// @param dst Destination buffer
    /// @param outWidth Output width
    /// @param outHeight Output height
    /// @param srcWidth Original source width (for calculating pixel scale)
    /// @param srcHeight Original source height
    /// @param params CRT profile parameters
    void apply(const uint8_t* src, uint8_t* dst, int outWidth, int outHeight,
               int srcWidth, int srcHeight, const CRTProfileParams& params);

    /// @brief Apply to QImage (creates copy with effects)
    QImage apply(const QImage& source, const CRTProfileParams& params);

private:
    // SIMD-optimized effect passes
    void applyScanlines(uint8_t* pixels, int width, int height, float weight, int srcHeight = 0);
    void applyPhosphorMaskFull(uint8_t* pixels, int width, int height, CRTMaskType maskType, float strength);
    void applyPhosphorMaskBlended(uint8_t* pixels, int width, int height, CRTMaskType maskType,
                                   float strength, float avgMaskEffect, float scaleFade);
    void applyBrightnessContrast(uint8_t* pixels, int width, int height, float brightness, float contrast);
    void applySaturation(uint8_t* pixels, int width, int height, float saturation);
    void applyGamma(uint8_t* pixels, int width, int height, float gamma);

    // Scalar fallbacks
    void applyScanlines_scalar(uint8_t* pixels, int width, int height, float weight, int srcHeight);

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    void applyScanlines_neon(uint8_t* pixels, int width, int height, float weight, int srcHeight);
    void applyBrightnessContrast_neon(uint8_t* pixels, int width, int height, float brightness, float contrast);
#endif

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
    void applyScanlines_sse2(uint8_t* pixels, int width, int height, float weight, int srcHeight);
    void applyBrightnessContrast_sse2(uint8_t* pixels, int width, int height, float brightness, float contrast);
#endif

    // Lookup tables for gamma correction
    uint8_t _gammaTable[256];
    float _currentGamma = 0.0f;
    void updateGammaTable(float gamma);
};
