// eve-emu VDAC4 profile (EVE_MODEL_VDAC4): the board's differences from the FT812, through
// the public C API only (eve/eve.h). The FT812 side of every rule is checked too: it must
// not change.

#include "eve/eve.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{

constexpr uint32_t kCrystalHz = 8000000;
constexpr uint32_t kRegId = 0x302000;
constexpr uint32_t kRegFrequency = 0x30200C;
constexpr uint32_t kRegHcycle = 0x30202C;
constexpr uint32_t kRegHoffset = 0x302030;
constexpr uint32_t kRegHsize = 0x302034;
constexpr uint32_t kRegVcycle = 0x302040;
constexpr uint32_t kRegVoffset = 0x302044;
constexpr uint32_t kRegVsize = 0x302048;
constexpr uint32_t kRegDlswap = 0x302054;
constexpr uint32_t kRegDither = 0x302060;
constexpr uint32_t kRegCspread = 0x302068;
constexpr uint32_t kRegPclk = 0x302070;
constexpr uint32_t kRegIntFlags = 0x3020A8;
constexpr uint32_t kRegCmdRead = 0x3020F8;
constexpr uint32_t kRegCmdWrite = 0x3020FC;
constexpr uint32_t kRamDl = 0x300000;
constexpr uint32_t kRamCmd = 0x308000;
constexpr uint32_t kRomFontRoot = 0x2FFFFC;
constexpr uint8_t kHostRstPulse = 0x68;
constexpr uint32_t kIntSwap = 1;
constexpr uint32_t kSweepClocks = 128;

// The ROM image the tests give the chip: 0x200000...0x2FFFFF (the end of the FT81x ROM),
// with ROM_FONTROOT and one usable metric block for font 26.
constexpr uint32_t kRomStart = 0x200000;
constexpr uint32_t kRomBytes = 0x100000;
constexpr uint32_t kRootBlocks = 0x201000;
constexpr uint32_t kFontGlyphs = 0x202000;
constexpr uint32_t kMetricBlockBytes = 148;

void Put32(std::vector<uint8_t>& rom, uint32_t address, uint32_t value)
{
    for (uint32_t i = 0; i < 4; ++i)
        rom[address - kRomStart + i] = static_cast<uint8_t>(value >> (8 * i));
}

std::vector<uint8_t> MakeRom()
{
    std::vector<uint8_t> rom(kRomBytes);
    for (uint32_t i = 0; i < 0x1000; ++i)
        rom[i] = static_cast<uint8_t>(0xA0 + (i & 0x0F)); // a pattern at 0x200000
    Put32(rom, kRomFontRoot, kRootBlocks);
    for (uint32_t font = 16; font < 32; ++font)
    {
        const uint32_t block = kRootBlocks + kMetricBlockBytes * (font - 16);
        for (uint32_t ch = 32; ch < 127; ++ch)
            rom[block - kRomStart + ch] = 8;
        Put32(rom, block + 128, 1);  // L1
        Put32(rom, block + 132, 1);  // stride
        Put32(rom, block + 136, 8);  // width
        Put32(rom, block + 140, 8);  // height
        Put32(rom, block + 144, kFontGlyphs);
    }
    return rom;
}

class Chip
{
public:
    explicit Chip(EveModel model, const std::vector<uint8_t>* rom = nullptr)
    {
        EveConfig config{};
        config.structSize = sizeof(EveConfig);
        config.model = model;
        config.externalClockHz = kCrystalHz;
        if (rom != nullptr)
        {
            config.romImage = rom->data();
            config.romImageSize = rom->size();
        }
        chip = EveCreate(&config);
        if (chip != nullptr && model == EVE_MODEL_VDAC4)
            EveAdvance(chip, kSweepClocks); // the power-up register sweep
    }
    ~Chip() { EveDestroy(chip); }
    Chip(const Chip&) = delete;
    Chip& operator=(const Chip&) = delete;

    // The FT812 boot of the SDK: ACTIVE, then wait for REG_ID
    void BootFt812()
    {
        HostCommand(0x44); // CLKEXT
        HostCommand(0x00); // ACTIVE
        EveAdvance(chip, 1000000);
    }

