// eve-emu drawing fuzz: random scenes (RAM_G contents, bitmap handles in every format, palettes, matrices, filters,
// wraps, blends, alpha and stencil tests, masks, scissors, every primitive) drawn by a chip with all its shortcuts
// (the bitmap fast paths, span fills, the recorded line walk, kept lines, the renderer thread) and by a reference chip
// with none of them: every pixel of every frame must be the same. The second frame of a scene changes nothing, so its
// lines are kept; the third changes RAM_G (palettes, texels) under the same display list.
//
// Default tier: a few scenes per variant. TestTier::Long(): thousands. A failing scene prints its seed: rerun it
// alone with UNREAL_EVE_FUZZ_SEED=<seed>. The line workers (EveSetDrawThreads) draw the same scenes, alone and on the
// renderer thread.

#include "_helpers/testtier.h"
#include "3rdparty/eve-emu/src/eve-internal.h"
#include "3rdparty/eve-emu/src/eve-render.h"
#include "eve/eve.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr uint32_t kCrystalHz = 12000000;
constexpr uint32_t kRegHcycle = 0x30202C;
constexpr uint32_t kRegHoffset = 0x302030;
constexpr uint32_t kRegHsize = 0x302034;
constexpr uint32_t kRegHsync0 = 0x302038;
constexpr uint32_t kRegHsync1 = 0x30203C;
constexpr uint32_t kRegVcycle = 0x302040;
constexpr uint32_t kRegVoffset = 0x302044;
constexpr uint32_t kRegVsize = 0x302048;
constexpr uint32_t kRegVsync0 = 0x30204C;
constexpr uint32_t kRegVsync1 = 0x302050;
constexpr uint32_t kRegDlswap = 0x302054;
constexpr uint32_t kRegPclk = 0x302070;
constexpr uint32_t kRegTagX = 0x302074;
constexpr uint32_t kRegTagY = 0x302078;
constexpr uint32_t kRamDl = 0x300000;
constexpr uint32_t kWidth = 96;
constexpr uint32_t kHeight = 48;
constexpr uint32_t kSceneRamG = 16 * 1024;  // texels and palettes live in the first 16 KB
constexpr uint32_t kSweepClocks = 128;      // VDAC4 power-up register sweep

// xorshift-style generator: scenes depend on the seed only
class Random
{
public:
    explicit Random(uint64_t seed) : _s(seed * 0x9E3779B97F4A7C15ull + 1) {}
    uint32_t Next()
    {
        _s ^= _s << 13;
        _s ^= _s >> 7;
        _s ^= _s << 17;
        return static_cast<uint32_t>(_s >> 11);
    }
    uint32_t Below(uint32_t n) { return n ? Next() % n : 0; }
    bool Chance(uint32_t percent) { return Below(100) < percent; }
    int32_t Range(int32_t lo, int32_t hi) { return lo + static_cast<int32_t>(Below(static_cast<uint32_t>(hi - lo + 1))); }
    template <typename T, size_t N>
    T Pick(const T (&values)[N])
    {
        return values[Below(N)];
    }

private:
    uint64_t _s;
};

class Chip
{
public:
    Chip(EveModel model, bool shortcuts) : model(model)
    {
        EveConfig config{};
        config.structSize = sizeof(EveConfig);
        config.model = model;
        config.externalClockHz = kCrystalHz;
        chip = EveCreate(&config);
        if (!chip)
            return;
        if (model == EVE_MODEL_VDAC4)
        {
            EveAdvance(chip, kSweepClocks);
            EveVdac4SetFontsReady(chip, 1);
        }
        if (!shortcuts)
        {
            chip->bitmapFastPath = false;
            chip->rasterSpanFill = false;
            chip->linePlanEnabled = false;
            chip->lineKeepEnabled = false;
        }
        HostCommand(0x44); // CLKEXT
        HostCommand(0x00); // ACTIVE
        EveAdvance(chip, 1000);
        const std::pair<uint32_t, uint32_t> timing[] = {
            {kRegHcycle, kWidth + 40}, {kRegHoffset, 20},      {kRegHsize, kWidth},  {kRegHsync0, 0}, {kRegHsync1, 5},
            {kRegVcycle, kHeight + 8}, {kRegVoffset, 4},       {kRegVsize, kHeight}, {kRegVsync0, 0}, {kRegVsync1, 2},
            {kRegTagX, 17},            {kRegTagY, kHeight / 3}, {kRegPclk, 1}};
        for (const auto& [address, value] : timing)
            Write32(address, value);
        EveSetOutput(chip, frame.data(), kWidth, kWidth, kHeight, 1);
    }
    ~Chip()
    {
        if (chip)
            EveSetAsyncDrawing(chip, 0, nullptr, nullptr);
        EveDestroy(chip);
    }
    Chip(const Chip&) = delete;
    Chip& operator=(const Chip&) = delete;

