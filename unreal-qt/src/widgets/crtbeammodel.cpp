#include "crtbeammodel.h"

#include "crtrows.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
constexpr int kPhases = CRTBeamModel::kPhases;
constexpr int kLinearMax = CRTBeamModel::kLinearMax;
constexpr int kOutputMax = CRTBeamModel::kOutputMax;
constexpr int kGlowPhases = CRTBeamModel::kGlowPhases;
constexpr int kGlowRows = CRTBeamModel::kGlowRows;
constexpr int kGlowFirst = CRTBeamModel::kGlowFirst;
constexpr int kGlowSamples = 7;  ///< the squared glow's samples per row and rows
constexpr int kGlowPad = 12;  ///< glow table columns and rows past the picture: farther than kReach plus the taps
constexpr int kTableMax = 32767;  ///< every table value: the products below stay under 2^31
constexpr float kReach = 6.0f;    ///< source pixels past the edge a filter or a glow still reaches in from

int Round(float v, int low, int high)
{
    return static_cast<int>(std::clamp(std::lround(v), static_cast<long>(low), static_cast<long>(high)));
}

/// Weights in floats to integers that sum to `total` exactly: the rounding error goes to the largest
template <typename T>
void ToIntegers(const float* weights, int count, int total, T* out)
{
    float sum = 0.0f;
    for (int i = 0; i < count; ++i)
        sum += weights[i];
    if (std::abs(sum) < 1e-6f)
        sum = 1.0f;
    int largest = 0;
    int rounded = 0;
    for (int i = 0; i < count; ++i)
    {
        out[i] = static_cast<T>(Round(weights[i] / sum * static_cast<float>(total), -kTableMax, kTableMax));
        rounded += out[i];
        if (std::abs(weights[i]) > std::abs(weights[largest]))
            largest = i;
    }
    out[largest] = static_cast<T>(out[largest] + (total - rounded));
}

/// The value at the centre of table bucket `index` (value >> 6), 0..1
float BucketCentre(int index)
{
    return std::min(1.0f, (static_cast<float>(index) * 64.0f + 32.0f) / static_cast<float>(kLinearMax));
}

/// Source position `p` (pixels, centres at .5 minus .5: tap 0 at floor) to the tap left of it and the phase (1/kPhases)
void SplitPosition(float p, int& tap, int& phase)
{
    tap = static_cast<int>(std::floor(p));
    phase = static_cast<int>(std::lround((p - static_cast<float>(tap)) * static_cast<float>(kPhases)));
    if (phase >= kPhases)
    {
        ++tap;
        phase = 0;
    }
}

struct Rgb
{
    int c[3];
};

// ---- The composition, the same integer arithmetic as kShader ----

struct Composer
{
    const CRTBeamModel& model;
    const uint16_t* frame;
    const uint16_t* glow;
    int frameWidth;
    QRect source;

    /// The shaders' ClampToBorder: a tap off the shown picture is black
    Rgb fetch(int x, int y) const
    {
        if (x < source.left() || x > source.right() || y < source.top() || y > source.bottom())
            return {{0, 0, 0}};
        const uint16_t* p = frame + (static_cast<size_t>(y) * frameWidth + x) * 4;
        return {{p[0], p[1], p[2]}};
    }

    /// The glow's columns around (x, y) at x phase q (`stride` values): zeros past the table (no tap of it reaches
    /// the picture)
    const uint16_t* glowColumns(int x, int y, int q, int stride) const
    {
        static const uint16_t kZero[6] = {};
        const int c = x - source.left() + kGlowPad;
        const int r = y - source.top() + kGlowPad;
        const int columns = source.width() + 2 * kGlowPad;
        if (c < 0 || c >= columns || r < 0 || r >= source.height() + 2 * kGlowPad)
            return kZero;
        return glow + ((static_cast<size_t>(r) * columns + c) * kGlowPhases + q) * stride;
    }

    /// One row through the horizontal filter, `w` the weights of the phase (3 channels x 4)
    Rgb row(int x, int y, const int16_t* w, bool centre) const
    {
        const Rgb a = fetch(x - 1, y);
        const Rgb b = fetch(x, y);
        const Rgb c = fetch(x + 1, y);
        const Rgb d = fetch(x + 2, y);
        Rgb out;
        for (int ch = 0; ch < 3; ++ch)
        {
            const int16_t* k = w + static_cast<size_t>(ch) * kPhases * 4;
            const int sum = (std::max(a.c[ch] * k[0] + b.c[ch] * k[1] + c.c[ch] * k[2] + d.c[ch] * k[3], 0) + 2048) >> 12;
            out.c[ch] = centre ? std::clamp(sum, std::min(b.c[ch], c.c[ch]), std::max(b.c[ch], c.c[ch]))
                               : std::min(sum, kLinearMax);
        }
        return out;
    }