    void HostCommand(uint8_t command, uint8_t parameter = 0)
    {
        EveSelect(chip, 1);
        EveExchange(chip, command);
        EveExchange(chip, parameter);
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

    void Write8(uint32_t address, uint8_t value) { Write(address, &value, 1); }

    uint32_t Read32(uint32_t address)
    {
        EveSelect(chip, 1);
        EveExchange(chip, static_cast<uint8_t>((address >> 16) & 0x3F));
        EveExchange(chip, static_cast<uint8_t>(address >> 8));
        EveExchange(chip, static_cast<uint8_t>(address));
        EveExchange(chip, 0); // dummy
        uint32_t value = 0;
        for (uint32_t i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(EveExchange(chip, 0)) << (8 * i);
        EveSelect(chip, 0);
        return value;
    }

    uint32_t Peek32(uint32_t address) const
    {
        uint32_t value = 0;
        for (uint32_t i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(EvePeek(chip, address + i)) << (8 * i);
        return value;
    }

    // Commands into the ring, then run until the coprocessor is idle, faulted or `clocks` pass
    void Commands(const std::vector<uint32_t>& words, uint64_t clocks = 2000000)
    {
        uint32_t wp = Peek32(kRegCmdWrite);
        for (uint32_t word : words)
        {
            Write32(kRamCmd + wp, word);
            wp = (wp + 4) & 0xFFF;
        }
        Write32(kRegCmdWrite, wp);
        Run(clocks);
    }

    // Words through REG_CMDB_WRITE as a host streams a file (the VDAC4 DOOM launcher's JPEG): as much as
    // REG_CMDB_SPACE allows, the chip runs between bursts; false: no room for `clocks`
    bool Stream(const std::vector<uint32_t>& words, uint64_t clocks = 64000000)
    {
        constexpr uint32_t kRegCmdbSpace = 0x302574;
        constexpr uint32_t kRegCmdbWrite = 0x302578;
        constexpr uint64_t kStep = 20000;
        size_t next = 0;
        for (uint64_t waited = 0; next < words.size();)
        {
            const uint32_t space = Read32(kRegCmdbSpace) & 0xFFC;
            if (space == 0)
            {
                if (waited >= clocks || Phase() == EVE_COPRO_FAULT)
                    return false;
                EveAdvance(chip, kStep);
                waited += kStep;
                continue;
            }
            const size_t n = std::min<size_t>(space / 4, words.size() - next);
            std::vector<uint8_t> bytes(n * 4);
            std::memcpy(bytes.data(), words.data() + next, bytes.size());
            Write(kRegCmdbWrite, bytes.data(), bytes.size());
            next += n;
        }
        return true;
    }

    void Run(uint64_t clocks)
    {
        constexpr uint64_t kStep = 20000;
        for (uint64_t done = 0; done < clocks; done += kStep)
        {
            EveAdvance(chip, kStep);
            const EveCoproPhase phase = Phase();
            if (phase == EVE_COPRO_IDLE || phase == EVE_COPRO_FAULT)
                return;
        }
    }

    EveCoproPhase Phase() const
    {
        EveCoproView view{};
        EveGetCoprocessor(chip, &view);
        return view.phase;
    }

    EveCoproView View() const
    {
        EveCoproView view{};
        EveGetCoprocessor(chip, &view);
        return view;
    }

    EveChip* chip = nullptr;
};

uint32_t Str(const char* text, std::vector<uint32_t>& out)
{
    const size_t n = std::strlen(text) + 1;
    for (size_t i = 0; i < n; i += 4)
    {
        uint32_t word = 0;
        for (size_t k = 0; k < 4 && i + k < n; ++k)
            word |= static_cast<uint32_t>(static_cast<uint8_t>(text[i + k])) << (8 * k);
        out.push_back(word);
    }
    return static_cast<uint32_t>(n);
}

// --- A small stored PNG (8-bit gray), built in the test ---------------------------------------

uint32_t Crc32(const uint8_t* p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i)
    {
        c ^= p[i];
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void Be32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int i = 3; i >= 0; --i)
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void Chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data)
{
    Be32(png, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    Be32(png, Crc32(body.data(), body.size()));
}

std::vector<uint8_t> GrayPng(uint32_t w, uint32_t h, const std::vector<uint8_t>& pixels)
{
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    Be32(ihdr, w);
    Be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 0, 0, 0, 0}); // 8-bit gray
    Chunk(png, "IHDR", ihdr);
    std::vector<uint8_t> raw;
    for (uint32_t y = 0; y < h; ++y)
    {
        raw.push_back(0); // filter none
        raw.insert(raw.end(), pixels.begin() + y * w, pixels.begin() + (y + 1) * w);
    }
    // zlib with stored deflate blocks (at most 65535 bytes each)
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t pos = 0; pos < raw.size();)
    {
        const size_t n = std::min<size_t>(raw.size() - pos, 65535);
        const bool last = pos + n == raw.size();
        z.insert(z.end(), {static_cast<uint8_t>(last ? 1 : 0), static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8),
                           static_cast<uint8_t>(~n), static_cast<uint8_t>(~n >> 8)});
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
                 raw.begin() + static_cast<std::ptrdiff_t>(pos + n));
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw)
    {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    Be32(z, (b << 16) | a);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});
    return png;
}

std::vector<uint32_t> Words(const std::vector<uint8_t>& bytes)
{
    std::vector<uint32_t> words((bytes.size() + 3) / 4);
    for (size_t i = 0; i < bytes.size(); ++i)
        words[i / 4] |= static_cast<uint32_t>(bytes[i]) << (8 * (i % 4));
    return words;
}

} // namespace

// --- Model, power-up, memory map -------------------------------------------------------------