    void HostCommand(uint8_t command)
    {
        EveSelect(chip, 1);
        EveExchange(chip, command);
        EveExchange(chip, 0);
        EveExchange(chip, 0);
        EveSelect(chip, 0);
    }
    void Write(uint32_t address, const uint8_t* data, size_t size)
    {
        EveSelect(chip, 1);
        EveExchange(chip, static_cast<uint8_t>(0x80 | ((address >> 16) & 0x3F)));
        EveExchange(chip, static_cast<uint8_t>(address >> 8));
        EveExchange(chip, static_cast<uint8_t>(address));
        for (size_t i = 0; i < size; ++i)
            EveExchange(chip, data[i]);
        EveSelect(chip, 0);
    }
    void Write32(uint32_t address, uint32_t value)
    {
        const uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                                  static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
        Write(address, bytes, sizeof(bytes));
    }
    // One whole frame drawn: from the next frame start to the one after
    void RunFrame()
    {
        const uint64_t frames = EveCompletedFrames(chip);
        while (EveCompletedFrames(chip) == frames)
            EveAdvance(chip, std::max<uint64_t>(1, std::min<uint64_t>(EveClocksToNextEvent(chip), 4096)));
    }

    EveModel model;
    EveChip* chip = nullptr;
    std::vector<uint32_t> frame = std::vector<uint32_t>(kWidth * kHeight, 0);
};

// --- Scene generation --------------------------------------------------------------------------------------------

constexpr uint8_t kFormats[] = {0 /*ARGB1555*/, 1 /*L1*/,  2 /*L4*/,  3 /*L8*/,          4 /*RGB332*/,
                                5 /*ARGB2*/,    6 /*ARGB4*/, 7 /*RGB565*/, 14 /*PALETTED565*/, 15 /*PALETTED4444*/,
                                16 /*PALETTED8*/, 17 /*L2*/};

uint32_t BitsPerPixel(uint8_t format)
{
    switch (format)
    {
    case 1: return 1;
    case 17: return 2;
    case 2: return 4;
    case 3: case 4: case 5: case 14: case 15: case 16: return 8;
    default: return 16;
    }
}

struct Scene
{
    std::vector<uint8_t> ramG;
    std::vector<uint32_t> dl;
    std::vector<uint8_t> ramG2;  // the third frame's RAM_G
};

// RAM_G: a mix of noise, flat runs (fully transparent / opaque texels, the sprite case) and gradients
void FillRamG(Random& r, std::vector<uint8_t>& ram)
{
    ram.assign(kSceneRamG, 0);
    for (uint32_t at = 0; at < kSceneRamG;)
    {
        const uint32_t run = std::min<uint32_t>(kSceneRamG - at, 16 + r.Below(600));
        const uint32_t kind = r.Below(5);
        const uint8_t value = kind == 0 ? 0x00 : kind == 1 ? 0xFF : static_cast<uint8_t>(r.Next());
        for (uint32_t i = 0; i < run; ++i)
            ram[at + i] = kind <= 1 ? value : kind == 2 ? static_cast<uint8_t>(i * 7 + value) : static_cast<uint8_t>(r.Next());
        at += run;
    }
}

uint32_t Transform(Random& r, int axis)  // A / E
{
    const int32_t values[] = {256, 256, 256, 128, 512, -256, 64, 384, 255, 257};
    int32_t v = r.Chance(85) ? values[r.Below(sizeof(values) / sizeof(values[0]))] : r.Range(-1024, 1024);
    (void)axis;
    return static_cast<uint32_t>(v) & 0x1FFFF;
}

