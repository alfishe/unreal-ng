#include "crtbeammodel.h"

#include "crtrows.h"

#include <algorithm>
#include <cmath>
#include <utility>

// The composition's sums four lanes at a time (the same integers: no rounding differs)
#if defined(__ARM_NEON) || defined(_M_ARM64)
#include <arm_neon.h>
#define CRT_BEAM_NEON
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define CRT_BEAM_SSE2
#endif

namespace
{
constexpr int kPhases = CRTBeamModel::kPhases;
constexpr int kLinearMax = CRTBeamModel::kLinearMax;
constexpr int kOutputMax = CRTBeamModel::kOutputMax;
constexpr int kGlowPhases = CRTBeamModel::kGlowPhases;
constexpr int kGlowRows = CRTBeamModel::kGlowRows;
constexpr int kGlowTaps = CRTBeamModel::kGlowTaps;
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

/// R, G, B and a zero: every loop over the four is one vector operation
struct Lanes
{
    int c[4];
};

enum class Glow
{
    None,
    Gaussian,
    Squared
};

/// The horizontal weights, 3 rows (r, g, b) of kPhases x 4, to [phase][tap pair][lane][tap in the pair], lane 3
/// zero: a pair of taps is one multiply-add
std::vector<int16_t> PairTaps(const std::vector<int16_t>& table)
{
    std::vector<int16_t> out(static_cast<size_t>(kPhases) * 16, 0);
    for (int p = 0; p < kPhases; ++p)
        for (int t = 0; t < 4; ++t)
            for (int ch = 0; ch < 3; ++ch)
                out[static_cast<size_t>(p * 16 + (t / 2) * 8 + ch * 2 + t % 2)] =
                    table[static_cast<size_t>((ch * kPhases + p) * 4 + t)];
    return out;
}

/// The glow's row weights per y phase, as the eight values a glow table entry has per row: squared, row k's square
/// weight for RGB0 then its product weight; gaussian, rows 2k and 2k + 1 (the table has four values a row)
std::vector<uint16_t> GlowLanes(const std::vector<uint16_t>& rows, const std::vector<uint16_t>& products, bool squared)
{
    std::vector<uint16_t> out(rows.empty() ? 0 : static_cast<size_t>(kPhases) * kGlowRows * 8, 0);
    for (size_t p = 0; p < out.size() / (kGlowRows * 8); ++p)
    {
        const uint16_t* wy = &rows[p * kGlowRows];
        uint16_t* o = &out[p * kGlowRows * 8];
        for (int k = 0; k < kGlowRows; ++k)
            for (int ch = 0; ch < 3; ++ch)
            {
                if (squared)
                {
                    o[k * 8 + ch] = wy[k];
                    o[k * 8 + 4 + ch] = products[p * kGlowRows + static_cast<size_t>(k)];
                }
                else if (k < kGlowRows / 2)
                {
                    o[k * 8 + ch] = wy[2 * k];
                    o[k * 8 + 4 + ch] = wy[2 * k + 1];
                }
            }
    }
    return out;
}

/// The gaussian glow's column weights, kGlowPhases rows of kGlowTaps, to [phase][tap pair][lane][tap in the pair]
std::vector<int16_t> GlowPairs(const std::vector<int16_t>& table)
{
    std::vector<int16_t> out(static_cast<size_t>(kGlowPhases) * kGlowTaps * 4, 0);
    for (int q = 0; q < kGlowPhases && !table.empty(); ++q)
        for (int t = 0; t < kGlowTaps; ++t)
            for (int ch = 0; ch < 3; ++ch)
                out[static_cast<size_t>(q * kGlowTaps * 4 + (t / 2) * 8 + ch * 2 + t % 2)] =
                    table[static_cast<size_t>(q * kGlowTaps + t)];
    return out;
}

/// The gaussian glow at one column: its taps `t` ([tap][lane], lane 3 zero) times the weights of every x phase `w`
/// (GlowPairs()) to `out`, kGlowPhases x RGB0
void GlowColumn(const uint16_t* t, const int16_t* w, uint16_t* out)
{
#if defined(CRT_BEAM_NEON)
    int16x4_t taps[kGlowTaps];
    for (int k = 0; k < kGlowTaps; ++k)
        taps[k] = vreinterpret_s16_u16(vld1_u16(t + k * 4));
    for (int q = 0; q < kGlowPhases; ++q, w += kGlowTaps * 4)
    {
        int32x4_t sum = vdupq_n_s32(0);
        for (int k = 0; k < kGlowTaps / 2; ++k)
        {
            const int16x4x2_t pair = vld2_s16(w + k * 8);
            sum = vmlal_s16(sum, taps[2 * k], pair.val[0]);
            sum = vmlal_s16(sum, taps[2 * k + 1], pair.val[1]);
        }
        sum = vshrq_n_s32(vaddq_s32(vmaxq_s32(sum, vdupq_n_s32(0)), vdupq_n_s32(2048)), 12);
        vst1_u16(out + q * 4, vmovn_u32(vreinterpretq_u32_s32(sum)));
    }
#elif defined(CRT_BEAM_SSE2)
    __m128i pairs[kGlowTaps / 2];
    for (int k = 0; k < kGlowTaps / 2; ++k)
    {
        const __m128i both = _mm_loadu_si128(reinterpret_cast<const __m128i*>(t + k * 8));
        pairs[k] = _mm_unpacklo_epi16(both, _mm_srli_si128(both, 8));
    }
    for (int q = 0; q < kGlowPhases; ++q, w += kGlowTaps * 4)
    {
        __m128i sum = _mm_setzero_si128();
        for (int k = 0; k < kGlowTaps / 2; ++k)
            sum = _mm_add_epi32(sum,
                                _mm_madd_epi16(pairs[k], _mm_loadu_si128(reinterpret_cast<const __m128i*>(w + k * 8))));
        sum = _mm_andnot_si128(_mm_srai_epi32(sum, 31), sum);
        sum = _mm_srli_epi32(_mm_add_epi32(sum, _mm_set1_epi32(2048)), 12);
        // The low 16 bits, as the scalar cast
        sum = _mm_srai_epi32(_mm_slli_epi32(sum, 16), 16);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(out + q * 4), _mm_packs_epi32(sum, sum));
    }
#else
    for (int q = 0; q < kGlowPhases; ++q, w += kGlowTaps * 4)
        for (int ch = 0; ch < 4; ++ch)
        {
            int sum = 0;
            for (int k = 0; k < kGlowTaps; ++k)
                sum += t[k * 4 + ch] * w[(k / 2) * 8 + ch * 2 + k % 2];
            out[q * 4 + ch] = static_cast<uint16_t>((std::max(sum, 0) + 2048) >> 12);
        }
#endif
}