TEST(EveVdac4_Test, PowerUpIsActiveAt64MHzWithIdReady)
{
    Chip vdac4(EVE_MODEL_VDAC4);
    ASSERT_NE(vdac4.chip, nullptr);
    EXPECT_EQ(EveSystemClockHz(vdac4.chip), 8u * kCrystalHz);
    EXPECT_EQ(vdac4.Read32(kRegId) & 0xFF, 0x7Cu);
    EXPECT_EQ(vdac4.Read32(kRegFrequency), 60000000u);
    EXPECT_EQ(vdac4.Peek32(0xC0000), Chip(EVE_MODEL_FT812).Peek32(0xC0000)); // the FT812 chip ID

    // FT812: sleeping until ACTIVE
    Chip ft812(EVE_MODEL_FT812);
    EXPECT_EQ(EveSystemClockHz(ft812.chip), 0u);
}

TEST(EveVdac4_Test, Ft812KeepsItsMapAndRefusesTheVdac4Settings)
{
    Chip ft812(EVE_MODEL_FT812);
    EXPECT_EQ(EveRamGLimit(ft812.chip), 0x100000u);
    EXPECT_NE(EveSetRamGLimit(ft812.chip, EVE_VDAC4_RAMG_2MB), 0);
    EXPECT_EQ(EveSetRamGLimit(ft812.chip, 0x100000u), 0);
    EXPECT_EQ(EveVdac4FontsReady(ft812.chip), 1);
    EXPECT_EQ(EveRegionCount(ft812.chip), 7u);

    Chip vdac4(EVE_MODEL_VDAC4);
    EXPECT_EQ(EveRegionCount(vdac4.chip), 8u);
    EveRegion region{};
    EveGetRegion(vdac4.chip, 0, &region);
    EXPECT_EQ(region.size, 0x300000u);
    EveGetRegion(vdac4.chip, 7, &region);
    EXPECT_STREQ(region.name, "VDAC4_3D");
    EXPECT_GT(EveStateSize(vdac4.chip), EveStateSize(ft812.chip));
}

TEST(EveVdac4_Test, RamG2MbShowsTheRomReadOnlyAfterTheFontCopy)
{
    const std::vector<uint8_t> rom = MakeRom();
    Chip vdac4(EVE_MODEL_VDAC4, &rom);
    EXPECT_EQ(EveRamGLimit(vdac4.chip), EVE_VDAC4_RAMG_2MB);
    vdac4.Write32(0x1FFFFC, 0x12345678);
    EXPECT_EQ(vdac4.Read32(0x1FFFFC), 0x12345678u);
    // Before the copy the ROM reads 0, also ROM_FONTROOT
    EXPECT_EQ(EveVdac4FontsReady(vdac4.chip), 0);
    EXPECT_EQ(vdac4.Read32(kRomStart), 0u);
    EXPECT_EQ(vdac4.Read32(kRomFontRoot), 0u);
    EveVdac4SetFontsReady(vdac4.chip, 1);
    EXPECT_EQ(vdac4.Read32(kRomStart), 0xA3A2A1A0u);
    EXPECT_EQ(vdac4.Read32(kRomFontRoot), kRootBlocks);
    vdac4.Write32(kRomStart, 0);           // read-only
    EXPECT_EQ(vdac4.Read32(kRomStart), 0xA3A2A1A0u);
}

TEST(EveVdac4_Test, RamG3MbIsRamAndSurvivesReset)
{
    const std::vector<uint8_t> rom = MakeRom();
    Chip vdac4(EVE_MODEL_VDAC4, &rom);
    EveVdac4SetFontsReady(vdac4.chip, 1);
    EXPECT_NE(EveSetRamGLimit(vdac4.chip, 0x280000), 0);
    ASSERT_EQ(EveSetRamGLimit(vdac4.chip, EVE_VDAC4_RAMG_3MB), 0);
    vdac4.Write32(0x250000, 0xCAFEF00D);
    EXPECT_EQ(vdac4.Read32(0x250000), 0xCAFEF00Du);
    EXPECT_EQ(vdac4.Read32(kRomStart), 0u); // no ROM fonts in 3 MB mode
    EveReset(vdac4.chip);
    EXPECT_EQ(EveRamGLimit(vdac4.chip), EVE_VDAC4_RAMG_3MB);
    EXPECT_EQ(EveVdac4FontsReady(vdac4.chip), 0);
}

TEST(EveVdac4_Test, CoreResetKeepsDlAndCmdAndIgnoresTheHostFor128Clocks)
{
    Chip vdac4(EVE_MODEL_VDAC4);
    vdac4.Write32(kRamDl + 8, 0x11223344);
    vdac4.Write32(kRamCmd + 16, 0x55667788);
    vdac4.Write32(0x1000, 0x99AABBCC);
    vdac4.HostCommand(kHostRstPulse);
    EXPECT_EQ(vdac4.Read32(kRegId), 0u);  // reads in the sweep: 0
    vdac4.Write32(0x2000, 0xDEADBEEF);    // dropped
    EveAdvance(vdac4.chip, kSweepClocks);
    EXPECT_EQ(vdac4.Read32(kRegId) & 0xFF, 0x7Cu);
    EXPECT_EQ(vdac4.Read32(0x2000), 0u);
    EXPECT_EQ(vdac4.Read32(kRamDl + 8), 0x11223344u);
    EXPECT_EQ(vdac4.Read32(kRamCmd + 16), 0x55667788u);
    EXPECT_EQ(vdac4.Read32(0x1000), 0x99AABBCCu);

    // FT812: RST_PULSE clears RAM_DL and RAM_CMD
    Chip ft812(EVE_MODEL_FT812);
    ft812.BootFt812();
    ft812.Write32(kRamDl + 8, 0x11223344);
    ft812.HostCommand(kHostRstPulse);
    EXPECT_EQ(ft812.Peek32(kRamDl + 8), 0u);
}