    int brightness(const Rgb& v) const
    {
        const int* bw = model.brightnessWeights();
        const int top = std::max(v.c[0], std::max(v.c[1], v.c[2]));
        return std::clamp((v.c[0] * bw[0] + v.c[1] * bw[1] + v.c[2] * bw[2] + top * bw[3] + 2048) >> 12, 0, kLinearMax);
    }

    void transfer(Rgb& v) const
    {
        const std::vector<uint16_t>& t = model.transferTable();
        if (t.empty())
            return;
        for (int ch = 0; ch < 3; ++ch)
            v.c[ch] = t[static_cast<size_t>(v.c[ch])];
    }

    /// One output pixel: geometry g (x, y, x phase, y phase), factors f (r, g, b, state) -> the 8-bit channels
    Rgb pixel(const int16_t* g, const uint16_t* f) const
    {
        const std::vector<uint16_t>& vertical = model.verticalTable();
        const int x = g[0];
        const int y = g[1];
        const int px = g[2];
        const int py = g[3];
        const int16_t* w = &model.horizontalWeights()[static_cast<size_t>(px) * 4];
        Rgb r0 = row(x, y, w, model.clampToCentre());
        Rgb r1 = row(x, y + 1, w, model.clampToCentre());
        Rgb col;
        if (model.vertical() == CRTBeamModel::Vertical::Beams)
        {
            transfer(r0);
            transfer(r1);
            const uint16_t* b0 = &vertical[static_cast<size_t>(py) * 256];
            const uint16_t* b1 = &vertical[static_cast<size_t>(kPhases - py) * 256];
            const int bright0 = brightness(r0);
            const int bright1 = brightness(r1);
            for (int ch = 0; ch < 3; ++ch)
            {
                const int v0 = model.beamByBrightness() ? bright0 : r0.c[ch];
                const int v1 = model.beamByBrightness() ? bright1 : r1.c[ch];
                col.c[ch] = (r0.c[ch] * b0[v0 >> 6] + r1.c[ch] * b1[v1 >> 6] + 2048) >> 12;
            }
        }
        else
        {
            const int16_t* a = &model.interpolationTable()[static_cast<size_t>(py) * 4];
            const bool four = model.fourRows();
            const Rgb above = four ? row(x, y - 1, w, model.clampToCentre()) : Rgb{{0, 0, 0}};
            const Rgb below = four ? row(x, y + 2, w, model.clampToCentre()) : Rgb{{0, 0, 0}};
            for (int ch = 0; ch < 3; ++ch)
            {
                const int16_t* k = a + static_cast<size_t>(ch) * kPhases * 4;
                const int sum = above.c[ch] * k[0] + r0.c[ch] * k[1] + r1.c[ch] * k[2] + below.c[ch] * k[3];
                col.c[ch] = std::clamp((sum + 2048) >> 12, 0, kLinearMax);
            }
            transfer(col);
            const int bright = brightness(col) >> 6;
            for (int ch = 0; ch < 3; ++ch)
            {
                const size_t index = ((static_cast<size_t>(f[3]) * 3 + ch) * kPhases + py) * 256 + bright;
                col.c[ch] = (col.c[ch] * vertical[index] + 2048) >> 12;
            }
        }
        for (int ch = 0; ch < 3; ++ch)
            col.c[ch] = std::min(col.c[ch], kOutputMax);
        if (!model.boostTable().empty())
        {
            const int top = std::max(col.c[0], std::max(col.c[1], col.c[2]));
            const int gain = model.boostTable()[static_cast<size_t>(top >> 6)];
            for (int ch = 0; ch < 3; ++ch)
                col.c[ch] = std::min((col.c[ch] * gain + 2048) >> 12, kOutputMax);
        }
        Rgb glowSum{{0, 0, 0}};
        if (!model.glowWeightsY().empty())
        {
            const int phase = model.glowPhase()[static_cast<size_t>(px)];
            const int gx = x + phase / kGlowPhases;
            const int q = phase % kGlowPhases;
            const uint16_t* wy = &model.glowWeightsY()[static_cast<size_t>(py) * kGlowRows];
            const bool squared = model.glowSquared();
            const uint16_t* wb = squared ? &model.glowWeightsB()[static_cast<size_t>(py) * kGlowRows] : nullptr;
            for (int r = 0; r < kGlowRows; ++r)
            {
                const uint16_t* c = glowColumns(gx, y + r + kGlowFirst, q, squared ? 6 : 3);
                for (int ch = 0; ch < 3; ++ch)
                    glowSum.c[ch] += c[ch] * wy[r] + (squared ? c[3 + ch] * wb[r] : 0);
            }
            for (int ch = 0; ch < 3; ++ch)
                glowSum.c[ch] = (glowSum.c[ch] + CRTBeamModel::kGlowUnit / 2) >> 16;
        }
        const std::vector<uint16_t>& output = model.outputTable();
        Rgb out;
        for (int ch = 0; ch < 3; ++ch)
        {
            if (model.glowInDisplay())
            {
                // The display value (kLinearMax = 1.0) plus the glow, then the factor, to 8 bits
                const int d = output[static_cast<size_t>(col.c[ch])] + glowSum.c[ch];
                const int v = (std::min(d, kOutputMax) * f[ch] + 512) >> 10;
                out.c[ch] = std::min((v * 255 + kLinearMax / 2) / kLinearMax, 255);
            }
            else
            {
                const int v = std::min(col.c[ch] + glowSum.c[ch], kOutputMax);
                out.c[ch] = output[static_cast<size_t>(std::min((v * f[ch] + 512) >> 10, kOutputMax))];
            }
        }
        return out;
    }
};
}  // namespace