void BitmapCommands(Random& r, std::vector<uint32_t>& dl, uint32_t handle, bool vdac4)
{
    const uint8_t format = r.Pick(kFormats);
    const uint32_t bpp = BitsPerPixel(format);
    const uint32_t width = 1 + r.Below(r.Chance(30) ? 160 : 48);
    const uint32_t height = 1 + r.Below(r.Chance(30) ? 80 : 40);
    const uint32_t stride = std::max<uint32_t>(1, (width * bpp + 7) / 8 + (r.Chance(20) ? r.Below(8) : 0));
    const uint32_t source = r.Below(kSceneRamG - std::min<uint32_t>(kSceneRamG - 1, stride * height + 4)) & ~1u;
    dl.push_back(0x05000000u | handle);                                                   // BITMAP_HANDLE
    dl.push_back(0x01000000u | source);                                                   // BITMAP_SOURCE
    dl.push_back(0x07000000u | (static_cast<uint32_t>(format) << 19) | ((stride & 0x3FF) << 9) | (height & 0x1FF));
    dl.push_back(0x28000000u | (((stride >> 10) & 3) << 2) | ((height >> 9) & 3));      // BITMAP_LAYOUT_H
    const uint32_t sizeW = r.Chance(70) ? width : 1 + r.Below(200);
    const uint32_t sizeH = r.Chance(70) ? height : 1 + r.Below(100);
    const uint32_t filter = r.Chance(25) ? 1u : 0u;
    const uint32_t wrapX = r.Chance(25) ? 1u : 0u;
    const uint32_t wrapY = r.Chance(25) ? 1u : 0u;
    dl.push_back(0x08000000u | (filter << 20) | (wrapX << 19) | (wrapY << 18) | ((sizeW & 0x1FF) << 9) | (sizeH & 0x1FF));
    dl.push_back(0x29000000u | (((sizeW >> 9) & 3) << 2) | ((sizeH >> 9) & 3));         // BITMAP_SIZE_H
    if (format == 14 || format == 15 || format == 16)
    {
        const uint32_t palette = (r.Below(kSceneRamG - (vdac4 ? 1024 : 512))) & ~3u;
        dl.push_back(0x2A000000u | (palette + (format == 16 && !vdac4 ? r.Below(4) : 0)));  // PALETTE_SOURCE
    }
    // The matrix: identity most of the time (the common case), else scaled, mirrored, rotated
    dl.push_back(0x15000000u | Transform(r, 0));                                          // A
    dl.push_back(0x16000000u | (r.Chance(15) ? static_cast<uint32_t>(r.Range(-200, 200)) & 0x1FFFF : 0));  // B
    dl.push_back(0x17000000u | (static_cast<uint32_t>(r.Range(-2000, 2000)) & 0xFFFFFF)); // C
    dl.push_back(0x18000000u | (r.Chance(15) ? static_cast<uint32_t>(r.Range(-200, 200)) & 0x1FFFF : 0));  // D
    dl.push_back(0x19000000u | Transform(r, 1));                                          // E
    dl.push_back(0x1A000000u | (static_cast<uint32_t>(r.Range(-2000, 2000)) & 0xFFFFFF)); // F
}

void StateCommands(Random& r, std::vector<uint32_t>& dl)
{
    const uint32_t count = r.Below(4);
    for (uint32_t i = 0; i < count; ++i)
    {
        switch (r.Below(12))
        {
        case 0: dl.push_back(0x04000000u | (r.Next() & 0xFFFFFF)); break;                   // COLOR_RGB
        case 1: dl.push_back(0x10000000u | (r.Chance(50) ? 255 : r.Below(256))); break;     // COLOR_A
        case 2: dl.push_back(0x0B000000u | (r.Below(6) << 3) | r.Below(6)); break;         // BLEND_FUNC
        case 3: dl.push_back(0x0B000000u | (2u << 3) | 4u); break;                          // default blend
        case 4: dl.push_back(0x09000000u | (r.Below(8) << 8) | r.Below(256)); break;        // ALPHA_FUNC
        case 5: dl.push_back(0x0A000000u | (r.Below(8) << 16) | (r.Below(256) << 8) | 0xFF); break;  // STENCIL_FUNC
        case 6: dl.push_back(0x0C000000u | (r.Below(6) << 3) | r.Below(6)); break;         // STENCIL_OP
        case 7: dl.push_back(0x20000000u | r.Below(16)); break;                             // COLOR_MASK
        case 8: dl.push_back(0x1B000000u | (r.Below(kWidth) << 11) | r.Below(kHeight)); break;  // SCISSOR_XY
        case 9: dl.push_back(0x1C000000u | ((1 + r.Below(kWidth + 20)) << 12) | (1 + r.Below(kHeight + 10))); break;
        case 10: dl.push_back(0x03000000u | r.Below(256)); break;                           // TAG
        default: dl.push_back(r.Chance(50) ? 0x22000000u : 0x23000000u); break;             // SAVE / RESTORE_CONTEXT
        }
    }
}

