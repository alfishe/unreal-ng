#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "crtprofiles.h"
#include "crtrenderer.h"

/// @brief Integer models of the single-pass libretro CRT shaders (crt-lottes, crt-geom, crt-easymode, crt-gdv-mini,
/// crt-consumer, crt-pi, zfast-crt), one for both screen backends: the software screen composes the picture with
/// compose(), the GPU screen uploads the same tables as integer textures and runs kShader, which does the same integer
/// arithmetic. The two pictures are the same bit for bit.
///
/// Every model is one pipeline with its own tables (built with float math once per output size and profile):
/// - prepare(), once per frame at the frame's resolution: every source pixel through the input table (14 bits), and
///   the glow's columns (its kernel across the rows, or its squared samples, at every 1/kGlowPhases pixel) for the
///   models that have one;
/// - per output pixel, from the geometry table: the source pixel left of and above the beam's position and the
///   position's phase (1/kPhases of a pixel) - curvature, offsets and the source crop are in the table;
/// - horizontal filter: four taps around the position, the weights per phase and channel (lanczos, gaussian,
///   linear), clamped to 0..max or to the two middle taps;
/// - vertical: two source rows, either beams (each row by a weight from its distance and its value: per channel or
///   the row's brightness) or an interpolation per channel (of up to four rows) and then a factor per channel from
///   the phase, the mask state and the colour's brightness; the transfer table on the filtered colour before either
///   weighting;
/// - a gain from the brightest channel, the glow (linear, or added to the display value), the per-pixel factor
///   (phosphor mask, corners), then the output table back to 8 bits (gamma, brightness).
class CRTBeamModel
{
public:
    using Layout = CRTRenderer::Layout;

    static constexpr int kPhases = 128;          ///< sub-pixel positions of the beam
    static constexpr int kLinearMax = 16383;     ///< the input table's range, 14 bits
    static constexpr int kOutputMax = 32767;     ///< the output table's range: up to 2.0
    static constexpr int kUnit = 4096;           ///< 1.0 of the weights and the vertical factors
    static constexpr int kFactorUnit = 1024;     ///< 1.0 of the per-pixel factors
    static constexpr int16_t kOutside = INT16_MIN;  ///< geometry: the pixel is off the tube, black
    static constexpr int kGlowPhases = 16;  ///< x positions of the glow (its columns are a table on the CPU)
    static constexpr int kGlowTaps = 8;     ///< glow columns, from 3 left of the tap left of the beam
    static constexpr int kGlowRows = 8;     ///< glow rows, from 3 above the row above the beam
    static constexpr int kGlowFirst = -3;   ///< the first glow column and row, from the tap left of / above the beam
    static constexpr int kGlowUnit = 65536;  ///< 1.0 of the glow's row weights

    enum class Vertical
    {
        Beams,          ///< out = row0 * beam(d, row0) + row1 * beam(1 - d, row1)
        Interpolation,  ///< out = sum(row * interp(d)) * factor(mask state, channel, d, brightness)
    };

