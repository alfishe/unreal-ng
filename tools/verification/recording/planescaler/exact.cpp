// PlaneScaler's bytes (Rows and RowsInterleaved) for many sizes, noise and a 0/255 checkerboard, on stdout:
// ab.sh compares the two builds' outputs byte for byte (a vector body must give the scalar code's bytes)
#include "framescaler.h"
#include <cstdio>
#include <vector>
int main()
{
    for (uint32_t sw : {4u, 5u, 13u, 64u, 659u})
        for (uint32_t dw = 1; dw <= 70; dw += 3)
            for (int checker = 0; checker < 2; checker++)
            {
                const uint32_t sh = 11, dh = 29;
                std::vector<uint8_t> u(static_cast<size_t>(sw) * sh), v(u.size());
                uint32_t state = sw * 131 + dw;
                for (size_t i = 0; i < u.size(); i++)
                {
                    state = state * 1664525u + 1013904223u;
                    u[i] = checker ? (((i % sw + i / sw) & 1) ? 255 : 0) : static_cast<uint8_t>(state >> 24);
                    v[i] = static_cast<uint8_t>(state >> 16);
                }
                FrameScaler::PlaneScaler s;
                s.Prepare(sw, sh, dw * 37, dh);
                std::vector<uint8_t> out(static_cast<size_t>(dw) * 37 * dh), pairs(out.size() * 2);
                s.Rows(u.data(), sw, out.data(), dw * 37, 0, dh);
                s.RowsInterleaved(u.data(), v.data(), sw, pairs.data(), dw * 74, 0, dh);
                std::fwrite(out.data(), 1, out.size(), stdout);
                std::fwrite(pairs.data(), 1, pairs.size(), stdout);
            }
}