/// 3 rows (r, g, b) of kPhases x 4 weights to [phase][tap][lane], lane 3 zero
std::vector<int> Spread(const std::vector<int16_t>& table)
{
    std::vector<int> out(static_cast<size_t>(kPhases) * 16, 0);
    if (table.empty())
        return out;
    for (int p = 0; p < kPhases; ++p)
        for (int t = 0; t < 4; ++t)
            for (int ch = 0; ch < 3; ++ch)
                out[static_cast<size_t>((p * 4 + t) * 4 + ch)] = table[static_cast<size_t>((ch * kPhases + p) * 4 + t)];
    return out;
}

/// Up to four source rows through the horizontal filter, from row `first` at column x, phase px
struct Filtered
{
    int x = CRTBeamModel::kOutside;
    int px = 0;
    int first = 0;
    Lanes rows[4];
};

// ---- The composition, the same integer arithmetic as kShader ----

struct Composer
{
    const uint16_t* frame;
    const uint16_t* glowTable;
    int frameWidth;
    int left;
    int top;
    int right;
    int bottom;
    int glowColumns;
    int glowRows;
    const uint16_t* vertical;
    const uint16_t* transferTable;
    const uint16_t* boost;
    const uint16_t* glowPhase;
    const uint16_t* output;
    bool clampToCentre;
    bool beamByBrightness;
    bool fourRows;
    bool glowInDisplay;
    int brightnessWeights[4];
    std::vector<int16_t> horizontal;    ///< PairTaps()
    std::vector<int> interpolation;     ///< [phase][row][lane]
    std::vector<uint16_t> glowWeights;  ///< GlowLanes()