    /// The model of one profile, in floats: the tables are built from it
    class Shape
    {
    public:
        virtual ~Shape() = default;
        /// A source value 0..1 to the filters' input 0..1 (linear light, or the shader's own space)
        virtual float toLinear(float c) const = 0;
        /// The light (0..2) to the output 0..1+, clamped by the caller
        virtual float toDisplay(float v) const = 0;
        /// The filtered colour (0..1) before the beams or the scanline factor: the shaders that filter in their
        /// input's gamma and then linearise. Identity: no table
        virtual float transfer(float v) const { return v; }
        /// The four tap weights at -1, 0, +1, +2 source pixels for a position `phase` (0..1) right of tap 0
        virtual void horizontal(float phase, float weights[4]) const = 0;
        /// The same for one channel (0 r, 1 g, 2 b): the shaders that converge the guns apart
        virtual void horizontal(float phase, int channel, float weights[4]) const
        {
            (void)channel;
            horizontal(phase, weights);
        }
        /// The filtered value stays between the two middle taps (no ringing of a lanczos)
        virtual bool clampToCentre() const { return false; }
        /// The source pixels added to the beam's position: the shaders that sample half a pixel aside
        virtual void offset(float& x, float& y) const { x = y = 0.0f; }
        /// The picture position (0..1, rows top first) to the source position it shows; false: off the tube
        virtual bool warp(float& u, float& v) const
        {
            (void)u;
            (void)v;
            return true;
        }
        /// Corners and vignette at the warped source position, 0..1, of a `width` x `height` source
        virtual float edge(float u, float v, int width, int height) const
        {
            (void)u;
            (void)v;
            (void)width;
            (void)height;
            return 1.0f;
        }
        virtual Vertical vertical() const = 0;
        /// Beams: the weight of a row at `distance` (0..1 source rows) with `value` (0..1: the channel's, or the
        /// row's brightness with beamByBrightness()). `rowsPerPixel`: source rows per output pixel
        virtual float beam(float distance, float value, float rowsPerPixel) const
        {
            (void)distance;
            (void)value;
            (void)rowsPerPixel;
            return 0.0f;
        }
        /// Beams: one weight for the three channels from the row's brightness()
        virtual bool beamByBrightness() const { return false; }
        /// Interpolation: the weights of the rows at -1, 0, +1, +2 for `channel` at `phase` (0..1) below row 0. Rows
        /// -1 and +2 zero everywhere: only the two are filtered
        virtual void interpolation(float phase, int channel, float weights[4]) const
        {
            (void)channel;
            weights[0] = 0.0f;
            weights[1] = 1.0f - phase;
            weights[2] = phase;
            weights[3] = 0.0f;
        }
        /// The brightness of a colour, weights of r, g, b and max(r, g, b)
        virtual void brightness(float weights[4]) const
        {
            weights[0] = 0.2126f;
            weights[1] = 0.7152f;
            weights[2] = 0.0722f;
            weights[3] = 0.0f;
        }
        /// Interpolation: the factor of `channel` at `phase` with `bright` (0..1) under mask state `state` (0, 1)
        virtual float factor(int state, int channel, float phase, float bright, float rowsPerPixel) const
        {
            (void)state;
            (void)channel;
            (void)phase;
            (void)bright;
            (void)rowsPerPixel;
            return 1.0f;
        }
        /// The gain from the brightest channel (0..2) after the vertical stage. Identity: no table
        virtual float boost(float top) const
        {
            (void)top;
            return 1.0f;
        }
        /// The phosphor mask of output pixel (x, y) of a `width` x `height` picture: the factor per channel and a
        /// state for factor()
        virtual void mask(int x, int y, int width, int height, float rgb[3], int& state) const
        {
            (void)x;
            (void)y;
            (void)width;
            (void)height;
            rgb[0] = rgb[1] = rgb[2] = 1.0f;
            state = 0;
        }
        /// The glow: its amount (0 = none). A gaussian one sums the source through glowSource() over kGlowTaps columns
        /// (glowColumn() weights, normalised) and kGlowRows rows (glowRow() weights times the amount) around the
        /// beam; a squared one (glowSquared()) sums the squares of 7 x 7 bilinear samples one pixel apart, centred on
        /// glowPosition(), times the amount
        virtual float glow() const { return 0.0f; }
        virtual bool glowSquared() const { return false; }
        /// Where the glow is centred for the beam at `phase` (0..1 right of the tap left of it / below the row above)
        virtual float glowPosition(float phase) const { return phase; }
        /// Gaussian: the weight of glow column `offset` (kGlowFirst.. from the tap left of the beam) for the glow
        /// centred at `position` (0..1) right of that tap; glowRow() the same for the rows from the row above
        virtual float glowColumn(int offset, float position) const
        {
            (void)offset;
            (void)position;
            return 0.0f;
        }
        virtual float glowRow(int offset, float position) const
        {
            (void)offset;
            (void)position;
            return 0.0f;
        }
        /// A source value 0..1 to what the glow sums. Same as toLinear(): no table
        virtual float glowSource(float c) const { return toLinear(c); }
        /// The glow adds to the display value (after toDisplay()), the per-pixel factors after it
        virtual bool glowInDisplay() const { return false; }
    };

    /// GLSL 330 fragment shader of the composition, integer textures (see the uniforms in the GPU screen)
    static const char* const kShader;

    static bool isModel(CRTProfile profile);
    /// The model of `profile` (nullptr for None / Basic)
    static std::unique_ptr<Shape> shapeOf(CRTProfile profile);

    /// Build the tables when the layout or the profile changed. @return true when they were rebuilt
    bool setLayout(const Layout& layout, CRTProfile profile);
    const Layout& layout() const { return _layout; }

    /// The frame (`src` RGBA8, `stride` in bytes) through the input table, and the glow's columns compose() reads
    /// (`withGlow` false: not those, kShader sums them itself)
    void prepare(const uint8_t* src, int stride, bool withGlow = true);
    /// The picture, outWidth x outHeight: RGBA8, or B G R A bytes with `bgra`. Alpha 255
    void compose(uint8_t* out, int stride, bool bgra = false) const;

