#include "crtrenderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace
{
constexpr float kPi = 3.14159265f;

/// Run fn(firstRow, endRow) over [0, rows) on a few threads: the tables and the composition are per row
template <typename Fn>
void ForRows(int rows, const Fn& fn)
{
    const int hardware = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
    const int parts = std::min({hardware, 8, std::max(1, rows / 64)});
    if (parts <= 1)
    {
        fn(0, rows);
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(static_cast<size_t>(parts - 1));
    const int chunk = (rows + parts - 1) / parts;
    for (int part = 1; part < parts; ++part)
    {
        const int first = part * chunk;
        const int end = std::min(rows, first + chunk);
        if (first < end)
            threads.emplace_back([&fn, first, end]() { fn(first, end); });
    }
    fn(0, std::min(rows, chunk));
    for (std::thread& thread : threads)
        thread.join();
}

float SmoothStep(float edge0, float edge1, float x)
{
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// Barrel distortion of the tube: uv in 0..1 to the frame position it shows
void Curve(float& u, float& v, float curvature)
{
    if (curvature < 0.001f)
        return;
    float x = (u - 0.5f) * 2.0f;
    float y = (v - 0.5f) * 2.0f;
    const float k = std::sqrt(x * x + y * y) * curvature;
    x *= 1.0f + k * k;
    y *= 1.0f + k * k;
    u = x * 0.5f + 0.5f;
    v = y * 0.5f + 0.5f;
}

float CornerMask(float u, float v, float radius)
{
    if (radius < 0.001f)
        return 1.0f;
    const float dx = std::max(std::abs(u - 0.5f) * 2.0f - (1.0f - radius), 0.0f);
    const float dy = std::max(std::abs(v - 0.5f) * 2.0f - (1.0f - radius), 0.0f);
    return 1.0f - SmoothStep(radius - 0.01f, radius, std::sqrt(dx * dx + dy * dy));
}

/// Phosphor mask at an output pixel position: per channel, mixed toward 1 by the strength
void Mask(CRTMaskType type, float px, float py, float pitch, float strength, float rgb[3])
{
    float m[3] = {1.0f, 1.0f, 1.0f};
    float lift = 0.0f;
    switch (type)
    {
        case CRTMaskType::Aperture:
        {
            const float x = std::fmod(px, pitch * 3.0f);
            const int stripe = x < pitch ? 0 : (x < pitch * 2.0f ? 1 : 2);
            for (int c = 0; c < 3; ++c)
                m[c] = c == stripe ? 1.0f : 0.2f;
            break;
        }
        case CRTMaskType::ShadowMask:
        {
            const float cellY = std::floor(py / pitch);
            const float offsetX = std::fmod(px, pitch) / pitch;
            const float offsetY = std::fmod(py, pitch) / pitch;
            const float phase = std::fmod(cellY, 2.0f) * 0.5f;
            const float x = std::fmod(offsetX + phase, 1.0f);
            const int dot = x < 0.333f ? 0 : (x < 0.666f ? 1 : 2);
            const float dist = std::sqrt((offsetX - 0.5f) * (offsetX - 0.5f) + (offsetY - 0.5f) * (offsetY - 0.5f)) * 2.0f;
            const float falloff = SmoothStep(1.0f, 0.5f, dist);
            for (int c = 0; c < 3; ++c)
                m[c] = (c == dot ? 1.0f : 0.1f) * falloff;
            lift = 0.3f;
            break;
        }
        case CRTMaskType::SlotMask:
        {
            const float slotHeight = pitch * 1.5f;
            const float cellY = std::floor(py / slotHeight);
            const float offsetY = std::fmod(py, slotHeight);
            const float phase = std::fmod(cellY, 2.0f) * pitch * 1.5f;
            const float x = std::fmod(std::fmod(px, pitch * 3.0f) + phase, pitch * 3.0f);
            const int slot = x < pitch ? 0 : (x < pitch * 2.0f ? 1 : 2);
            const float gap = SmoothStep(0.0f, 0.15f, offsetY / slotHeight) * SmoothStep(1.0f, 0.85f, offsetY / slotHeight);
            for (int c = 0; c < 3; ++c)
                m[c] = (c == slot ? 1.0f : 0.15f) * gap;
            lift = 0.2f;
            break;
        }
        case CRTMaskType::None:
            break;
    }
    for (int c = 0; c < 3; ++c)
        rgb[c] = type == CRTMaskType::None ? 1.0f : 1.0f + (m[c] + lift - 1.0f) * strength;
}

bool SameParams(const CRTProfileParams& a, const CRTProfileParams& b)
{
    return a.profile == b.profile && a.curvature == b.curvature && a.cornerRadius == b.cornerRadius &&
           a.scanlineWeight == b.scanlineWeight && a.maskType == b.maskType && a.maskStrength == b.maskStrength &&
           a.maskDotPitch == b.maskDotPitch && a.bloomStrength == b.bloomStrength && a.bloomRadius == b.bloomRadius &&
           a.saturation == b.saturation && a.brightness == b.brightness && a.contrast == b.contrast &&
           a.gamma == b.gamma;
}

uint8_t ToByte(float v)
{
    return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
}
}  // namespace

const char* const CRTRenderer::kCompositionShader = R"(
#version 120
uniform sampler2D frame;
uniform sampler2D indexTable;
uniform sampler2D factorTable;
uniform vec2 frameSize;
uniform vec2 outSize;
uniform vec2 outOrigin;
void main() {
    // Output pixel centre, rows top first; every value below is an integer, exact in a float
    vec2 p = gl_FragCoord.xy - outOrigin;
    vec2 uv = vec2(p.x, outSize.y - p.y) / outSize;
    vec4 i = floor(texture2D(indexTable, uv) * 255.0 + 0.5);
    vec2 s = vec2(i.r + i.g * 256.0, i.b + i.a * 256.0);
    vec3 c = floor(texture2D(frame, (s + 0.5) / frameSize).rgb * 255.0 + 0.5);
    vec3 f = floor(texture2D(factorTable, uv).rgb * 255.0 + 0.5);
    gl_FragColor = vec4(min(floor(c * f / 128.0), 255.0) / 255.0, 1.0);
}
)";

bool CRTRenderer::setLayout(const Layout& layout, const CRTProfileParams& params)
{
    if (_built && layout == _layout && SameParams(params, _params))
        return false;
    const bool colorChanged = !_built || !SameParams(params, _params);
    _layout = layout;
    _params = params;
    _built = true;
    _plain = params.profile == CRTProfile::None;
    if (colorChanged)
        buildColorTables(params);
    buildGeometry(params);
    return true;
}

void CRTRenderer::buildColorTables(const CRTProfileParams& params)
{
    for (int i = 0; i < 256; ++i)
    {
        const float v = static_cast<float>(i) / 255.0f;
        _gammaTable[i] = ToByte(std::pow(v, params.gamma) * 255.0f);
        _toneTable[i] = ToByte(((v - 0.5f) * params.contrast + 0.5f) * params.brightness * 255.0f);
    }
    _saturation256 = static_cast<int>(std::lround(params.saturation * 256.0f));
    _bloom256 = static_cast<int>(std::lround(params.bloomStrength * 256.0f));
    _bloomStep = std::max(1, static_cast<int>(std::lround(params.bloomRadius)));
}

void CRTRenderer::buildGeometry(const CRTProfileParams& params)
{
    const int width = _layout.outWidth;
    const int height = _layout.outHeight;
    const QRect source = _layout.source;
    const size_t pixels = static_cast<size_t>(std::max(width, 0)) * static_cast<size_t>(std::max(height, 0));
    _index.assign(pixels * 4, 0);
    _factor.assign(pixels * 4, 0);  // outside the tube: factor 0, black
    if (pixels == 0 || source.isEmpty())
        return;

    // No profile: the plain picture, a nearest scale - drawn by the same tables, so the backends agree on it too
    const bool plain = params.profile == CRTProfile::None;
    const float pitch = params.maskDotPitch >= 1.0f ? params.maskDotPitch : std::max(1.0f, width / 640.0f);
    // Below 2.5x the mask's pattern aliases: it fades to its average, full from 3.5x
    const float maskFade = SmoothStep(2.5f, 3.5f, static_cast<float>(width) / static_cast<float>(source.width()));

    ForRows(height, [&](int firstRow, int endRow) {
        for (int y = firstRow; y < endRow; ++y)
        {
            uint8_t* index = _index.data() + static_cast<size_t>(y) * width * 4;
            uint8_t* factor = _factor.data() + static_cast<size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x, index += 4, factor += 4)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
                float cu = u;
                float cv = v;
                if (!plain)
                    Curve(cu, cv, params.curvature);
                if (cu < 0.0f || cu >= 1.0f || cv < 0.0f || cv >= 1.0f)
                    continue;

                const int sx = source.x() + std::min(static_cast<int>(cu * source.width()), source.width() - 1);
                const int sy = source.y() + std::min(static_cast<int>(cv * source.height()), source.height() - 1);
                index[0] = static_cast<uint8_t>(sx & 0xFF);
                index[1] = static_cast<uint8_t>(sx >> 8);
                index[2] = static_cast<uint8_t>(sy & 0xFF);
                index[3] = static_cast<uint8_t>(sy >> 8);

                if (plain)
                {
                    factor[0] = factor[1] = factor[2] = 128;
                    factor[3] = 255;
                    continue;
                }

                float shade = 1.0f;
                if (params.scanlineWeight > 0.001f)
                {
                    // Bright at a source line's centre, dark between lines
                    const float line = cv * source.height();
                    const float beam = std::sin(kPi * (line - std::floor(line)));
                    shade *= 1.0f - params.scanlineWeight * (1.0f - beam);
                }
                shade *= CornerMask(cu, cv, params.cornerRadius);
                const float r = std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 1.1f;
                shade *= 1.0f - r * r * 0.15f;  // vignette

                float mask[3];
                Mask(params.maskType, static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, pitch,
                     params.maskStrength, mask);
                const float average = (mask[0] + mask[1] + mask[2]) / 3.0f;
                for (int c = 0; c < 3; ++c)
                    factor[c] = ToByte((average + (mask[c] - average) * maskFade) * shade * 128.0f);
                factor[3] = 255;
            }
        }
    });
}