const char* const CRTBeamModel::kShader = R"(
#version 330
uniform usampler2D frame;               // the input, RGBA16UI
uniform usampler2D glowFrame;           // the glow's input (frame when the glow sums the same)
uniform isampler2D geometry;            // RGBA16I: source x, y, x phase, y phase
uniform usampler2D factors;             // RGBA16UI: factor r, g, b (1024 = 1.0), mask state
uniform isampler2D horizontalWeights;   // RGBA16I, 128 x 3: the phases, the channels
uniform usampler2D verticalTable;       // R16UI, 256 wide
uniform isampler2D interpolationTable;  // RGBA16I, 128 x 3: rows -1..+2 per phase, the channels
uniform usampler2D transferTable;       // R16UI, 256 x 64
uniform usampler2D boostTable;          // R16UI, 512 x 1
uniform usampler2D glowPhase;           // R16UI, 128 x 1: the glow's x phase and column per beam x phase
uniform isampler2D glowWeightsX;        // R16I, 8 x 16: the gaussian glow's columns per x phase
uniform usampler2D glowWeightsY;        // R16UI, 8 x 128: the glow's rows (squared: the squares) per y phase
uniform usampler2D glowWeightsB;        // R16UI, 8 x 128: the squared glow's row products per y phase
uniform usampler2D outputTable;         // R16UI, 256 x 128
uniform ivec4 sourceRect;               // the shown picture: left, top, right, bottom (inclusive)
uniform ivec2 outSize;
uniform int beams;
uniform int beamByBrightness;
uniform int fourRows;
uniform int clampToCentre;
uniform int transfer;
uniform int boost;
uniform int glow;
uniform int glowInDisplay;
uniform int glowSquared;
uniform ivec4 brightnessWeights;
in vec2 texCoord;
out vec4 fragColor;

