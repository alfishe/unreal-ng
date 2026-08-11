/**
 * blip_buf - Band-limited sound synthesis buffer
 * Clean C++ port of Shay Green's blip_buf library.
 */

#include "blip_buf.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using fixed_t = std::uint64_t;

constexpr int pre_shift       = 32;
constexpr int time_bits       = pre_shift + 20;
constexpr fixed_t time_unit   = static_cast<fixed_t>(1) << time_bits;

constexpr int end_frame_extra = 2;

constexpr int half_width      = 8;
constexpr int buf_extra       = half_width * 2 + end_frame_extra;
constexpr int phase_bits      = 5;
constexpr int phase_count     = 1 << phase_bits;
constexpr int delta_bits      = 15;
constexpr int delta_unit      = 1 << delta_bits;
constexpr int frac_bits       = time_bits - pre_shift;

constexpr int max_sample      = +32767;
constexpr int min_sample      = -32768;

// Sinc impulse step table
constexpr int16_t bl_step[phase_count + 1][half_width] = {
    { 43, -115,  350, -488, 1136, -914, 5861, 21022 },
    { 44, -118,  348, -473, 1076, -799, 5274, 21001 },
    { 45, -121,  344, -454, 1011, -677, 4706, 20936 },
    { 46, -122,  336, -431,  942, -549, 4156, 20829 },
    { 47, -123,  327, -404,  868, -418, 3629, 20679 },
    { 47, -122,  316, -375,  792, -285, 3124, 20488 },
    { 47, -120,  303, -344,  714, -151, 2644, 20256 },
    { 46, -117,  289, -310,  634,  -17, 2188, 19985 },
    { 46, -114,  273, -275,  553,  117, 1758, 19675 },
    { 44, -108,  255, -237,  471,  247, 1356, 19327 },
    { 43, -103,  237, -199,  390,  373,  981, 18944 },
    { 42,  -98,  218, -160,  310,  495,  633, 18527 },
    { 40,  -91,  198, -121,  231,  611,  314, 18078 },
    { 38,  -84,  178,  -81,  153,  722,   22, 17599 },
    { 36,  -76,  157,  -43,   80,  824, -241, 17092 },
    { 34,  -68,  135,   -3,    8,  919, -476, 16558 },
    { 32,  -61,  115,   34,  -60, 1006, -683, 16001 },
    { 29,  -52,   94,   70, -123, 1083, -862, 15422 },
    { 27,  -44,   73,  106, -184, 1152, -1015, 14824 },
    { 25,  -36,   53,  139, -239, 1211, -1142, 14210 },
    { 22,  -27,   34,  170, -290, 1261, -1244, 13582 },
    { 20,  -20,   16,  199, -335, 1301, -1322, 12942 },
    { 18,  -12,   -3,  226, -375, 1331, -1376, 12293 },
    { 15,   -4,  -19,  250, -410, 1351, -1408, 11638 },
    { 13,    3,  -35,  272, -439, 1361, -1419, 10979 },
    { 11,    9,  -49,  292, -464, 1362, -1410, 10319 },
    {  9,   16,  -63,  309, -483, 1354, -1383,  9660 },
    {  7,   22,  -75,  322, -496, 1337, -1339,  9005 },
    {  6,   26,  -85,  333, -504, 1312, -1280,  8355 },
    {  4,   31,  -94,  341, -507, 1278, -1205,  7713 },
    {  3,   35, -102,  347, -506, 1238, -1119,  7082 },
    {  1,   40, -110,  350, -499, 1190, -1021,  6464 },
    {  0,   43, -115,  350, -488, 1136, -914,  5861 }
};

inline void clamp_sample(int& n)
{
    n = std::clamp(n, min_sample, max_sample);
}

} // namespace

struct blip_t
{
    fixed_t factor = 0;
    fixed_t offset = 0;
    int avail = 0;
    int size = 0;
    int integrator = 0;
    std::vector<int> buffer;
};