TEST(EveVdac4_Test, StateRoundTripKeepsTheVdac4Block)
{
    Chip vdac4(EVE_MODEL_VDAC4);
    ASSERT_EQ(EveSetRamGLimit(vdac4.chip, EVE_VDAC4_RAMG_3MB), 0);
    EveVdac4SetFontsReady(vdac4.chip, 1);
    std::vector<uint8_t> state(EveStateSize(vdac4.chip));
    EveSaveState(vdac4.chip, state.data());

    Chip other(EVE_MODEL_VDAC4);
    ASSERT_EQ(EveLoadState(other.chip, state.data(), state.size()), 0) << EveLastError();
    EXPECT_EQ(EveRamGLimit(other.chip), EVE_VDAC4_RAMG_3MB);
    EXPECT_EQ(EveVdac4FontsReady(other.chip), 1);

    Chip ft812(EVE_MODEL_FT812);
    EXPECT_NE(EveLoadState(ft812.chip, state.data(), state.size()), 0); // other size
    state[EveStateSize(ft812.chip)] ^= 0xFF; // the VDAC4 block (after the control state): its magic
    EXPECT_NE(EveLoadState(other.chip, state.data(), state.size()), 0);
}

// --- Rendering --------------------------------------------------------------------------------

TEST(EveVdac4_Test, Paletted8IsOneArgbEntryAtAWordAlignedSource)
{
    Chip vdac4(EVE_MODEL_VDAC4);
    constexpr uint32_t kPixels = 0x1000, kPalette = 0x2000;
    vdac4.Write8(kPixels, 5);
    const uint8_t entry[4] = {0x30, 0x60, 0x90, 0xFF}; // B, G, R, A
    vdac4.Write(kPalette + 4 * 5, entry, sizeof(entry));
    const uint32_t dl[] = {
        0x02000000,                          // CLEAR_COLOR_RGB 0
        0x26000007,                          // CLEAR
        0x01000000 | kPixels,                // BITMAP_SOURCE
        0x07000000 | (16u << 19) | (1u << 9) | 1u, // BITMAP_LAYOUT PALETTED8, stride 1, height 1
        0x08000000 | (1u << 9) | 1u,         // BITMAP_SIZE 1 x 1
        0x2A000000 | (kPalette + 2),         // PALETTE_SOURCE: the low 2 bits are ignored
        0x1F000001,                          // BEGIN BITMAPS
        0x80000000 | (10u << 21) | (10u << 12), // VERTEX2II 10, 10
        0x21000000,                          // END
        0x00000000,                          // DISPLAY
    };
    for (uint32_t i = 0; i < sizeof(dl) / sizeof(dl[0]); ++i)
        vdac4.Write32(kRamDl + 4 * i, dl[i]);
    vdac4.Write32(kRegHcycle, 100);
    vdac4.Write32(kRegHoffset, 10);
    vdac4.Write32(kRegHsize, 64);
    vdac4.Write32(kRegVcycle, 40);
    vdac4.Write32(kRegVoffset, 4);
    vdac4.Write32(kRegVsize, 32);
    vdac4.Write32(kRegDither, 0);
    vdac4.Write32(kRegCspread, 0); // no channel spread: R, G, B in the same pixel
    vdac4.Write32(kRegDlswap, 2);
    vdac4.Write32(kRegPclk, 2);
    std::vector<uint32_t> frame(64 * 32);
    EveSetOutput(vdac4.chip, frame.data(), 64, 64, 32, 1);
    EveTiming timing{};
    EveGetTiming(vdac4.chip, &timing);
    ASSERT_GT(timing.framePeriodClocks, 0u);
    EveAdvance(vdac4.chip, 3 * timing.framePeriodClocks);
    EXPECT_EQ(frame[10 * 64 + 10] & 0xFFFFFFu, 0x906030u);
    EXPECT_EQ(frame[10 * 64 + 11] & 0xFFFFFFu, 0u);
}