// CRTBeamModel::compose() in GLSL: every value is an integer, every operation the same as on the CPU
ivec3 fetch(usampler2D plane, int x, int y) {
    if (x < sourceRect.x || x > sourceRect.z || y < sourceRect.y || y > sourceRect.w)
        return ivec3(0);
    return ivec3(texelFetch(plane, ivec2(x, y), 0).rgb);
}
int table(usampler2D t, int index) {
    return int(texelFetch(t, ivec2(index & 255, index >> 8), 0).r);
}
int filterTaps(int a, int b, int c, int d, ivec4 w) {
    int sum = (max(a * w.x + b * w.y + c * w.z + d * w.w, 0) + 2048) >> 12;
    if (clampToCentre != 0)
        return clamp(sum, min(b, c), max(b, c));
    return min(sum, 16383);
}
ivec3 filterRow(int x, int y, int phase) {
    ivec3 a = fetch(frame, x - 1, y);
    ivec3 b = fetch(frame, x, y);
    ivec3 c = fetch(frame, x + 1, y);
    ivec3 d = fetch(frame, x + 2, y);
    return ivec3(filterTaps(a.r, b.r, c.r, d.r, texelFetch(horizontalWeights, ivec2(phase, 0), 0)),
                 filterTaps(a.g, b.g, c.g, d.g, texelFetch(horizontalWeights, ivec2(phase, 1), 0)),
                 filterTaps(a.b, b.b, c.b, d.b, texelFetch(horizontalWeights, ivec2(phase, 2), 0)));
}
int brightness(ivec3 v) {
    ivec4 bw = brightnessWeights;
    int top = max(v.r, max(v.g, v.b));
    return clamp((v.r * bw.x + v.g * bw.y + v.b * bw.z + top * bw.w + 2048) >> 12, 0, 16383);
}
ivec3 transferred(ivec3 v) {
    if (transfer == 0)
        return v;
    return ivec3(table(transferTable, v.r), table(transferTable, v.g), table(transferTable, v.b));
}
int vertical(int value, int row) {
    return int(texelFetch(verticalTable, ivec2(value >> 6, row), 0).r);
}
void main() {
    ivec2 p = ivec2(texCoord * vec2(outSize));
    ivec4 g = texelFetch(geometry, p, 0);
    if (g.x == -32768) {
        fragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    ivec4 f = ivec4(texelFetch(factors, p, 0));
    ivec3 r0 = filterRow(g.x, g.y, g.z);
    ivec3 r1 = filterRow(g.x, g.y + 1, g.z);
    ivec3 col;
    if (beams != 0) {
        r0 = transferred(r0);
        r1 = transferred(r1);
        ivec3 v0 = beamByBrightness != 0 ? ivec3(brightness(r0)) : r0;
        ivec3 v1 = beamByBrightness != 0 ? ivec3(brightness(r1)) : r1;
        ivec3 b0 = ivec3(vertical(v0.r, g.w), vertical(v0.g, g.w), vertical(v0.b, g.w));
        ivec3 b1 = ivec3(vertical(v1.r, 128 - g.w), vertical(v1.g, 128 - g.w), vertical(v1.b, 128 - g.w));
        col = (r0 * b0 + r1 * b1 + 2048) >> 12;
    } else {
        ivec3 above = fourRows != 0 ? filterRow(g.x, g.y - 1, g.z) : ivec3(0);
        ivec3 below = fourRows != 0 ? filterRow(g.x, g.y + 2, g.z) : ivec3(0);
        ivec4 ar = texelFetch(interpolationTable, ivec2(g.w, 0), 0);
        ivec4 ag = texelFetch(interpolationTable, ivec2(g.w, 1), 0);
        ivec4 ab = texelFetch(interpolationTable, ivec2(g.w, 2), 0);
        ivec3 sum = ivec3(above.r * ar.x + r0.r * ar.y + r1.r * ar.z + below.r * ar.w,
                          above.g * ag.x + r0.g * ag.y + r1.g * ag.z + below.g * ag.w,
                          above.b * ab.x + r0.b * ab.y + r1.b * ab.z + below.b * ab.w);
        col = transferred(clamp((sum + 2048) >> 12, ivec3(0), ivec3(16383)));
        int bright = brightness(col);
        int row = f.a * 3 * 128 + g.w;
        ivec3 factor = ivec3(vertical(bright, row), vertical(bright, row + 128), vertical(bright, row + 256));
        col = (col * factor + 2048) >> 12;
    }
    col = min(col, ivec3(32767));
    if (boost != 0) {
        int gain = int(texelFetch(boostTable, ivec2(max(col.r, max(col.g, col.b)) >> 6, 0), 0).r);
        col = min((col * gain + 2048) >> 12, ivec3(32767));
    }
    ivec3 glowSum = ivec3(0);
    if (glow != 0) {
        int phase = int(texelFetch(glowPhase, ivec2(g.z, 0), 0).r);
        int gx = g.x + (phase >> 4);
        int q = phase & 15;
        if (glowSquared == 0) {
            for (int r = 0; r < 8; ++r) {
                int y = g.y + r - 3;
                ivec3 columns = ivec3(0);
                for (int t = 0; t < 8; ++t)
                    columns += fetch(glowFrame, gx + t - 3, y) * texelFetch(glowWeightsX, ivec2(t, q), 0).r;
                columns = (max(columns, ivec3(0)) + 2048) >> 12;
                glowSum += columns * int(texelFetch(glowWeightsY, ivec2(r, g.w), 0).r);
            }
        } else {
            ivec3 above[7];
            for (int r = 0; r < 8; ++r) {
                int y = g.y + r - 3;
                ivec3 squares = ivec3(0);
                ivec3 products = ivec3(0);
                ivec3 t0 = fetch(glowFrame, gx - 3, y);
                for (int i = 0; i < 7; ++i) {
                    ivec3 t1 = fetch(glowFrame, gx - 2 + i, y);
                    ivec3 h = ((16 - q) * t0 + q * t1 + 8) / 16;
                    squares += (h * h) >> 15;
                    if (r > 0)
                        products += (above[i] * h) >> 15;
                    above[i] = h;
                    t0 = t1;
                }
                glowSum += squares * int(texelFetch(glowWeightsY, ivec2(r, g.w), 0).r);
                if (r > 0)
                    glowSum += products * int(texelFetch(glowWeightsB, ivec2(r - 1, g.w), 0).r);
            }
        }
        glowSum = (glowSum + 32768) >> 16;
    }
    ivec3 o;
    if (glowInDisplay != 0) {
        ivec3 d = ivec3(table(outputTable, col.r), table(outputTable, col.g), table(outputTable, col.b)) + glowSum;
        ivec3 v = (min(d, ivec3(32767)) * f.rgb + 512) >> 10;
        o = min((v * 255 + 8191) / 16383, ivec3(255));
    } else {
        ivec3 v = min((min(col + glowSum, ivec3(32767)) * f.rgb + 512) >> 10, ivec3(32767));
        o = ivec3(table(outputTable, v.r), table(outputTable, v.g), table(outputTable, v.b));
    }
    fragColor = vec4(vec3(o) / 255.0, 1.0);
}
)";