void CRTRenderer::grade(const uint8_t* src, int stride, const CRTProfileParams& params)
{
    (void)params;  // the tables setLayout() built
    const int width = _layout.frameWidth;
    const int height = _layout.frameHeight;
    _graded.resize(static_cast<size_t>(std::max(width, 0)) * static_cast<size_t>(std::max(height, 0)) * 4);
    if (width <= 0 || height <= 0 || !src)
        return;

    // The plain picture's grading is the identity: a copy (the alpha byte is never read)
    if (_plain)
    {
        for (int y = 0; y < height; ++y)
            std::memcpy(_graded.data() + static_cast<size_t>(y) * width * 4, src + static_cast<size_t>(y) * stride,
                        static_cast<size_t>(width) * 4);
        return;
    }

    // Bloom: a separable binomial blur (1 4 6 4 1, taps _bloomStep apart), added by its strength
    const int step = _bloomStep;
    if (_bloom256 > 0)
    {
        _bloomRows.resize(static_cast<size_t>(width) * height * 3);
        ForRows(height, [&](int firstRow, int endRow) {
            for (int y = firstRow; y < endRow; ++y)
            {
                const uint8_t* row = src + static_cast<size_t>(y) * stride;
                uint16_t* out = _bloomRows.data() + static_cast<size_t>(y) * width * 3;
                for (int x = 0; x < width; ++x)
                {
                    // Taps inside the row need no clamp: only the edges do
                    const bool inside = x >= 2 * step && x < width - 2 * step;
                    int tap[5];
                    for (int t = 0; t < 5; ++t)
                        tap[t] = (inside ? x + (t - 2) * step : std::clamp(x + (t - 2) * step, 0, width - 1)) * 4;
                    for (int c = 0; c < 3; ++c)
                        out[x * 3 + c] = static_cast<uint16_t>(row[tap[0] + c] + 4 * row[tap[1] + c] +
                                                               6 * row[tap[2] + c] + 4 * row[tap[3] + c] +
                                                               row[tap[4] + c]);  // <= 255 * 16
                }
            }
        });
    }

    ForRows(height, [&](int firstRow, int endRow) {
        for (int y = firstRow; y < endRow; ++y)
        {
            const uint8_t* row = src + static_cast<size_t>(y) * stride;
            uint8_t* out = _graded.data() + static_cast<size_t>(y) * width * 4;
            const uint16_t* bloom[5] = {};
            if (_bloom256 > 0)
            {
                for (int t = 0; t < 5; ++t)
                {
                    const int sy = std::clamp(y + (t - 2) * step, 0, height - 1);
                    bloom[t] = _bloomRows.data() + static_cast<size_t>(sy) * width * 3;
                }
            }
            for (int x = 0; x < width; ++x, out += 4)
            {
                int c[3];
                for (int k = 0; k < 3; ++k)
                {
                    int v = row[x * 4 + k];
                    if (_bloom256 > 0)
                    {
                        const size_t i = static_cast<size_t>(x) * 3 + k;
                        const int sum = bloom[0][i] + 4 * bloom[1][i] + 6 * bloom[2][i] + 4 * bloom[3][i] + bloom[4][i];
                        v = std::min(255, v + (((sum >> 8) * _bloom256) >> 8));
                    }
                    c[k] = _gammaTable[v];
                }
                if (_saturation256 != 256)
                {
                    const int luma = (77 * c[0] + 150 * c[1] + 29 * c[2] + 128) >> 8;
                    for (int k = 0; k < 3; ++k)
                    {
                        const int d = (c[k] - luma) * _saturation256;
                        c[k] = std::clamp(luma + (d >= 0 ? (d + 128) >> 8 : -((-d + 128) >> 8)), 0, 255);
                    }
                }
                for (int k = 0; k < 3; ++k)
                    out[k] = _toneTable[c[k]];
                out[3] = 255;
            }
        }
    });
}