    Composer(const CRTBeamModel& model, const uint16_t* frameData, const uint16_t* glowData)
        : frame(frameData),
          glowTable(glowData),
          frameWidth(model.layout().frameWidth),
          left(model.layout().source.left()),
          top(model.layout().source.top()),
          right(model.layout().source.right()),
          bottom(model.layout().source.bottom()),
          glowColumns(model.layout().source.width() + 2 * kGlowPad),
          glowRows(model.layout().source.height() + 2 * kGlowPad),
          vertical(model.verticalTable().data()),
          transferTable(model.transferTable().empty() ? nullptr : model.transferTable().data()),
          boost(model.boostTable().empty() ? nullptr : model.boostTable().data()),
          glowPhase(model.glowPhase().data()),
          output(model.outputTable().data()),
          clampToCentre(model.clampToCentre()),
          beamByBrightness(model.beamByBrightness()),
          fourRows(model.fourRows()),
          glowInDisplay(model.glowInDisplay()),
          brightnessWeights{model.brightnessWeights()[0], model.brightnessWeights()[1], model.brightnessWeights()[2],
                            model.brightnessWeights()[3]},
          horizontal(PairTaps(model.horizontalWeights())),
          interpolation(Spread(model.interpolationTable())),
          glowWeights(GlowLanes(model.glowWeightsY(), model.glowWeightsB(), model.glowSquared()))
    {
    }

    /// The taps x - 1 .. x + 2 of row y, RGBA each: in the frame when all are on the shown picture, otherwise copied
    /// into `buffer`, the ones off it black (the shaders' ClampToBorder)
    const uint16_t* taps(int x, int y, bool inside, uint16_t* buffer) const
    {
        if (inside)
            return frame + (static_cast<size_t>(y) * frameWidth + x - 1) * 4;
        for (int t = 0; t < 4; ++t)
        {
            const int tx = x - 1 + t;
            const bool shown = tx >= left && tx <= right && y >= top && y <= bottom;
            for (int ch = 0; ch < 4; ++ch)
                buffer[t * 4 + ch] = shown ? frame[(static_cast<size_t>(y) * frameWidth + tx) * 4 + ch] : 0;
        }
        return buffer;
    }

    /// One row through the horizontal filter: taps `t` ([tap][lane]), weights `w` of the phase (PairTaps())
    Lanes row(const uint16_t* t, const int16_t* w) const
    {
        Lanes out;
#if defined(CRT_BEAM_NEON)
        const int16x4x2_t w01 = vld2_s16(w);  // the even weights tap 0's, the odd ones tap 1's
        const int16x4x2_t w23 = vld2_s16(w + 8);
        const int16x4_t b = vreinterpret_s16_u16(vld1_u16(t + 4));
        const int16x4_t c = vreinterpret_s16_u16(vld1_u16(t + 8));
        int32x4_t sum = vmull_s16(vreinterpret_s16_u16(vld1_u16(t)), w01.val[0]);
        sum = vmlal_s16(sum, b, w01.val[1]);
        sum = vmlal_s16(sum, c, w23.val[0]);
        sum = vmlal_s16(sum, vreinterpret_s16_u16(vld1_u16(t + 12)), w23.val[1]);
        sum = vshrq_n_s32(vaddq_s32(vmaxq_s32(sum, vdupq_n_s32(0)), vdupq_n_s32(2048)), 12);
        if (clampToCentre)
            sum = vminq_s32(vmaxq_s32(sum, vmovl_s16(vmin_s16(b, c))), vmovl_s16(vmax_s16(b, c)));
        else
            sum = vminq_s32(sum, vdupq_n_s32(kLinearMax));
        vst1q_s32(out.c, sum);
#elif defined(CRT_BEAM_SSE2)
        const __m128i t01 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(t));
        const __m128i t23 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(t + 8));
        // Each lane's two taps next to each other, then one multiply-add per pair (the taps are under 2^14)
        const __m128i pair01 = _mm_unpacklo_epi16(t01, _mm_srli_si128(t01, 8));
        const __m128i pair23 = _mm_unpacklo_epi16(t23, _mm_srli_si128(t23, 8));
        __m128i sum = _mm_add_epi32(_mm_madd_epi16(pair01, _mm_loadu_si128(reinterpret_cast<const __m128i*>(w))),
                                    _mm_madd_epi16(pair23, _mm_loadu_si128(reinterpret_cast<const __m128i*>(w + 8))));
        sum = _mm_andnot_si128(_mm_srai_epi32(sum, 31), sum);
        sum = _mm_srli_epi32(_mm_add_epi32(sum, _mm_set1_epi32(2048)), 12);
        // 16 bits, saturated: both bounds are under 2^15, a larger sum ends at the bound either way
        __m128i narrow = _mm_packs_epi32(sum, sum);
        if (clampToCentre)
        {
            const __m128i b = _mm_srli_si128(t01, 8);
            narrow = _mm_min_epi16(_mm_max_epi16(narrow, _mm_min_epi16(b, t23)), _mm_max_epi16(b, t23));
        }
        else
            narrow = _mm_min_epi16(narrow, _mm_set1_epi16(static_cast<short>(kLinearMax)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out.c), _mm_unpacklo_epi16(narrow, _mm_setzero_si128()));