// The HMM2 launcher's error screen: an 8 x 8 L1 glyph in RAM_G, magnified 2x by BITMAP_TRANSFORM A = E = 0.5
TEST(EveVdac4_Test, TransformMagnifiesAnL1GlyphOverItsCell)
{
    for (const EveModel model : {EVE_MODEL_VDAC4, EVE_MODEL_FT812})
    {
        Chip chip(model);
        if (model == EVE_MODEL_FT812)
            chip.BootFt812();
        uint8_t glyph[8];
        for (uint32_t row = 0; row < 8; ++row)
            glyph[row] = static_cast<uint8_t>(0x80u >> row); // a diagonal
        chip.Write(0, glyph, sizeof(glyph));
        constexpr uint32_t kX = 16, kY = 8;
        const uint32_t dl[] = {
            0x02000000,                               // CLEAR_COLOR_RGB 0
            0x26000007,                               // CLEAR
            0x04FFFFFF,                               // COLOR_RGB white
            0x27000004,                               // VERTEX_FORMAT 4
            0x07080208,                               // BITMAP_LAYOUT L1, stride 1, height 8
            0x08002010,                               // BITMAP_SIZE 16 x 16
            0x15000080,                               // BITMAP_TRANSFORM_A 0.5
            0x19000080,                               // BITMAP_TRANSFORM_E 0.5
            0x1F000001,                               // BEGIN BITMAPS
            0x01000000,                               // BITMAP_SOURCE 0
            0x40000000 | (kX * 16u << 15) | (kY * 16u), // VERTEX2F
            0x21000000,                               // END
            0x00000000,                               // DISPLAY
        };
        for (uint32_t i = 0; i < sizeof(dl) / sizeof(dl[0]); ++i)
            chip.Write32(kRamDl + 4 * i, dl[i]);
        chip.Write32(kRegHcycle, 100);
        chip.Write32(kRegHoffset, 10);
        chip.Write32(kRegHsize, 64);
        chip.Write32(kRegVcycle, 40);
        chip.Write32(kRegVoffset, 4);
        chip.Write32(kRegVsize, 32);
        chip.Write32(kRegDither, 0);
        chip.Write32(kRegCspread, 0);
        chip.Write32(kRegDlswap, 2);
        chip.Write32(kRegPclk, 2);
        std::vector<uint32_t> frame(64 * 32);
        EveSetOutput(chip.chip, frame.data(), 64, 64, 32, 1);
        EveTiming timing{};
        EveGetTiming(chip.chip, &timing);
        ASSERT_GT(timing.framePeriodClocks, 0u);
        EveAdvance(chip.chip, 3 * timing.framePeriodClocks);
        std::string picture;
        uint32_t wrong = 0;
        for (uint32_t y = 0; y < 16; ++y)
        {
            for (uint32_t x = 0; x < 16; ++x)
            {
                const bool lit = (frame[(kY + y) * 64 + kX + x] & 0xFFFFFFu) != 0;
                picture += lit ? '#' : '.';
                wrong += lit != (x / 2 == y / 2) ? 1u : 0u;
            }
            picture += '\n';
        }
        EXPECT_EQ(wrong, 0u) << (model == EVE_MODEL_VDAC4 ? "VDAC4" : "FT812") << "\n" << picture;
    }
}

// --- Coprocessor ------------------------------------------------------------------------------

TEST(EveVdac4_Test, CommandsTheBl616LacksFaultAsUnknown)
{
    for (uint32_t code : {0xFFFFFF3Bu /* SETFONT2 */, 0xFFFFFF3Fu /* ROMFONT */, 0xFFFFFF3Au /* PLAYVIDEO */,
                          0xFFFFFF04u, 0xFFFFFF07u})
    {
        Chip vdac4(EVE_MODEL_VDAC4);
        vdac4.Commands({code, 0, 0, 0});
        EXPECT_EQ(vdac4.Phase(), EVE_COPRO_FAULT) << std::hex << code;
        EXPECT_EQ(vdac4.Read32(kRegCmdRead), 0xFFFu);
    }
    // FT812: CMD_SETFONT2 is a command
    Chip ft812(EVE_MODEL_FT812);
    ft812.BootFt812();
    ft812.Commands({0xFFFFFF3Bu, 1, 0x1000, 32});
    EXPECT_NE(ft812.Phase(), EVE_COPRO_FAULT);
    // and the VDAC4 commands are unknown to it
    ft812.Commands({0xFFFFFFB1u, 0, 0, 0, 0});
    EXPECT_EQ(ft812.Phase(), EVE_COPRO_FAULT);
}

TEST(EveVdac4_Test, ProgressDrawsTheBl616BarWords)
{
    Chip vdac4(EVE_MODEL_VDAC4);
    vdac4.Commands({0xFFFFFF00u /* DLSTART */, 0xFFFFFF09u, 0x223344u /* BGCOLOR */,
                    0xFFFFFF0Fu, (20u << 16) | 10u, (8u << 16) | 100u, (50u << 16) | 0u, 100u});
    ASSERT_EQ(vdac4.Phase(), EVE_COPRO_IDLE);
    // x 10, y 20, w 100, h 8: horizontal; t = 8, a = 40, len = 400, cc = 96, vb = 240
    const auto v2f = [](uint32_t x, uint32_t y) { return 0x40000000u | ((x & 0x7FFF) << 15) | (y & 0x7FFF); };
    const uint32_t expect[] = {0x22000000, 0x27000002, 0x0E000000 | 64, 0x1F000003, 0x04FFFFFF, v2f(42, 98),
                               v2f(442, 98), 0x04000000, v2f(37, 93), v2f(437, 93), 0x04223344, v2f(40, 96),
                               v2f(440, 96), 0x23000000, 0x22000000, 0x27000002, 0x0E000000 | 56, v2f(40, 96),
                               v2f(240, 96), 0x23000000};
    for (uint32_t i = 0; i < sizeof(expect) / sizeof(expect[0]); ++i)
        EXPECT_EQ(vdac4.Peek32(kRamDl + 4 * i), expect[i]) << i;
}