void CRTRenderer::compose(uint8_t* out, int stride, bool bgra) const
{
    const int red = bgra ? 2 : 0;
    const int blue = bgra ? 0 : 2;
    const int width = _layout.outWidth;
    const int frameWidth = _layout.frameWidth;
    if (!out || _graded.empty() || _index.size() != static_cast<size_t>(width) * _layout.outHeight * 4)
        return;
    if (_plain)
    {
        composePlain(out, stride, bgra);
        return;
    }
    ForRows(_layout.outHeight, [&](int firstRow, int endRow) {
        for (int y = firstRow; y < endRow; ++y)
        {
            const uint8_t* index = _index.data() + static_cast<size_t>(y) * width * 4;
            const uint8_t* factor = _factor.data() + static_cast<size_t>(y) * width * 4;
            uint8_t* o = out + static_cast<size_t>(y) * stride;
            for (int x = 0; x < width; ++x, index += 4, factor += 4, o += 4)
            {
                const int sx = index[0] | (index[1] << 8);
                const int sy = index[2] | (index[3] << 8);
                const uint8_t* c = _graded.data() + (static_cast<size_t>(sy) * frameWidth + sx) * 4;
                o[red] = static_cast<uint8_t>(std::min(255, (c[0] * factor[0]) >> 7));
                o[1] = static_cast<uint8_t>(std::min(255, (c[1] * factor[1]) >> 7));
                o[blue] = static_cast<uint8_t>(std::min(255, (c[2] * factor[2]) >> 7));
                o[3] = 255;
            }
        }
    });
}