uint32_t Vertex(Random& r)
{
    if (r.Chance(50))  // VERTEX2II: pixel coordinates, a handle, a cell
        return 0x80000000u | ((r.Below(kWidth + 20) & 0x1FF) << 21) | ((r.Below(kHeight + 10) & 0x1FF) << 12) |
               (r.Below(4) << 7) | r.Below(3);
    const int32_t x = r.Range(-40 * 16, (static_cast<int32_t>(kWidth) + 40) * 16);
    const int32_t y = r.Range(-20 * 16, (static_cast<int32_t>(kHeight) + 20) * 16);
    return 0x40000000u | ((static_cast<uint32_t>(x) & 0x7FFF) << 15) | (static_cast<uint32_t>(y) & 0x7FFF);  // VERTEX2F
}

Scene MakeScene(uint64_t seed, bool vdac4)
{
    Random r(seed);
    Scene s;
    FillRamG(r, s.ramG);
    s.ramG2 = s.ramG;
    for (uint32_t i = 0; i < 64; ++i)  // the third frame: a few palette / texel changes
        s.ramG2[r.Below(kSceneRamG)] = static_cast<uint8_t>(r.Next());

    auto& dl = s.dl;
    dl.push_back(0x02000000u | (r.Next() & 0xFFFFFF));  // CLEAR_COLOR_RGB
    dl.push_back(0x0F000000u | r.Below(256));           // CLEAR_COLOR_A
    dl.push_back(0x26000007u);                          // CLEAR(1, 1, 1)
    for (uint32_t h = 0; h < 4; ++h)
        BitmapCommands(r, dl, h, vdac4);
    const uint32_t primitives[] = {1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9};
    const uint32_t groups = 2 + r.Below(8);
    for (uint32_t g = 0; g < groups && dl.size() < 1800; ++g)
    {
        StateCommands(r, dl);
        const uint32_t primitive = r.Pick(primitives);
        if (primitive == 1 && r.Chance(30))
            BitmapCommands(r, dl, r.Below(4), vdac4);  // a handle redefined between draws
        if (primitive == 2)
            dl.push_back(0x0D000000u | (1 + r.Below(200)));  // POINT_SIZE
        if (primitive >= 3 && primitive <= 9)
            dl.push_back(0x0E000000u | (1 + r.Below(120)));  // LINE_WIDTH
        dl.push_back(0x1F000000u | primitive);               // BEGIN
        if (primitive == 1)
            dl.push_back(0x05000000u | r.Below(4));          // BITMAP_HANDLE for VERTEX2F
        const uint32_t vertices = 1 + r.Below(primitive == 1 ? 6 : 8);
        for (uint32_t v = 0; v < vertices; ++v)
        {
            if (primitive == 1 && r.Chance(20))
                dl.push_back(0x06000000u | r.Below(3));      // CELL
            dl.push_back(Vertex(r));
        }
        dl.push_back(0x21000000u);                           // END
    }
    dl.push_back(0x00000000u);                               // DISPLAY
    return s;
}

// --- Drawing ----------------------------------------------------------------------------------------------------

void Load(Chip& c, const std::vector<uint8_t>& ramG)
{
    c.Write(0, ramG.data(), ramG.size());
}

void LoadDisplayList(Chip& c, const std::vector<uint32_t>& dl)
{
    std::vector<uint8_t> bytes(dl.size() * 4);
    for (size_t i = 0; i < dl.size(); ++i)
    {
        bytes[4 * i] = static_cast<uint8_t>(dl[i]);
        bytes[4 * i + 1] = static_cast<uint8_t>(dl[i] >> 8);
        bytes[4 * i + 2] = static_cast<uint8_t>(dl[i] >> 16);
        bytes[4 * i + 3] = static_cast<uint8_t>(dl[i] >> 24);
    }
    c.Write(kRamDl, bytes.data(), bytes.size());
    c.Write32(kRegDlswap, 2);  // at the frame end
}