    // The tables, for the GPU (all tight, rows top first)
    /// frameWidth x frameHeight x RGBA uint16: the input (alpha 0)
    const std::vector<uint16_t>& frame() const { return _frame; }
    /// frameWidth x frameHeight x RGBA uint16: the input through glowSource(); empty when it is frame()
    const std::vector<uint16_t>& glowFrame() const { return _glowFrame; }
    /// outWidth x outHeight x int16: source x, source y, x phase, y phase (x = kOutside off the tube)
    const std::vector<int16_t>& geometry() const { return _geometry; }
    /// outWidth x outHeight x uint16: factor r, g, b (kFactorUnit = 1.0), mask state
    const std::vector<uint16_t>& factors() const { return _factors; }
    /// 3 rows (r, g, b) of kPhases x 4 int16: the tap weights per phase (sum kUnit)
    const std::vector<int16_t>& horizontalWeights() const { return _horizontal; }
    /// Beams: (kPhases + 1) rows of 256 uint16, [distance][value >> 6]; Interpolation: 2 x 3 x kPhases rows of 256,
    /// [(state * 3 + channel) * kPhases + phase][brightness >> 6]
    const std::vector<uint16_t>& verticalTable() const { return _vertical; }
    /// Interpolation: 3 rows (r, g, b) of kPhases x 4 int16, the weights of rows -1..+2 (sum kUnit)
    const std::vector<int16_t>& interpolationTable() const { return _interpolation; }
    /// Interpolation: rows -1 and +2 have a weight
    bool fourRows() const { return _fourRows; }
    /// (kLinearMax + 1) uint16: the transfer; empty without one
    const std::vector<uint16_t>& transferTable() const { return _transfer; }
    /// (kOutputMax + 1) / 64 uint16: the gain from the brightest channel (kUnit = 1.0); empty without one
    const std::vector<uint16_t>& boostTable() const { return _boost; }
    /// kPhases uint16: the glow's x phase (low 4 bits) and column (+ value >> 4) for a beam x phase
    const std::vector<uint16_t>& glowPhase() const { return _glowPhase; }
    /// Gaussian: kGlowPhases rows of kGlowTaps int16, the column weights per x phase (sum kUnit); empty otherwise
    const std::vector<int16_t>& glowWeightsX() const { return _glowX; }
    /// kPhases rows of kGlowRows uint16 (kGlowUnit = 1.0, the amount in them), per y phase: gaussian, the rows;
    /// squared, the rows' squares. Empty without a glow
    const std::vector<uint16_t>& glowWeightsY() const { return _glowY; }
    /// Squared: kPhases rows of kGlowRows uint16, the products of row r and r + 1 (the last one 0)
    const std::vector<uint16_t>& glowWeightsB() const { return _glowB; }
    /// (kOutputMax + 1) uint16: the light to the output, 8 bits, or with glowInDisplay() the display value
    /// (kLinearMax = 1.0) the glow adds to
    const std::vector<uint16_t>& outputTable() const { return _output; }

    Vertical vertical() const { return _verticalMode; }
    bool clampToCentre() const { return _clampToCentre; }
    bool beamByBrightness() const { return _beamByBrightness; }
    bool glowInDisplay() const { return _glowInDisplay; }
    bool glowSquared() const { return _glowSquared; }
    /// Brightness weights r, g, b, max (kUnit = 1.0)
    const int* brightnessWeights() const { return _brightness; }

private:
    void buildTables(const Shape& shape);

    Layout _layout;
    CRTProfile _profile = CRTProfile::None;
    bool _built = false;

    Vertical _verticalMode = Vertical::Beams;
    bool _clampToCentre = false;
    bool _beamByBrightness = false;
    bool _glowInDisplay = false;
    bool _glowSquared = false;
    int _brightness[4] = {};

    uint16_t _linear[256] = {};
    uint16_t _glowLinear[256] = {};
    bool _glowOwnSource = false;
    std::vector<uint16_t> _frame;
    std::vector<uint16_t> _glowFrame;
    /// The glow's columns: (source + pads) rows x columns x kGlowPhases x (gaussian: RGB0; squared: the squares RGB0,
    /// the products with the next row RGB0)
    std::vector<uint16_t> _glow;
    std::vector<int16_t> _geometry;
    std::vector<uint16_t> _factors;
    std::vector<int16_t> _horizontal;
    std::vector<uint16_t> _vertical;
    std::vector<int16_t> _interpolation;
    bool _fourRows = false;
    std::vector<uint16_t> _transfer;
    std::vector<uint16_t> _boost;
    std::vector<uint16_t> _glowPhase;
    std::vector<int16_t> _glowX;
    std::vector<uint16_t> _glowY;
    std::vector<uint16_t> _glowB;
    std::vector<uint16_t> _output;
};
