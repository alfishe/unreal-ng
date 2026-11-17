/* Benchmark / compiled-code workload for unreal-xtensa. Built by genprograms.py with xtensa-esp32s3-elf-gcc -O2 (so
 * the interpreter runs real GCC output: zero-overhead loops, CALL8, MUL/DIV, L32R literals), and compiled natively by
 * xtensacpu_test.cpp (included there) to get the expected results. Must stay valid C and C++, freestanding. */

typedef unsigned int u32;
typedef int s32;

u32 BenchCrc32(const unsigned char* p, s32 n)
{
    u32 crc = 0xFFFFFFFFu;
    for (s32 i = 0; i < n; ++i)
    {
        crc ^= p[i];
        for (s32 k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void BenchSort(s32* a, s32 n)
{
    for (s32 i = 1; i < n; ++i)
    {
        s32 v = a[i];
        s32 j = i - 1;
        while (j >= 0 && a[j] > v)
        {
            a[j + 1] = a[j];
            --j;
        }
        a[j + 1] = v;
    }
}

s32 BenchFib(s32 n)
{
    return n < 2 ? n : BenchFib(n - 1) + BenchFib(n - 2);
}

s32 BenchMandel(s32 w, s32 h, s32 iters)
{
    s32 total = 0;
    for (s32 y = 0; y < h; ++y)
    {
        for (s32 x = 0; x < w; ++x)
        {
            s32 cr = (x * 3 * 4096) / w - 2 * 4096; /* 4.12 fixed point */
            s32 ci = (y * 2 * 4096) / h - 4096;
            s32 zr = 0, zi = 0, i = 0;
            while (i < iters)
            {
                s32 zr2 = (zr * zr) >> 12;
                s32 zi2 = (zi * zi) >> 12;
                if (zr2 + zi2 > 4 * 4096)
                    break;
                zi = ((zr * zi) >> 11) + ci;
                zr = zr2 - zi2 + cr;
                ++i;
            }
            total += i;
        }
    }
    return total;
}

/* One round over a 1 KB work area: fill, CRC, sort 64 words, fib, a small Mandelbrot. */
u32 BenchRound(unsigned char* work, u32 seed)
{
    u32 x = seed;
    for (s32 i = 0; i < 512; ++i)
    {
        x = x * 1664525u + 1013904223u;
        work[i] = (unsigned char)(x >> 24);
    }
    u32 r = BenchCrc32(work, 512);
    s32* a = (s32*)(work + 512);
    for (s32 i = 0; i < 64; ++i)
    {
        x = x * 1664525u + 1013904223u;
        a[i] = (s32)(x >> 8) - (1 << 23);
    }
    BenchSort(a, 64);
    r ^= (u32)a[0] + (u32)a[63] * 3u;
    r += (u32)BenchFib(12);
    r ^= (u32)BenchMandel(16, 12, 24) << 8;
    return r;
}

u32 BenchMain(unsigned char* work, u32 rounds)
{
    u32 r = 0;
    for (u32 i = 0; i < rounds; ++i)
        r = r * 31u + BenchRound(work, i);
    return r;
}
