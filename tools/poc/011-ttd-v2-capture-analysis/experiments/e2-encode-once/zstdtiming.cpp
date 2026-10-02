// E2 timing: how long the TTD page store's zstd calls take on real pieces.
//
// Input: a file of (previous, new) 4 KB piece pairs written by run.py.
// For every pair it times, exactly as the emulator's ttdcompression.h does
// (ZSTD_compress / ZSTD_decompress, level 1, no context reuse):
//   - compressing the XOR difference and the full new piece;
//   - decompressing both.
// Each measurement is the minimum over R repetitions (load on a shared host
// only ever adds time). Output: one line per pair "xor_c full_c xor_d full_d"
// in nanoseconds, then nothing else - run.py does the statistics.
#include <zstd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <vector>

namespace
{
constexpr size_t kPiece = 4096;
constexpr int kRepeats = 15;
using Clock = std::chrono::steady_clock;

template <typename F>
uint64_t MinNs(F&& f)
{
    uint64_t best = std::numeric_limits<uint64_t>::max();
    for (int r = 0; r < kRepeats; r++)
    {
        const auto t0 = Clock::now();
        f();
        const auto t1 = Clock::now();
        best = std::min<uint64_t>(best, std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }
    return best;
}
}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: zstdtiming <pairs.bin>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t pairs = data.size() / (2 * kPiece);

    std::vector<uint8_t> diff(kPiece), out(ZSTD_compressBound(kPiece)), back(kPiece);
    std::vector<uint8_t> zx(ZSTD_compressBound(kPiece)), zf(ZSTD_compressBound(kPiece));
    for (size_t p = 0; p < pairs; p++)
    {
        const uint8_t* prev = data.data() + p * 2 * kPiece;
        const uint8_t* cur = prev + kPiece;
        for (size_t i = 0; i < kPiece; i++)
            diff[i] = prev[i] ^ cur[i];
        const size_t nx = ZSTD_compress(zx.data(), zx.size(), diff.data(), kPiece, 1);
        const size_t nf = ZSTD_compress(zf.data(), zf.size(), cur, kPiece, 1);
        if (ZSTD_isError(nx) || ZSTD_isError(nf))
            return 1;
        volatile size_t sink = 0;
        const uint64_t xc = MinNs([&] { sink = ZSTD_compress(out.data(), out.size(), diff.data(), kPiece, 1); });
        const uint64_t fc = MinNs([&] { sink = ZSTD_compress(out.data(), out.size(), cur, kPiece, 1); });
        const uint64_t xd = MinNs([&] { sink = ZSTD_decompress(back.data(), kPiece, zx.data(), nx); });
        const uint64_t fd = MinNs([&] { sink = ZSTD_decompress(back.data(), kPiece, zf.data(), nf); });
        (void)sink;
        std::printf("%llu %llu %llu %llu\n", static_cast<unsigned long long>(xc), static_cast<unsigned long long>(fc),
                    static_cast<unsigned long long>(xd), static_cast<unsigned long long>(fd));
    }
    return 0;
}