extern "C" {

blip_t* blip_new(int size)
{
    assert(size >= 0);
    auto* m = new (std::nothrow) blip_t();
    if (m)
    {
        m->factor = time_unit / blip_max_ratio;
        m->size = size;
        m->buffer.resize(size + buf_extra, 0);
        blip_clear(m);
    }
    return m;
}

void blip_delete(blip_t* m)
{
    delete m;
}

void blip_set_rates(blip_t* m, double clock_rate, double sample_rate)
{
    if (!m) return;
    double factor = time_unit * sample_rate / clock_rate;
    m->factor = static_cast<fixed_t>(factor);

    assert(0 <= factor - m->factor && factor - m->factor < 1);

    if (m->factor < factor)
        m->factor++;
}

void blip_clear(blip_t* m)
{
    if (!m) return;
    m->offset = m->factor / 2;
    m->avail = 0;
    m->integrator = 0;
    std::fill(m->buffer.begin(), m->buffer.end(), 0);
}

int blip_clocks_needed(const blip_t* m, int samples)
{
    if (!m) return 0;
    assert(samples >= 0 && m->avail + samples <= m->size);

    fixed_t needed = static_cast<fixed_t>(samples) * time_unit;
    if (needed < m->offset)
        return 0;

    return static_cast<int>((needed - m->offset + m->factor - 1) / m->factor);
}

void blip_add_delta(blip_t* m, unsigned int time, int delta)
{
    if (!m || delta == 0) return;

    unsigned fixed = static_cast<unsigned>((time * m->factor + m->offset) >> pre_shift);
    int sample_idx = m->avail + (fixed >> frac_bits);

    if (sample_idx < 0 || sample_idx + 16 > static_cast<int>(m->buffer.size()))
    {
        sample_idx = static_cast<int>(m->buffer.size()) - 16;
        if (sample_idx < 0) return;
    }

    int* out = &m->buffer[sample_idx];

    int const phase_shift = frac_bits - phase_bits;
    int phase = (fixed >> phase_shift) & (phase_count - 1);
    const int16_t* in1 = bl_step[phase];
    const int16_t* in2 = bl_step[phase + 1];
    const int16_t* rev1 = bl_step[phase_count - phase];
    const int16_t* rev2 = bl_step[phase_count - phase - 1];

    int interp = (fixed >> (phase_shift - delta_bits)) & (delta_unit - 1);
    int delta2 = (delta * interp) >> delta_bits;
    delta -= delta2;

    out[0]  += in1[0] * delta + in2[0] * delta2;
    out[1]  += in1[1] * delta + in2[1] * delta2;
    out[2]  += in1[2] * delta + in2[2] * delta2;
    out[3]  += in1[3] * delta + in2[3] * delta2;
    out[4]  += in1[4] * delta + in2[4] * delta2;
    out[5]  += in1[5] * delta + in2[5] * delta2;
    out[6]  += in1[6] * delta + in2[6] * delta2;
    out[7]  += in1[7] * delta + in2[7] * delta2;

    out[8]  += rev1[7] * delta + rev2[7] * delta2;
    out[9]  += rev1[6] * delta + rev2[6] * delta2;
    out[10] += rev1[5] * delta + rev2[5] * delta2;
    out[11] += rev1[4] * delta + rev2[4] * delta2;
    out[12] += rev1[3] * delta + rev2[3] * delta2;
    out[13] += rev1[2] * delta + rev2[2] * delta2;
    out[14] += rev1[1] * delta + rev2[1] * delta2;
    out[15] += rev1[0] * delta + rev2[0] * delta2;
}

void blip_add_delta_fast(blip_t* m, unsigned int time, int delta)
{
    if (!m || delta == 0) return;

    unsigned fixed = static_cast<unsigned>((time * m->factor + m->offset) >> pre_shift);
    int sample_idx = m->avail + (fixed >> frac_bits);

    if (sample_idx < 0 || sample_idx + 9 > static_cast<int>(m->buffer.size()))
    {
        sample_idx = static_cast<int>(m->buffer.size()) - 9;
        if (sample_idx < 0) return;
    }

    int* out = &m->buffer[sample_idx];

    int interp = (fixed >> (frac_bits - delta_bits)) & (delta_unit - 1);
    int delta2 = delta * interp;

    out[7] += delta * delta_unit - delta2;
    out[8] += delta2;
}

void blip_end_frame(blip_t* m, unsigned int t)
{
    if (!m) return;
    fixed_t off = t * m->factor + m->offset;
    m->avail += static_cast<int>(off >> time_bits);
    m->offset = off & (time_unit - 1);

    if (m->avail > m->size)
        m->avail = m->size;
}

int blip_samples_avail(const blip_t* m)
{
    return m ? m->avail : 0;
}

static void remove_samples(blip_t* m, int count)
{
    if (!m || count <= 0) return;
    if (count > m->avail) count = m->avail;

    int remain = m->avail + buf_extra - count;
    m->avail -= count;

    if (remain > 0)
    {
        std::memmove(&m->buffer[0], &m->buffer[count], remain * sizeof(int));
    }
    std::memset(&m->buffer[m->avail + buf_extra], 0, count * sizeof(int));
}

int blip_read_samples(blip_t* m, short out[], int count, int stereo)
{
    if (!m || count <= 0) return 0;

    assert(count >= 0);

    if (count > m->avail)
        count = m->avail;

    if (count)
    {
        int const step = stereo ? 2 : 1;
        const int* in = &m->buffer[0];
        const int* end = in + count;
        int sum = m->integrator;

        do
        {
            int s = sum >> delta_bits;

            sum += *in++;

            clamp_sample(s);

            *out = static_cast<short>(s);
            out += step;
        } while (in != end);

        m->integrator = sum;

        remove_samples(m, count);
    }

    return count;
}

} // extern "C"