#else
        for (int ch = 0; ch < 4; ++ch)
        {
            const int b = t[4 + ch];
            const int c = t[8 + ch];
            const int sum = t[ch] * w[ch * 2] + b * w[ch * 2 + 1] + c * w[8 + ch * 2] + t[12 + ch] * w[9 + ch * 2];
            const int v = (std::max(sum, 0) + 2048) >> 12;
            out.c[ch] = clampToCentre ? std::min(std::max(v, std::min(b, c)), std::max(b, c)) : std::min(v, kLinearMax);
        }
#endif
        return out;
    }

    /// The glow: the eight rows' values from `v`, `stride` apart (`Values` each, the table's), times the weights
    /// `w` of the y phase (GlowLanes())
    template <int Values>
    static Lanes glowSum(const uint16_t* v, size_t stride, const uint16_t* w)
    {
        Lanes out;
#if defined(CRT_BEAM_NEON) || defined(CRT_BEAM_SSE2)
        constexpr int count = kGlowRows * Values / 8;  // eight values a step
#endif
#if defined(CRT_BEAM_NEON)
        uint32x4_t sum = vdupq_n_u32(0);
        for (int k = 0; k < count; ++k)
        {
            const uint16x8_t a = Values == 8
                                     ? vld1q_u16(v + k * stride)
                                     : vcombine_u16(vld1_u16(v + 2 * k * stride), vld1_u16(v + (2 * k + 1) * stride));
            const uint16x8_t b = vld1q_u16(w + k * 8);
            sum = vmlal_u16(sum, vget_low_u16(a), vget_low_u16(b));
            sum = vmlal_u16(sum, vget_high_u16(a), vget_high_u16(b));
        }
        vst1q_s32(out.c,
                  vreinterpretq_s32_u32(vshrq_n_u32(vaddq_u32(sum, vdupq_n_u32(CRTBeamModel::kGlowUnit / 2)), 16)));
#elif defined(CRT_BEAM_SSE2)
        __m128i sum = _mm_setzero_si128();
        for (int k = 0; k < count; ++k)
        {
            const __m128i a =
                Values == 8
                    ? _mm_loadu_si128(reinterpret_cast<const __m128i*>(v + k * stride))
                    : _mm_unpacklo_epi64(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(v + 2 * k * stride)),
                                         _mm_loadl_epi64(reinterpret_cast<const __m128i*>(v + (2 * k + 1) * stride)));
            const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(w + k * 8));
            const __m128i low = _mm_mullo_epi16(a, b);
            const __m128i high = _mm_mulhi_epu16(a, b);
            sum = _mm_add_epi32(sum, _mm_add_epi32(_mm_unpacklo_epi16(low, high), _mm_unpackhi_epi16(low, high)));
        }
        sum = _mm_srli_epi32(_mm_add_epi32(sum, _mm_set1_epi32(CRTBeamModel::kGlowUnit / 2)), 16);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out.c), sum);
#else
        uint32_t sum[4] = {};
        for (int k = 0; k < kGlowRows; ++k)
            for (int i = 0; i < Values; ++i)
                sum[i % 4] += static_cast<uint32_t>(v[k * stride + i]) * w[k * Values + i];
        for (int ch = 0; ch < 4; ++ch)
            out.c[ch] = static_cast<int>((sum[ch] + CRTBeamModel::kGlowUnit / 2) >> 16);