void CRTRenderer::composePlain(uint8_t* out, int stride, bool bgra) const
{
    const int red = bgra ? 2 : 0;
    const int blue = bgra ? 0 : 2;
    // The same values as compose() with factor 128 (c * 128 >> 7 = c): every row maps x alike, so a row showing the
    // source row of the one above is a copy of it
    const int width = _layout.outWidth;
    const int frameWidth = _layout.frameWidth;
    ForRows(_layout.outHeight, [&](int firstRow, int endRow) {
        int shownRow = -1;
        for (int y = firstRow; y < endRow; ++y)
        {
            const uint8_t* index = _index.data() + static_cast<size_t>(y) * width * 4;
            const int sy = index[2] | (index[3] << 8);
            uint8_t* o = out + static_cast<size_t>(y) * stride;
            if (sy == shownRow)
            {
                std::memcpy(o, o - stride, static_cast<size_t>(width) * 4);
                continue;
            }
            shownRow = sy;
            const uint8_t* row = _graded.data() + static_cast<size_t>(sy) * frameWidth * 4;
            for (int x = 0; x < width; ++x, index += 4, o += 4)
            {
                const uint8_t* c = row + static_cast<size_t>(index[0] | (index[1] << 8)) * 4;
                o[red] = c[0];
                o[1] = c[1];
                o[blue] = c[2];
                o[3] = 255;
            }
        }
    });
}
