// PlaneScaler speed: the picture converted at its own size (PackedToYuv) and scaled into a 4K NV12 frame's place in
// 4 bands, as the window recorder's composeNv12 does. ab.sh builds it once per framescaler.cpp; prints the median and
// the minimum of 40 runs per case (the check sum shows that both builds wrote the same frame)
#include "framescaler.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

int main()
{
    struct { uint32_t sw, sh, rw, rh; const char* name; } cases[] = {
        {659, 540, 2636, 2160, "659x540 -> 2636x2160"},
        {659, 540, 1708, 1400, "659x540 -> 1708x1400"},
        {352, 288, 2640, 2160, "352x288 -> 2640x2160"},
        {1320, 1080, 2640, 2160, "1320x1080 -> 2640x2160"},
    };
    std::vector<uint8_t> frame(3840 * 2160 * 3 / 2);
    for (const auto& c : cases)
    {
        std::vector<uint8_t> src(static_cast<size_t>(c.sw) * c.sh * 4);
        uint32_t state = 1;
        for (auto& b : src)
            b = static_cast<uint8_t>((state = state * 1664525u + 1013904223u) >> 24);
        const size_t n = static_cast<size_t>(c.sw) * c.sh;
        std::vector<uint8_t> yuv(n * 3);
        FrameScaler::PlaneScaler luma, chroma;
        uint8_t* yAt = frame.data() + 600;
        uint8_t* uvAt = frame.data() + 3840 * 2160 + 600;
        std::vector<double> ms;
        for (int r = 0; r < 41; r++)
        {
            const auto t0 = std::chrono::steady_clock::now();
            FrameScaler::PackedToYuv(src.data(), c.sw * 4, c.sw, c.sh, true, yuv.data(), yuv.data() + n, yuv.data() + 2 * n, c.sw);
            luma.Prepare(c.sw, c.sh, c.rw, c.rh);
            chroma.Prepare(c.sw, c.sh, c.rw / 2, c.rh / 2);
            auto band = [&](int part) {
                luma.Rows(yuv.data(), c.sw, yAt, 3840, c.rh * part / 4, c.rh * (part + 1) / 4);
                chroma.RowsInterleaved(yuv.data() + n, yuv.data() + 2 * n, c.sw, uvAt, 3840, c.rh / 2 * part / 4, c.rh / 2 * (part + 1) / 4);
            };
            std::thread helpers[3];
            for (int p = 1; p < 4; p++)
                helpers[p - 1] = std::thread(band, p);
            band(0);
            for (auto& h : helpers)
                h.join();
            if (r)
                ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(ms.begin(), ms.end());
        unsigned sum = 0;
        for (size_t i = 0; i < frame.size(); i += 4093)
            sum += frame[i];
        std::printf("%-24s med %6.2f min %6.2f ms (check %u)\n", c.name, ms[ms.size() / 2], ms[0], sum);
    }
}
