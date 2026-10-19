#pragma once

#include <QRect>
#include <cstdint>
#include <vector>

#include "crtprofiles.h"

/// @brief The CRT effect model, one for both screen backends: the software screen (DeviceScreen) composes the picture
/// with it, the GPU screen (DeviceScreenGLWindow) uploads its tables and lets a shader do the same composition. The two
/// pictures are the same bit for bit.
///
/// The picture is made in two steps:
/// - grade(): the colour of every source pixel - bloom, gamma, saturation, contrast, brightness - in integers, at the
///   source's resolution, once per frame. Both backends show the graded frame.
/// - the geometry (setLayout()): for every output pixel, the source pixel it shows (curvature, the source crop) and a
///   per-channel factor (scanline, phosphor mask, rounded corners, vignette; 128 = 1.0). Built once per output size
///   and profile, with whatever float math; the result is integers both backends read.
/// An output pixel is min(255, graded * factor / 128) per channel: compose() on the CPU, the shader kCompositionShader
/// on the GPU. CRTProfile::None is the plain picture (a nearest scale, factor 1.0) through the same tables. Every value in that product is an integer below 2^24, exact in a float, so the GPU cannot round
/// differently.
class CRTRenderer
{
public:
    /// Where the picture comes from and where it goes
    struct Layout
    {
        int outWidth = 0;   ///< output picture size, device pixels
        int outHeight = 0;
        QRect source;       ///< the shown part of the frame (the viewport crop), frame pixels
        int frameWidth = 0; ///< the whole frame
        int frameHeight = 0;

        bool operator==(const Layout& other) const
        {
            return outWidth == other.outWidth && outHeight == other.outHeight && source == other.source &&
                   frameWidth == other.frameWidth && frameHeight == other.frameHeight;
        }
        bool operator!=(const Layout& other) const { return !(*this == other); }
    };

    /// GLSL 1.20 fragment shader of the composition: frame = the graded frame, index / factor = the geometry tables,
    /// all nearest-sampled. Draw a quad over a viewport of exactly the output size
    static const char* const kCompositionShader;

    /// Build the geometry when the layout or the profile changed. @return true when the tables were rebuilt (a GPU
    /// backend uploads them again)
    bool setLayout(const Layout& layout, const CRTProfileParams& params);
    const Layout& layout() const { return _layout; }

    /// Per output pixel, RGBA8, rows top first: the source pixel (x low, x high, y low, y high bytes)
    const std::vector<uint8_t>& indexTable() const { return _index; }
    /// Per output pixel, RGBA8, rows top first: the factor per channel, 128 = 1.0 (alpha 255)
    const std::vector<uint8_t>& factorTable() const { return _factor; }

    /// The graded frame: `src` RGBA8 (alpha ignored, written 255), `stride` in bytes. Into graded() (frameWidth x
    /// frameHeight, tight). Call after setLayout() for the frame of that size
    void grade(const uint8_t* src, int stride, const CRTProfileParams& params);
    const std::vector<uint8_t>& graded() const { return _graded; }

    /// The composed picture, RGBA8, outWidth x outHeight, `stride` in bytes
    void compose(uint8_t* out, int stride) const;

private:
    void buildGeometry(const CRTProfileParams& params);
    void buildColorTables(const CRTProfileParams& params);

    Layout _layout;
    CRTProfileParams _params;
    bool _built = false;

    std::vector<uint8_t> _index;
    std::vector<uint8_t> _factor;
    std::vector<uint8_t> _graded;

    // Grading, rebuilt with the profile: per-value tables and the integer constants
    uint8_t _gammaTable[256] = {};
    uint8_t _toneTable[256] = {};  ///< contrast, then brightness
    int _saturation256 = 256;
    int _bloom256 = 0;
    int _bloomStep = 1;
    std::vector<uint16_t> _bloomRows;  ///< horizontal pass of the bloom, per channel
};