#endif
        return out;
    }

    int brightness(const Lanes& v) const
    {
        const int* bw = brightnessWeights;
        const int high = std::max(v.c[0], std::max(v.c[1], v.c[2]));
        return std::clamp((v.c[0] * bw[0] + v.c[1] * bw[1] + v.c[2] * bw[2] + high * bw[3] + 2048) >> 12, 0,
                          kLinearMax);
    }

    void transfer(Lanes& v) const
    {
        if (!transferTable)
            return;
        for (int ch = 0; ch < 3; ++ch)
            v.c[ch] = transferTable[v.c[ch]];
    }

    /// The rows around source row y through the horizontal filter at column x, phase px: rows y - 1 .. y + 2
    /// (`four`) or y, y + 1 from `first`. The rows the cache (the output row above at this output column) has
    /// are not filtered again
    void filtered(int x, int y, int px, bool four, Filtered& cache) const
    {
        const int first = four ? y - 1 : y;
        const int count = four ? 4 : 2;
        int done = 0;
        if (cache.x == x && cache.px == px && cache.first <= first && first < cache.first + count)
        {
            done = cache.first + count - first;
            for (int i = 0; i < done; ++i)
                cache.rows[i] = cache.rows[i + first - cache.first];
        }
        cache.x = x;
        cache.px = px;
        cache.first = first;
        if (done == count)
            return;
        const int16_t* w = &horizontal[static_cast<size_t>(px) * 16];
        const bool inside = x - 1 >= left && x + 2 <= right && first >= top && first + count - 1 <= bottom;
        uint16_t buffer[16];
        for (int i = done; i < count; ++i)
            cache.rows[i] = row(taps(x, first + i, inside, buffer), w);
    }

    /// One output pixel: geometry g (x, y, x phase, y phase), factors f (r, g, b, state) -> the 8-bit channels;
    /// `cache` the filtered rows of this output column
    template <bool Beams, Glow GlowKind>
    Lanes pixel(const int16_t* g, const uint16_t* f, Filtered& cache) const
    {
        const int x = g[0];
        const int y = g[1];
        const int px = g[2];
        const int py = g[3];
        const bool four = !Beams && fourRows;
        filtered(x, y, px, four, cache);
        Lanes r0 = cache.rows[four ? 1 : 0];
        Lanes r1 = cache.rows[four ? 2 : 1];
        Lanes col;
        if constexpr (Beams)
        {
            transfer(r0);
            transfer(r1);
            const uint16_t* b0 = vertical + static_cast<size_t>(py) * 256;
            const uint16_t* b1 = vertical + static_cast<size_t>(kPhases - py) * 256;
            const int bright0 = beamByBrightness ? brightness(r0) : 0;
            const int bright1 = beamByBrightness ? brightness(r1) : 0;
            for (int ch = 0; ch < 3; ++ch)
            {
                const int v0 = beamByBrightness ? bright0 : r0.c[ch];
                const int v1 = beamByBrightness ? bright1 : r1.c[ch];
                col.c[ch] = (r0.c[ch] * b0[v0 >> 6] + r1.c[ch] * b1[v1 >> 6] + 2048) >> 12;
            }
            col.c[3] = 0;
        }
        else
        {
            const int* a = &interpolation[static_cast<size_t>(py) * 16];
            const Lanes above = four ? cache.rows[0] : Lanes{};
            const Lanes below = four ? cache.rows[3] : Lanes{};
            for (int ch = 0; ch < 4; ++ch)
            {
                const int sum =
                    above.c[ch] * a[ch] + r0.c[ch] * a[4 + ch] + r1.c[ch] * a[8 + ch] + below.c[ch] * a[12 + ch];
                col.c[ch] = std::clamp((sum + 2048) >> 12, 0, kLinearMax);
            }
            transfer(col);
            const int bright = brightness(col) >> 6;
            const uint16_t* factor = vertical + (static_cast<size_t>(f[3]) * 3 * kPhases + py) * 256 + bright;
            for (int ch = 0; ch < 3; ++ch)
                col.c[ch] = (col.c[ch] * factor[static_cast<size_t>(ch) * kPhases * 256] + 2048) >> 12;
        }
        for (int ch = 0; ch < 4; ++ch)
            col.c[ch] = std::min(col.c[ch], kOutputMax);
        if (boost)
        {
            const int gain = boost[std::max(col.c[0], std::max(col.c[1], col.c[2])) >> 6];
            for (int ch = 0; ch < 4; ++ch)
                col.c[ch] = std::min((col.c[ch] * gain + 2048) >> 12, kOutputMax);
        }
        Lanes glow{};
        if constexpr (GlowKind != Glow::None)
        {
            // The glow table's eight rows around the beam: zeros past the table (no tap of it reaches the picture)
            constexpr int values = GlowKind == Glow::Squared ? 8 : 4;
            const int phase = glowPhase[px];
            const int c = x + phase / kGlowPhases - left + kGlowPad;
            const int r = y + kGlowFirst - top + kGlowPad;
            const bool column = c >= 0 && c < glowColumns;
            const size_t stride = static_cast<size_t>(glowColumns) * kGlowPhases * values;
            const uint16_t* p =
                column ? glowTable + (static_cast<size_t>(c) * kGlowPhases + phase % kGlowPhases) * values : nullptr;
            uint16_t edge[kGlowRows * values];
            const uint16_t* v = edge;
            size_t step = values;
            if (column && r >= 0 && r + kGlowRows <= glowRows)
            {
                v = p + static_cast<size_t>(r) * stride;
                step = stride;
            }
            else
                for (int k = 0; k < kGlowRows; ++k)
                {
                    const bool shown = column && r + k >= 0 && r + k < glowRows;
                    for (int i = 0; i < values; ++i)
                        edge[k * values + i] = shown ? p[static_cast<size_t>(r + k) * stride + i] : 0;
                }
            glow = glowSum<values>(v, step, &glowWeights[static_cast<size_t>(py) * kGlowRows * 8]);
        }
        Lanes out;
        for (int ch = 0; ch < 3; ++ch)
        {
            if (glowInDisplay)
            {
                // The display value (kLinearMax = 1.0) plus the glow, then the factor, to 8 bits
                const int d = output[col.c[ch]] + glow.c[ch];
                const int v = (std::min(d, kOutputMax) * f[ch] + 512) >> 10;
                out.c[ch] = std::min((v * 255 + kLinearMax / 2) / kLinearMax, 255);
            }
            else
            {
                const int v = std::min(col.c[ch] + glow.c[ch], kOutputMax);
                out.c[ch] = output[std::min((v * f[ch] + 512) >> 10, kOutputMax)];
            }
        }
        out.c[3] = 255;
        return out;
    }
};

