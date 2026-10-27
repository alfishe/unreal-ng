#include "stdafx.h"
#include "pch.h"

#include <cstring>
#include <type_traits>

#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/video/atm/atmfont.h"

/// The hardware-family state embedded in EmulatorState (atm, evo, scorpion, profi).
/// The emulator resets EmulatorState by assignment and TTD copies it field by field, so the
/// family structs must stay plain values: no owning pointers, zero after value-init.
/// Design: docs/inprogress/2026-10-07-model-state/tdd.md

static_assert(std::is_trivially_copyable_v<EmulatorState>);
static_assert(std::is_trivially_copyable_v<AtmState>);
static_assert(std::is_trivially_copyable_v<EvoState>);
static_assert(std::is_trivially_copyable_v<ScorpionState>);
static_assert(std::is_trivially_copyable_v<ProfiState>);

namespace
{
// Field by field: the padding of a value-initialized aggregate is unspecified

bool IsZero(const AtmState& s)
{
    if (s.aFE || s.aFB || s.aFF77 || s.memSwapped || s.borderBright || s.fontByte)
        return false;
    for (unsigned v : s.pFFF7)
        if (v) return false;
    for (int i = 0; i < 16; i++)
        if (s.palette[i] || s.paletteRegs[i]) return false;
    for (uint8_t v : s.fontRam)
        if (v) return false;
    return true;
}

bool IsZero(const EvoState& s)
{
    return !s.pBD && !s.pBE && !s.pBF && !s.fddMask && !s.trdemu && !s.vgSys && !s.wrProt && !s.turboPending &&
           !s.inNmi && !s.nmiEntry;
}

bool IsZero(const ScorpionState& s)
{
    return !s.turbo && !s.p7EFD && !s.profromBank && !s.dosTrigger && !s.pFFBA && !s.p7FBA;
}

bool IsZero(const ProfiState& s)
{
    if (s.turboSwitch || s.cpmSwitch)
        return false;
    for (uint16_t v : s.palette)
        if (v) return false;
    return true;
}

bool Equal(const AtmState& a, const AtmState& b)
{
    return a.aFE == b.aFE && a.aFB == b.aFB && a.aFF77 == b.aFF77 && a.memSwapped == b.memSwapped &&
           a.borderBright == b.borderBright && a.fontByte == b.fontByte &&
           std::memcmp(a.pFFF7, b.pFFF7, sizeof(a.pFFF7)) == 0 &&
           std::memcmp(a.palette, b.palette, sizeof(a.palette)) == 0 &&
           std::memcmp(a.paletteRegs, b.paletteRegs, sizeof(a.paletteRegs)) == 0 &&
           std::memcmp(a.fontRam, b.fontRam, sizeof(a.fontRam)) == 0;
}

bool Equal(const EvoState& a, const EvoState& b)
{
    return a.pBD == b.pBD && a.pBE == b.pBE && a.pBF == b.pBF && a.fddMask == b.fddMask && a.trdemu == b.trdemu &&
           a.vgSys == b.vgSys && a.wrProt == b.wrProt && a.turboPending == b.turboPending && a.inNmi == b.inNmi &&
           a.nmiEntry == b.nmiEntry;
}

bool Equal(const ScorpionState& a, const ScorpionState& b)
{
    return a.turbo == b.turbo && a.p7EFD == b.p7EFD && a.profromBank == b.profromBank &&
           a.dosTrigger == b.dosTrigger && a.pFFBA == b.pFFBA && a.p7FBA == b.p7FBA;
}

bool Equal(const ProfiState& a, const ProfiState& b)
{
    return a.turboSwitch == b.turboSwitch && a.cpmSwitch == b.cpmSwitch &&
           std::memcmp(a.palette, b.palette, sizeof(a.palette)) == 0;
}

void Scribble(EmulatorState& state)
{
    AtmState& atm = state.atm;
    atm.aFE = 0x21;
    atm.aFB = 0x22;
    atm.aFF77 = 0x4077;
    atm.memSwapped = true;
    atm.borderBright = 1;
    atm.fontByte = 0x5A;
    for (unsigned i = 0; i < 8; i++)
        atm.pFFF7[i] = 0x100 + i;
    for (unsigned i = 0; i < 16; i++)
    {
        atm.palette[i] = 0xFF000000u | i;
        atm.paletteRegs[i] = static_cast<uint8_t>(0x80 + i);
    }
    for (unsigned i = 0; i < sizeof(atm.fontRam); i++)
        atm.fontRam[i] = static_cast<uint8_t>(i * 7 + 1);

    EvoState& evo = state.evo;
    evo.pBD = 0x1234;
    evo.pBE = 0x31;
    evo.pBF = 0x32;
    evo.fddMask = 0x0F;
    evo.trdemu = 0x03;
    evo.vgSys = 0x3F;
    evo.wrProt = 0x81;
    evo.turboPending = 0x02;
    evo.inNmi = true;
    evo.nmiEntry = true;

    ScorpionState& scorpion = state.scorpion;
    scorpion.turbo = 1;
    scorpion.p7EFD = 0x08;
    scorpion.profromBank = 3;
    scorpion.dosTrigger = 1;
    scorpion.pFFBA = 0x41;
    scorpion.p7FBA = 0x42;

    ProfiState& profi = state.profi;
    profi.turboSwitch = 1;
    profi.cpmSwitch = 1;
    for (unsigned i = 0; i < 16; i++)
        profi.palette[i] = static_cast<uint16_t>(0x1F0 + i);
}
}  // namespace