bool CRTBeamModel::isModel(CRTProfile profile)
{
    return profile != CRTProfile::None && profile != CRTProfile::Basic;
}

bool CRTBeamModel::setLayout(const Layout& layout, CRTProfile profile)
{
    if (_built && layout == _layout && profile == _profile)
        return false;
    const std::unique_ptr<Shape> shape = shapeOf(profile);
    if (!shape)
        return false;
    _layout = layout;
    _profile = profile;
    _built = true;
    buildTables(*shape);
    return true;
}

void CRTBeamModel::buildTables(const Shape& shape)
{
    const Layout& l = _layout;

    _glowOwnSource = false;
    for (int i = 0; i < 256; ++i)
    {
        const float c = static_cast<float>(i) / 255.0f;
        _linear[i] = static_cast<uint16_t>(Round(std::clamp(shape.toLinear(c), 0.0f, 1.0f) * kLinearMax, 0, kLinearMax));
        _glowLinear[i] =
            static_cast<uint16_t>(Round(std::clamp(shape.glowSource(c), 0.0f, 1.0f) * kLinearMax, 0, kLinearMax));
        _glowOwnSource = _glowOwnSource || _glowLinear[i] != _linear[i];
    }

    _glowInDisplay = shape.glowInDisplay();
    _output.resize(static_cast<size_t>(kOutputMax) + 1);
    for (int v = 0; v <= kOutputMax; ++v)
    {
        const float display = std::max(shape.toDisplay(static_cast<float>(v) / kLinearMax), 0.0f);
        _output[static_cast<size_t>(v)] = static_cast<uint16_t>(
            _glowInDisplay ? Round(display * kLinearMax, 0, kOutputMax) : Round(std::min(display, 1.0f) * 255.0f, 0, 255));
    }

    _transfer.assign(static_cast<size_t>(kLinearMax) + 1, 0);
    bool identity = true;
    for (int v = 0; v <= kLinearMax; ++v)
    {
        const float t = shape.transfer(static_cast<float>(v) / kLinearMax);
        _transfer[static_cast<size_t>(v)] = static_cast<uint16_t>(Round(std::clamp(t, 0.0f, 1.0f) * kLinearMax, 0, kLinearMax));
        identity = identity && _transfer[static_cast<size_t>(v)] == v;
    }
    if (identity)
        _transfer.clear();

    _horizontal.resize(static_cast<size_t>(3) * kPhases * 4);
    for (int ch = 0; ch < 3; ++ch)
        for (int p = 0; p < kPhases; ++p)
        {
            float w[4];
            shape.horizontal(static_cast<float>(p) / kPhases, ch, w);
            ToIntegers(w, 4, kUnit, &_horizontal[(static_cast<size_t>(ch) * kPhases + p) * 4]);
        }
    _clampToCentre = shape.clampToCentre();

    _verticalMode = shape.vertical();
    _beamByBrightness = shape.beamByBrightness();
    const float rowsPerPixel = static_cast<float>(l.source.height()) / static_cast<float>(std::max(1, l.outHeight));
    if (_verticalMode == Vertical::Beams)
    {
        _vertical.resize(static_cast<size_t>(kPhases + 1) * 256);
        for (int d = 0; d <= kPhases; ++d)
            for (int k = 0; k < 256; ++k)
                _vertical[static_cast<size_t>(d) * 256 + k] = static_cast<uint16_t>(Round(
                    shape.beam(static_cast<float>(d) / kPhases, BucketCentre(k), rowsPerPixel) * kUnit, 0, kTableMax));
        _interpolation.clear();
        _fourRows = false;
    }
    else
    {
        _interpolation.resize(static_cast<size_t>(3) * kPhases * 4);
        _fourRows = false;
        for (int ch = 0; ch < 3; ++ch)
            for (int p = 0; p < kPhases; ++p)
            {
                float w[4];
                shape.interpolation(static_cast<float>(p) / kPhases, ch, w);
                int16_t* out = &_interpolation[(static_cast<size_t>(ch) * kPhases + p) * 4];
                ToIntegers(w, 4, kUnit, out);
                _fourRows = _fourRows || out[0] != 0 || out[3] != 0;
            }
        _vertical.resize(static_cast<size_t>(2 * 3 * kPhases) * 256);
        for (int state = 0; state < 2; ++state)
            for (int ch = 0; ch < 3; ++ch)
                for (int p = 0; p < kPhases; ++p)
                    for (int k = 0; k < 256; ++k)
                        _vertical[((static_cast<size_t>(state) * 3 + ch) * kPhases + p) * 256 + k] =
                            static_cast<uint16_t>(Round(
                                shape.factor(state, ch, static_cast<float>(p) / kPhases, BucketCentre(k), rowsPerPixel) *
                                    kUnit,
                                0, kTableMax));
    }
    float bright[4];
    shape.brightness(bright);
    for (int i = 0; i < 4; ++i)
        _brightness[i] = Round(bright[i] * kUnit, 0, 2 * kUnit);

    _boost.resize((static_cast<size_t>(kOutputMax) + 1) / 64);
    identity = true;
    for (size_t k = 0; k < _boost.size(); ++k)
    {
        const float top = (static_cast<float>(k) * 64.0f + 32.0f) / static_cast<float>(kLinearMax);
        _boost[k] = static_cast<uint16_t>(Round(shape.boost(top) * kUnit, 0, 2 * kUnit));
        identity = identity && _boost[k] == kUnit;
    }
    if (identity)
        _boost.clear();

    // The glow around the beam, as the shaders sum it: x to 1/kGlowPhases of a pixel, y to the phase. Gaussian:
    // columns normalised, rows by their weight times the amount. Squared: the samples' squares and the products of
    // neighbouring rows, by the bilinear weights of the rows ((1 - fy)^2, fy^2, 2 fy (1 - fy)) times the amount
    const float amount = shape.glow();
    _glowSquared = shape.glowSquared();
    _glow.clear();
    _glowFrame.clear();
    _glowPhase.clear();
    _glowX.clear();
    _glowY.clear();
    _glowB.clear();
    if (amount > 0.0f)
    {
        _glowPhase.resize(kPhases);
        for (int p = 0; p < kPhases; ++p)
        {
            const float position = shape.glowPosition(static_cast<float>(p) / kPhases) * kGlowPhases;
            const int q = static_cast<int>(std::lround(position));
            _glowPhase[static_cast<size_t>(p)] =
                static_cast<uint16_t>(std::clamp(q, 0, 2 * kGlowPhases - 1));  // q / kGlowPhases: the column carried
        }
        _glowY.resize(static_cast<size_t>(kPhases) * kGlowRows);
        if (_glowSquared)
        {
            // A square is (h * h) >> 15 of 14-bit samples: 1.0 squared is kLinearMax / 2
            const float scale = amount * static_cast<float>(kGlowUnit) * 32768.0f / static_cast<float>(kLinearMax);
            _glowB.resize(_glowY.size());
            for (int p = 0; p < kPhases; ++p)
            {
                const float fy = std::clamp(shape.glowPosition(static_cast<float>(p) / kPhases), 0.0f, 1.0f);
                for (int r = 0; r < kGlowRows; ++r)
                {
                    // Sample row j = -3..3 mixes rows j and j + 1 (from the row above the beam, index r = j + 3)
                    const float upper = r < kGlowSamples ? (1.0f - fy) * (1.0f - fy) : 0.0f;
                    const float lower = r >= 1 ? fy * fy : 0.0f;
                    const float both = r < kGlowSamples ? 2.0f * fy * (1.0f - fy) : 0.0f;
                    _glowY[static_cast<size_t>(p) * kGlowRows + r] =
                        static_cast<uint16_t>(Round((upper + lower) * scale, 0, 65535));
                    _glowB[static_cast<size_t>(p) * kGlowRows + r] = static_cast<uint16_t>(Round(both * scale, 0, 65535));
                }
            }
        }
        else
        {
            _glowX.resize(static_cast<size_t>(kGlowPhases) * kGlowTaps);
            for (int q = 0; q < kGlowPhases; ++q)
            {
                float w[kGlowTaps];
                for (int t = 0; t < kGlowTaps; ++t)
                    w[t] = shape.glowColumn(t + kGlowFirst, static_cast<float>(q) / kGlowPhases);
                ToIntegers(w, kGlowTaps, kUnit, &_glowX[static_cast<size_t>(q) * kGlowTaps]);
            }
            for (int p = 0; p < kPhases; ++p)
                for (int r = 0; r < kGlowRows; ++r)
                    _glowY[static_cast<size_t>(p) * kGlowRows + r] = static_cast<uint16_t>(Round(
                        shape.glowRow(r + kGlowFirst, static_cast<float>(p) / kPhases) * amount * kGlowUnit, 0, 65535));
        }
    }

    // Per output pixel: where the beam is, the mask and the edges
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    shape.offset(offsetX, offsetY);
    const size_t pixels = static_cast<size_t>(l.outWidth) * l.outHeight;
    _geometry.assign(pixels * 4, 0);
    _factors.assign(pixels * 4, 0);
    ForRows(l.outHeight, [&](int first, int end) {
        for (int y = first; y < end; ++y)
            for (int x = 0; x < l.outWidth; ++x)
            {
                const size_t i = (static_cast<size_t>(y) * l.outWidth + x) * 4;
                float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(l.outWidth);
                float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(l.outHeight);
                // Past the source the taps read black (the shaders' ClampToBorder), the filters still reach in: only
                // a beam farther than any filter or glow is off the tube
                const bool onTube = shape.warp(u, v);
                const float fx = u * static_cast<float>(l.source.width()) - 0.5f + offsetX;
                const float fy = v * static_cast<float>(l.source.height()) - 0.5f + offsetY;
                if (!onTube || !(fx > -kReach && fx < static_cast<float>(l.source.width()) + kReach) ||
                    !(fy > -kReach && fy < static_cast<float>(l.source.height()) + kReach))
                {
                    _geometry[i] = kOutside;
                    continue;
                }
                int sx, px, sy, py;
                SplitPosition(static_cast<float>(l.source.left()) + fx, sx, px);
                SplitPosition(static_cast<float>(l.source.top()) + fy, sy, py);
                _geometry[i] = static_cast<int16_t>(sx);
                _geometry[i + 1] = static_cast<int16_t>(sy);
                _geometry[i + 2] = static_cast<int16_t>(px);
                _geometry[i + 3] = static_cast<int16_t>(py);
                float rgb[3];
                int state = 0;
                shape.mask(x, y, l.outWidth, l.outHeight, rgb, state);
                const float edge = shape.edge(u, v, l.source.width(), l.source.height());
                for (int c = 0; c < 3; ++c)
                    _factors[i + c] = static_cast<uint16_t>(Round(rgb[c] * edge * kFactorUnit, 0, kTableMax));
                _factors[i + 3] = static_cast<uint16_t>(std::clamp(state, 0, 1));
            }
    });
}