/// The picture with one pixel() for the model's vertical stage and glow
template <bool Beams, Glow GlowKind>
void ComposeRows(const Composer& composer, const CRTBeamModel& model, uint8_t* out, int stride, bool bgra)
{
    const CRTBeamModel::Layout& l = model.layout();
    const int red = bgra ? 2 : 0;
    const int blue = bgra ? 0 : 2;
    ForRows(l.outHeight, [&](int first, int end) {
        // An output column mostly shows the same source column and phase as the output row above it
        std::vector<Filtered> columns(static_cast<size_t>(l.outWidth));
        for (int y = first; y < end; ++y)
        {
            uint8_t* o = out + static_cast<size_t>(y) * stride;
            const size_t row = static_cast<size_t>(y) * l.outWidth * 4;
            const int16_t* g = &model.geometry()[row];
            const uint16_t* f = &model.factors()[row];
            Filtered* cache = columns.data();
            for (int x = 0; x < l.outWidth; ++x, o += 4, g += 4, f += 4, ++cache)
            {
                if (g[0] == CRTBeamModel::kOutside)
                {
                    o[0] = o[1] = o[2] = 0;
                    o[3] = 255;
                    continue;
                }
                const Lanes c = composer.pixel<Beams, GlowKind>(g, f, *cache);
                o[red] = static_cast<uint8_t>(c.c[0]);
                o[1] = static_cast<uint8_t>(c.c[1]);
                o[blue] = static_cast<uint8_t>(c.c[2]);
                o[3] = 255;
            }
        }
    });
}
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
    const int values = _glowSquared ? 8 : 4;
    _glow.resize(static_cast<size_t>(rows) * columns * kGlowPhases * values);
    // Source value at (x, y), black off the picture
    auto tap = [&](int x, int y, int ch) {
        const bool inside = x >= source.left() && x <= source.right() && y >= source.top() && y <= source.bottom();
        return inside ? static_cast<int>(input[(static_cast<size_t>(y) * w + x) * 4 + ch]) : 0;
    };
    const std::vector<int16_t> columnWeights = GlowPairs(_glowX);
    ForRows(rows, [&](int first, int end) {
        // Squared: the bilinear samples of a row at every column and x phase, [column + kGlowSamples][q][RGB0]
        constexpr int kColumn = kGlowPhases * 4;
        const int span = columns + kGlowSamples;
        std::vector<int> samples[2];
        std::vector<int> squareTerms(static_cast<size_t>(span) * kColumn);
        std::vector<int> productTerms(squareTerms.size());
        auto sampleRow = [&](int y, std::vector<int>& out) {
            out.resize(static_cast<size_t>(span) * kColumn);
            for (int c = 0; c < span; ++c)
            {
                const int x = source.left() - kGlowPad + c + kGlowFirst;
                int t0[4] = {};
                int t1[4] = {};
                for (int ch = 0; ch < 3; ++ch)
                {
                    t0[ch] = tap(x, y, ch);
                    t1[ch] = tap(x + 1, y, ch);
                }
                int* o = &out[static_cast<size_t>(c) * kColumn];
                for (int q = 0; q < kGlowPhases; ++q)
                    for (int ch = 0; ch < 4; ++ch)
                        o[q * 4 + ch] = ((kGlowPhases - q) * t0[ch] + q * t1[ch] + kGlowPhases / 2) / kGlowPhases;
            }
        };
        for (int r = first; r < end; ++r)
        {
            const int y = source.top() - kGlowPad + r;
            auto at = [&](int c, int q) {
                return &_glow[((static_cast<size_t>(r) * columns + c) * kGlowPhases + q) * values];
            };
            if (_glowSquared)
            {
                // This row's samples are the last row's next ones; the sums over seven columns slide
                if (r == first)
                    sampleRow(y, samples[0]);
                sampleRow(y + 1, samples[1]);
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
                    uint16_t* o = at(c, 0);
                    for (int q = 0; q < kGlowPhases; ++q, o += 8)
                        for (int ch = 0; ch < 4; ++ch)
                        {
                            o[ch] = static_cast<uint16_t>(squares[q * 4 + ch]);
                            o[4 + ch] = static_cast<uint16_t>(products[q * 4 + ch]);
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
                uint16_t taps[kGlowTaps * 4] = {};
                for (int t = 0; t < kGlowTaps; ++t)
                    for (int ch = 0; ch < 3; ++ch)
                        taps[t * 4 + ch] = static_cast<uint16_t>(tap(x + t + kGlowFirst, y, ch));
                GlowColumn(taps, columnWeights.data(), at(c, 0));
            }
        }
    });
}

void CRTBeamModel::compose(uint8_t* out, int stride, bool bgra) const
{
    const Composer composer(*this, _frame.data(), _glow.data());
    const Glow glow = _glowY.empty() ? Glow::None : _glowSquared ? Glow::Squared : Glow::Gaussian;
    if (_verticalMode == Vertical::Beams)
    {
        if (glow == Glow::None)
            ComposeRows<true, Glow::None>(composer, *this, out, stride, bgra);
        else if (glow == Glow::Gaussian)
            ComposeRows<true, Glow::Gaussian>(composer, *this, out, stride, bgra);
        else
            ComposeRows<true, Glow::Squared>(composer, *this, out, stride, bgra);
    }
    else if (glow == Glow::None)
        ComposeRows<false, Glow::None>(composer, *this, out, stride, bgra);
    else if (glow == Glow::Gaussian)
        ComposeRows<false, Glow::Gaussian>(composer, *this, out, stride, bgra);
    else
        ComposeRows<false, Glow::Squared>(composer, *this, out, stride, bgra);
}