TEST(EveVdac4_Test, TextWaitsForTheRomFontsThenDraws)
{
    const std::vector<uint8_t> rom = MakeRom();
    Chip vdac4(EVE_MODEL_VDAC4, &rom);
    std::vector<uint32_t> cmd = {0xFFFFFF00u, 0xFFFFFF0Cu, (10u << 16) | 10u, 26u};
    Str("AB", cmd);
    vdac4.Commands(cmd, 64000 * 20); // 20 ms: still waiting
    EXPECT_EQ(vdac4.Phase(), EVE_COPRO_EXECUTING);
    EXPECT_EQ(vdac4.View().command, 0xFFFFFF0Cu);
    EveVdac4SetFontsReady(vdac4.chip, 1);
    vdac4.Run(64000 * 10);
    EXPECT_EQ(vdac4.Phase(), EVE_COPRO_IDLE);
    EXPECT_GT(vdac4.View().cmdDl, 0u);
}

TEST(EveVdac4_Test, TextGivesUpWaitingAfterSixSeconds)
{
    Chip vdac4(EVE_MODEL_VDAC4);
    std::vector<uint32_t> cmd = {0xFFFFFF00u, 0xFFFFFF0Cu, (10u << 16) | 10u, 26u};
    Str("A", cmd);
    vdac4.Commands(cmd, 64000ull * 5000);
    EXPECT_EQ(vdac4.Phase(), EVE_COPRO_EXECUTING);
    vdac4.Run(64000ull * 2100);
    EXPECT_EQ(vdac4.Phase(), EVE_COPRO_IDLE);
}

TEST(EveVdac4_Test, LoadImageBoxScalesByAreaAndReportsImageInfo)
{
    Chip vdac4(EVE_MODEL_VDAC4);
    // 4 x 2 gray, box 2 x 1: each output pixel averages a 2 x 2 block
    const std::vector<uint8_t> png = GrayPng(4, 2, {0, 100, 10, 20, 200, 100, 30, 40});
    std::vector<uint32_t> cmd = {0xFFFFFF00u, 0xFFFFFFB0u, 0x1000, 0, (1u << 16) | 2u, 0x10000};
    const std::vector<uint32_t> file = Words(png);
    cmd.insert(cmd.end(), file.begin(), file.end());
    cmd.insert(cmd.end(), {0xFFFFFFB1u, 0, 0, 0, 0});
    vdac4.Commands(cmd);
    ASSERT_EQ(vdac4.Phase(), EVE_COPRO_IDLE) << vdac4.View().faultReason;
    EXPECT_EQ(EvePeek(vdac4.chip, 0x1000), 100); // (0 + 100 + 200 + 100) / 4
    EXPECT_EQ(EvePeek(vdac4.chip, 0x1001), 25);  // (10 + 20 + 30 + 40) / 4
    const uint32_t rp = vdac4.Peek32(kRegCmdRead);
    EXPECT_EQ(vdac4.Peek32(kRamCmd + ((rp - 16) & 0xFFF)), (2u << 16) | 4u);       // source 4 x 2
    EXPECT_EQ(vdac4.Peek32(kRamCmd + ((rp - 12) & 0xFFF)), (1u << 16) | 2u);       // output 2 x 1
    EXPECT_EQ(vdac4.Peek32(kRamCmd + ((rp - 8) & 0xFFF)), (2u << 16) | 3u);        // L8, stride 2
    EXPECT_EQ(vdac4.Peek32(kRamCmd + ((rp - 4) & 0xFFF)), 0u);                     // no palette
}

TEST(EveVdac4_Test, LoadImageTakesImagesBeyondTheFt812Limits)
{
    // 1400 x 400 = 560000 pixels: wider than 1024 and more pixels than the FT812 limits (JPEG 524288,
    // PNG 483328); the VDAC4 core loads any image with sides below 2048 (DOOM's 1024 x 768 loader JPEG)
    constexpr uint32_t kW = 1400, kH = 400;
    std::vector<uint8_t> pixels(kW * kH);
    for (uint32_t i = 0; i < pixels.size(); ++i)
        pixels[i] = static_cast<uint8_t>(i * 7 + i / kW);
    const std::vector<uint32_t> file = Words(GrayPng(kW, kH, pixels));
    std::vector<uint32_t> cmd = {0xFFFFFF24u, 0, 2}; // CMD_LOADIMAGE ptr 0, OPT_NODL
    cmd.insert(cmd.end(), file.begin(), file.end());

    Chip vdac4(EVE_MODEL_VDAC4);
    ASSERT_TRUE(vdac4.Stream(cmd)) << vdac4.View().faultReason;
    vdac4.Run(64000000);
    ASSERT_EQ(vdac4.Phase(), EVE_COPRO_IDLE) << vdac4.View().faultReason;
    for (uint32_t i : {0u, 1u, kW - 1, kW, kW * kH / 2 + 3, kW * kH - 1})
        EXPECT_EQ(EvePeek(vdac4.chip, i), pixels[i]) << "pixel " << i;

    Chip ft812(EVE_MODEL_FT812);
    ft812.BootFt812();
    ft812.Stream(cmd);
    ft812.Run(64000000);
    EXPECT_EQ(ft812.Phase(), EVE_COPRO_FAULT) << "FT812 keeps the card's limits";
}