// Frame 1: the scene; frame 2: unchanged (kept lines); frame 3: RAM_G changed under the same list
std::vector<std::vector<uint32_t>> DrawScene(Chip& c, const Scene& s)
{
    std::vector<std::vector<uint32_t>> frames;
    Load(c, s.ramG);
    LoadDisplayList(c, s.dl);
    c.RunFrame();  // the swap happens at this frame's end
    c.RunFrame();
    EveDrawingBarrier(c.chip);
    frames.push_back(c.frame);
    c.RunFrame();
    EveDrawingBarrier(c.chip);
    frames.push_back(c.frame);
    Load(c, s.ramG2);
    c.RunFrame();
    EveDrawingBarrier(c.chip);
    frames.push_back(c.frame);
    return frames;
}

std::string FirstDifference(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)
{
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i])
        {
            char text[160];
            std::snprintf(text, sizeof(text), "pixel (%zu, %zu): shortcuts %08X, reference %08X", i % kWidth, i / kWidth,
                          a[i], b[i]);
            return text;
        }
    return "same";
}

uint64_t FuzzSeeds()
{
    return TestTier::Long() ? 3000 : 3;
}

void Fuzz(EveModel model, bool async, uint32_t threads = 1)
{
    const char* only = std::getenv("UNREAL_EVE_FUZZ_SEED");
    const uint64_t firstSeed = only ? std::strtoull(only, nullptr, 10) : 1;
    const uint64_t seeds = only ? 1 : FuzzSeeds();
    size_t richest = 0;  // the scenes must draw pictures, not a cleared screen
    uint64_t parallelLines = 0;
    uint64_t repeatedLines = 0;
    for (uint64_t seed = firstSeed; seed < firstSeed + seeds; ++seed)
    {
        const Scene scene = MakeScene(seed, model == EVE_MODEL_VDAC4);
        Chip fast(model, true);
        Chip reference(model, false);
        ASSERT_NE(fast.chip, nullptr);
        ASSERT_NE(reference.chip, nullptr);
        if (threads > 1)
        {
            ASSERT_EQ(EveSetDrawThreads(fast.chip, threads), threads);
            fast.chip->parallelMinLines = 2;  // the scenes' catch-ups are a few lines each
        }
        if (async)
            ASSERT_EQ(EveSetAsyncDrawing(fast.chip, 1, nullptr, nullptr), 0) << EveLastError();
        const auto a = DrawScene(fast, scene);
        const auto b = DrawScene(reference, scene);
        for (size_t f = 0; f < a.size(); ++f)
            ASSERT_EQ(a[f], b[f]) << "seed " << seed << " frame " << f + 1 << ": " << FirstDifference(a[f], b[f])
                                  << " (rerun: UNREAL_EVE_FUZZ_SEED=" << seed << ")";
        parallelLines += fast.chip->parallelLines;
        repeatedLines += fast.chip->linePlan->repeat.hits;
        std::vector<uint32_t> colors = a[0];
        std::sort(colors.begin(), colors.end());
        richest = std::max<size_t>(richest, static_cast<size_t>(std::unique(colors.begin(), colors.end()) - colors.begin()));
    }
    EXPECT_GE(richest, 16u) << "no scene drew more than a few colors";
    if (threads > 1 && !async)
        EXPECT_GT(parallelLines, 0u) << "no line went to the line workers";
    if (threads == 1 && !async)
        EXPECT_GT(repeatedLines, 0u) << "no line repeated the one before it (LineRepeat)";
}

} // namespace

TEST(EveDrawFuzz_Test, Ft812ShortcutsDrawWhatTheReferenceDraws)
{
    Fuzz(EVE_MODEL_FT812, false);
}

TEST(EveDrawFuzz_Test, Vdac4ShortcutsDrawWhatTheReferenceDraws)
{
    Fuzz(EVE_MODEL_VDAC4, false);
}

TEST(EveDrawFuzz_Test, RendererThreadDrawsWhatTheReferenceDraws)
{
    Fuzz(EVE_MODEL_VDAC4, true);
}

TEST(EveDrawFuzz_Test, LineWorkersDrawWhatTheReferenceDraws)
{
    Fuzz(EVE_MODEL_VDAC4, false, 4);
}

TEST(EveDrawFuzz_Test, RendererLineWorkersDrawWhatTheReferenceDraws)
{
    Fuzz(EVE_MODEL_FT812, true, 3);
}