void CRTBeamModel::prepare(const uint8_t* src, int stride, bool withGlow)
{
    const int w = _layout.frameWidth;
    const int h = _layout.frameHeight;
    const bool glow = !_glowY.empty();
    _frame.resize(static_cast<size_t>(w) * h * 4);
    _glowFrame.resize(glow && _glowOwnSource ? _frame.size() : 0);
    for (int y = 0; y < h; ++y)
    {
        const uint8_t* s = src + static_cast<size_t>(y) * stride;
        const size_t row = static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x, s += 4)
        {
            uint16_t* d = &_frame[row + static_cast<size_t>(x) * 4];
            d[0] = _linear[s[0]];
            d[1] = _linear[s[1]];
            d[2] = _linear[s[2]];
            d[3] = 0;
            if (!_glowFrame.empty())
            {
                uint16_t* e = &_glowFrame[row + static_cast<size_t>(x) * 4];
                e[0] = _glowLinear[s[0]];
                e[1] = _glowLinear[s[1]];
                e[2] = _glowLinear[s[2]];
                e[3] = 0;
            }
        }
    }
    if (!glow || !withGlow)
        return;

    // The glow's columns, every source row and column at every x phase: what kShader sums on the fly
    const std::vector<uint16_t>& input = _glowFrame.empty() ? _frame : _glowFrame;
    const QRect& source = _layout.source;
    const int columns = source.width() + 2 * kGlowPad;
    const int rows = source.height() + 2 * kGlowPad;
    const int values = _glowSquared ? 6 : 3;
    _glow.resize(static_cast<size_t>(rows) * columns * kGlowPhases * values);
    // Source value at (x, y), black off the picture
    auto tap = [&](int x, int y, int ch) {
        const bool inside = x >= source.left() && x <= source.right() && y >= source.top() && y <= source.bottom();
        return inside ? static_cast<int>(input[(static_cast<size_t>(y) * w + x) * 4 + ch]) : 0;
    };
    ForRows(rows, [&](int first, int end) {
        // Squared: the bilinear samples of a row at every column and x phase, [column + kGlowSamples][q][ch]
        const int span = columns + kGlowSamples;
        std::vector<int> samples[2];
        std::vector<int> squareTerms(static_cast<size_t>(span) * kGlowPhases * 3);
        std::vector<int> productTerms(squareTerms.size());
        auto sampleRow = [&](int y, std::vector<int>& out) {
            out.resize(static_cast<size_t>(span) * kGlowPhases * 3);
            for (int c = 0; c < span; ++c)
            {
                const int x = source.left() - kGlowPad + c + kGlowFirst;
                for (int ch = 0; ch < 3; ++ch)
                {
                    const int t0 = tap(x, y, ch);
                    const int t1 = tap(x + 1, y, ch);
                    for (int q = 0; q < kGlowPhases; ++q)
                        out[(static_cast<size_t>(c) * kGlowPhases + q) * 3 + ch] =
                            ((kGlowPhases - q) * t0 + q * t1 + kGlowPhases / 2) / kGlowPhases;
                }
            }
        };
        for (int r = first; r < end; ++r)
        {
            const int y = source.top() - kGlowPad + r;
            uint16_t* o = &_glow[static_cast<size_t>(r) * columns * kGlowPhases * values];
            if (_glowSquared)
            {
                // This row's samples are the last row's next ones; the sums over seven columns slide
                if (r == first)
                    sampleRow(y, samples[0]);
                sampleRow(y + 1, samples[1]);
                constexpr int kColumn = kGlowPhases * 3;
                for (size_t k = 0; k < samples[0].size(); ++k)
                {
                    squareTerms[k] = (samples[0][k] * samples[0][k]) >> 15;
                    productTerms[k] = (samples[0][k] * samples[1][k]) >> 15;
                }
                int squares[kColumn] = {};
                int products[kColumn] = {};
                for (int i = 0; i < kGlowSamples - 1; ++i)
                    for (int j = 0; j < kColumn; ++j)
                    {
                        squares[j] += squareTerms[static_cast<size_t>(i) * kColumn + j];
                        products[j] += productTerms[static_cast<size_t>(i) * kColumn + j];
                    }
                for (int c = 0; c < columns; ++c)
                {
                    const size_t enter = static_cast<size_t>(c + kGlowSamples - 1) * kColumn;
                    const size_t leave = static_cast<size_t>(c) * kColumn;
                    for (int j = 0; j < kColumn; ++j)
                    {
                        squares[j] += squareTerms[enter + j];
                        products[j] += productTerms[enter + j];
                    }
                    for (int q = 0; q < kGlowPhases; ++q, o += values)
                        for (int ch = 0; ch < 3; ++ch)
                        {
                            o[ch] = static_cast<uint16_t>(squares[q * 3 + ch]);
                            o[3 + ch] = static_cast<uint16_t>(products[q * 3 + ch]);
                        }
                    for (int j = 0; j < kColumn; ++j)
                    {
                        squares[j] -= squareTerms[leave + j];
                        products[j] -= productTerms[leave + j];
                    }
                }
                std::swap(samples[0], samples[1]);
                continue;
            }
            for (int c = 0; c < columns; ++c)
            {
                const int x = source.left() - kGlowPad + c;
                int taps[kGlowTaps][3];
                for (int t = 0; t < kGlowTaps; ++t)
                    for (int ch = 0; ch < 3; ++ch)
                        taps[t][ch] = tap(x + t + kGlowFirst, y, ch);
                for (int q = 0; q < kGlowPhases; ++q, o += values)
                {
                    const int16_t* wx = &_glowX[static_cast<size_t>(q) * kGlowTaps];
                    for (int ch = 0; ch < 3; ++ch)
                    {
                        int sum = 0;
                        for (int t = 0; t < kGlowTaps; ++t)
                            sum += taps[t][ch] * wx[t];
                        o[ch] = static_cast<uint16_t>((std::max(sum, 0) + 2048) >> 12);
                    }
                }
            }
        }
    });
}

void CRTBeamModel::compose(uint8_t* out, int stride, bool bgra) const
{
    const Layout& l = _layout;
    const Composer composer{*this, _frame.data(), _glow.data(), l.frameWidth, l.source};
    const int red = bgra ? 2 : 0;
    const int blue = bgra ? 0 : 2;
    ForRows(l.outHeight, [&](int first, int end) {
        for (int y = first; y < end; ++y)
        {
            uint8_t* o = out + static_cast<size_t>(y) * stride;
            const size_t row = static_cast<size_t>(y) * l.outWidth * 4;
            for (int x = 0; x < l.outWidth; ++x, o += 4)
            {
                const int16_t* g = &_geometry[row + static_cast<size_t>(x) * 4];
                if (g[0] == kOutside)
                {
                    o[0] = o[1] = o[2] = 0;
                    o[3] = 255;
                    continue;
                }
                const Rgb c = composer.pixel(g, &_factors[row + static_cast<size_t>(x) * 4]);
                o[red] = static_cast<uint8_t>(c.c[0]);
                o[1] = static_cast<uint8_t>(c.c[1]);
                o[blue] = static_cast<uint8_t>(c.c[2]);
                o[3] = 255;
            }
        }
    });
}