TEST(EveVdac4_Test, LoadImageBoxWithAnEmptyBoxWritesAnImageReport)
{
    Chip vdac4(EVE_MODEL_VDAC4);
    vdac4.Commands({0xFFFFFFB0u, 0x1000, 0, 0, 0x10000});
    ASSERT_EQ(vdac4.Phase(), EVE_COPRO_FAULT);
    EXPECT_EQ(vdac4.Peek32(kRamCmd), 0x05474D49u); // 'I' 'M' 'G' + E_SIZE
    EXPECT_EQ(vdac4.Peek32(kRamCmd + 4), 0u);
    char text[16] = {};
    for (uint32_t i = 0; i < 15; ++i)
        text[i] = static_cast<char>(EvePeek(vdac4.chip, kRamCmd + 8 + i));
    EXPECT_STREQ(text, "too large: 0x0");
}

TEST(EveVdac4_Test, ThreeDDrawsATriangleIntoTheTarget)
{
    Chip vdac4(EVE_MODEL_VDAC4);
    constexpr uint32_t kTarget = 0x10000, kVerts = 0x8000, kFaces = 0x9000;
    // A triangle in front of the camera, z = 100 (int16 x, y, z, 0)
    const int16_t verts[3][4] = {{-50, -50, 100, 0}, {50, -50, 100, 0}, {0, 50, 100, 0}};
    vdac4.Write(kVerts, reinterpret_cast<const uint8_t*>(verts), sizeof(verts));
    const uint16_t faces[4] = {0, 2, 1, 0xF800}; // red, either winding (no culling)
    vdac4.Write(kFaces, reinterpret_cast<const uint8_t*>(faces), sizeof(faces));
    vdac4.Commands({0xFFFFFFA0u, kTarget, 7, (32u << 16) | 32u, // TARGET RGB565 32 x 32
                    0xFFFFFFA1u, 16u << 16, 1u << 16,          // PROJECT focal 16, znear 1
                    0xFFFFFFA9u, 0xFF0000FFu,                  // CLEAR blue
                    0xFFFFFFAAu, kVerts, kFaces, (1u << 16) | 3u, 0, // MESH, flat colour
                    0xFFFFFFABu,                               // RENDER
                    0xFFFFFFACu, 0, 0});                       // INFO
    ASSERT_EQ(vdac4.Phase(), EVE_COPRO_IDLE) << vdac4.View().faultReason;
    const auto pixel = [&](uint32_t x, uint32_t y) {
        return static_cast<uint32_t>(EvePeek(vdac4.chip, kTarget + 2 * (32 * y + x))) |
               (static_cast<uint32_t>(EvePeek(vdac4.chip, kTarget + 2 * (32 * y + x) + 1)) << 8);
    };
    EXPECT_EQ(pixel(0, 0), 0x001Fu);   // background
    EXPECT_EQ(pixel(16, 16), 0xF800u); // the triangle, centre of the target
    const uint32_t rp = vdac4.Peek32(kRegCmdRead);
    EXPECT_EQ(vdac4.Peek32(kRamCmd + ((rp - 8) & 0xFFF)), 0x3D030500u);
    EXPECT_EQ(vdac4.Peek32(kRamCmd + ((rp - 4) & 0xFFF)), 1u); // one triangle, 0 ms
}

TEST(EveVdac4_Test, ThreeDRotateIsDeterministic)
{
    // A quarter turn about z maps (50, 0) to (0, 50): the result is the same on every host
    Chip vdac4(EVE_MODEL_VDAC4);
    constexpr uint32_t kTarget = 0x10000, kVerts = 0x8000, kFaces = 0x9000;
    const int16_t verts[3][4] = {{0, 0, 100, 0}, {60, -10, 100, 0}, {60, 10, 100, 0}};
    vdac4.Write(kVerts, reinterpret_cast<const uint8_t*>(verts), sizeof(verts));
    const uint16_t faces[4] = {0, 1, 2, 0x07E0};
    vdac4.Write(kFaces, reinterpret_cast<const uint8_t*>(faces), sizeof(faces));
    vdac4.Commands({0xFFFFFFA0u, kTarget, 7, (64u << 16) | 64u, 0xFFFFFFA1u, 32u << 16, 1u << 16, 0xFFFFFFA9u, 0,
                    0xFFFFFFA4u, 0, 0, 0x4000, 0xFFFFFFAAu, kVerts, kFaces, (1u << 16) | 3u, 0, 0xFFFFFFABu});
    ASSERT_EQ(vdac4.Phase(), EVE_COPRO_IDLE) << vdac4.View().faultReason;
    const auto pixel = [&](uint32_t x, uint32_t y) {
        return static_cast<uint32_t>(EvePeek(vdac4.chip, kTarget + 2 * (64 * y + x))) |
               (static_cast<uint32_t>(EvePeek(vdac4.chip, kTarget + 2 * (64 * y + x) + 1)) << 8);
    };
    EXPECT_EQ(pixel(32, 32 - 15), 0x07E0u); // rotated up (screen y down)
    EXPECT_EQ(pixel(32 + 15, 32), 0u);      // nothing left where it was
}