TEST(EmulatorStateFamily_Test, ValueInitZeroesEveryFamily)
{
    const EmulatorState state{};

    EXPECT_TRUE(IsZero(state.atm));
    EXPECT_TRUE(IsZero(state.evo));
    EXPECT_TRUE(IsZero(state.scorpion));
    EXPECT_TRUE(IsZero(state.profi));
}

TEST(EmulatorStateFamily_Test, ResetByAssignmentClearsEveryFamily)
{
    // EmulatorContext resets with emulatorState = EmulatorState{}
    EmulatorState state{};
    Scribble(state);
    ASSERT_FALSE(IsZero(state.atm));
    ASSERT_FALSE(IsZero(state.evo));
    ASSERT_FALSE(IsZero(state.scorpion));
    ASSERT_FALSE(IsZero(state.profi));

    state = EmulatorState{};

    EXPECT_TRUE(IsZero(state.atm));
    EXPECT_TRUE(IsZero(state.evo));
    EXPECT_TRUE(IsZero(state.scorpion));
    EXPECT_TRUE(IsZero(state.profi));
}

TEST(EmulatorStateFamily_Test, CopyCarriesEveryFamily)
{
    EmulatorState source{};
    Scribble(source);

    EmulatorState copy{};
    copy = source;
    ASSERT_FALSE(IsZero(copy.profi));

    EXPECT_TRUE(Equal(copy.atm, source.atm));
    EXPECT_TRUE(Equal(copy.evo, source.evo));
    EXPECT_TRUE(Equal(copy.scorpion, source.scorpion));
    EXPECT_TRUE(Equal(copy.profi, source.profi));
}

TEST(EmulatorStateFamily_Test, ContextSeedsAtmPaletteAndFont)
{
    EmulatorContext context(LoggerLevel::LogError);
    const AtmState& atm = context.emulatorState.atm;

    // The standard 16 ZX colors, ABGR - what the ATM extended modes show before any #FF write
    static const uint32_t ZXPAL[16] = {
        0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4, 0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
        0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF, 0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF};
    for (int i = 0; i < 16; i++)
    {
        EXPECT_EQ(atm.palette[i], ZXPAL[i]) << "cell " << i;
        EXPECT_EQ(atm.paletteRegs[i], 0x00) << "cell " << i;
    }
    EXPECT_EQ(atm.borderBright, 0);

    // Font RAM is addressed code * 8 + row; the built-in font is stored row * 256 + code
    for (unsigned code = 0; code < 256; code++)
        for (unsigned row = 0; row < 8; row++)
            ASSERT_EQ(atm.fontRam[code * 8 + row], ATM_FONT[row * 256 + code]) << "code " << code << " row " << row;
    EXPECT_EQ(atm.fontByte, 0xFF);

    // The other families start at zero
    EXPECT_TRUE(IsZero(context.emulatorState.evo));
    EXPECT_TRUE(IsZero(context.emulatorState.scorpion));
    EXPECT_TRUE(IsZero(context.emulatorState.profi));
}