TEST(EveVdac4_Test, ThreeDBadParametersFault)
{
    Chip vdac4(EVE_MODEL_VDAC4);
    vdac4.Commands({0xFFFFFFA0u, 0x10000, 7, (32u << 16) | 30u}); // width not a multiple of 4
    ASSERT_EQ(vdac4.Phase(), EVE_COPRO_FAULT);
    EXPECT_STREQ(vdac4.View().faultReason, "invalid 3D command parameters");
    EXPECT_EQ(vdac4.Read32(kRegCmdRead), 0xFFFu);
}

TEST(EveVdac4_Test, IntFlagsReadKeepsASwapThatLandsWithIt)
{
    for (const EveModel model : {EVE_MODEL_VDAC4, EVE_MODEL_FT812})
    {
        Chip chip(model);
        if (model == EVE_MODEL_FT812)
            chip.BootFt812();
        chip.Write32(kRegHcycle, 100);
        chip.Write32(kRegHoffset, 10);
        chip.Write32(kRegHsize, 64);
        chip.Write32(kRegVcycle, 40);
        chip.Write32(kRegVoffset, 4);
        chip.Write32(kRegVsize, 32);
        chip.Write32(kRegPclk, 2);
        chip.Write32(kRamDl, 0);
        chip.Read32(kRegIntFlags);
        chip.Write32(kRegDlswap, 2);
        uint64_t guard = 0;
        while (EvePeek(chip.chip, kRegDlswap) != 0 && guard++ < 100000)
            EveAdvance(chip.chip, 1);
        ASSERT_EQ(EvePeek(chip.chip, kRegDlswap), 0);
        // The read in the clock of the swap: the swap flag is reported and stays for the next read
        EXPECT_EQ(chip.Read32(kRegIntFlags) & kIntSwap, kIntSwap);
        if (model == EVE_MODEL_VDAC4)
        {
            EXPECT_EQ(chip.Read32(kRegIntFlags) & kIntSwap, kIntSwap);
            EveAdvance(chip.chip, 2);
            EXPECT_EQ(chip.Read32(kRegIntFlags) & kIntSwap, kIntSwap);
            EXPECT_EQ(chip.Read32(kRegIntFlags) & kIntSwap, 0u);
        }
        else
            EXPECT_EQ(chip.Read32(kRegIntFlags) & kIntSwap, 0u);
    }
}

// EveWriteRamGWords (the ESP's ft_write in one pass) stores what one write transaction per word stores, up to the
// first word outside RAM_G; the chip's state after it is that of the transactions
TEST(EveVdac4_Test, WriteRamGWordsMatchesOneTransactionPerWord)
{
    const std::vector<uint8_t> rom = MakeRom();
    Chip a(EVE_MODEL_VDAC4, &rom);
    Chip b(EVE_MODEL_VDAC4, &rom);
    ASSERT_NE(a.chip, nullptr);
    ASSERT_NE(b.chip, nullptr);

    // The last 6 words of the 2 MB RAM_G, then 2 words past it (the ROM there ignores the host)
    const uint32_t limit = EveRamGLimit(a.chip);
    const uint32_t address = limit - 6 * 4;
    std::vector<uint8_t> words(8 * 4);
    for (size_t i = 0; i < words.size(); ++i)
        words[i] = static_cast<uint8_t>(0x31 + 7 * i);

    for (uint32_t i = 0; i < 8; ++i)
        a.Write(address + 4 * i, words.data() + 4 * i, 4);

    ASSERT_EQ(EveWriteRamGWords(b.chip, address, words.data(), 8), 6u);
    for (uint32_t i = 6; i < 8; ++i)
        b.Write(address + 4 * i, words.data() + 4 * i, 4);

    for (uint32_t i = 0; i < 8 * 4; ++i)
        EXPECT_EQ(EvePeek(b.chip, address + i), EvePeek(a.chip, address + i)) << "byte " << i;
    ASSERT_EQ(EveStateSize(a.chip), EveStateSize(b.chip));
    std::vector<uint8_t> stateA(EveStateSize(a.chip)), stateB(EveStateSize(b.chip));
    EveSaveState(a.chip, stateA.data());
    EveSaveState(b.chip, stateB.data());
    EXPECT_EQ(stateA, stateB);

    // Not inside a transaction the host has open: the caller sends the words as transactions
    EveSelect(b.chip, 1);
    EXPECT_EQ(EveWriteRamGWords(b.chip, 0, words.data(), 1), 0u);
    EveSelect(b.chip, 0);
    // Not in the core reset's register sweep, which drops host bytes
    b.HostCommand(kHostRstPulse);
    EXPECT_EQ(EveWriteRamGWords(b.chip, 0, words.data(), 1), 0u);
}
