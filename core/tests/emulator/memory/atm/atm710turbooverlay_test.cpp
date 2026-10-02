// ATM Turbo 2+ v7.10 RAM waits at 7 MHz (atm710turbooverlay.h). The expected durations are the worked
// examples of docs/inprogress/2026-10-02-atm710-turbo-waits/reference-atm710-turbo-waits.md section 6,
// from the v7.10 schematic and the assembly manual's turbo timing diagram. All clocks here are 7 MHz
// T-states (Z80::t counts the current clock); RAM slots start on even clocks of the frame.

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/ttd/atm/ttdatmpaging.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "emulator/state/devicestate.h"
#include "emulator/ports/models/portdecoder_atm710.h"

namespace
{
class Atm710TurboOverlay_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_ATM710* _decoder = nullptr;

    // Any clock of the frame: the rule depends on its parity only
    static constexpr uint32_t kEven = 20000;
    static constexpr uint32_t kOdd = 20001;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM710", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _core = _context->pCore;
        _z80 = _core->GetZ80();
        _decoder = dynamic_cast<PortDecoder_ATM710*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _z80->iff1 = 0;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// #FF77 bit 3 with every other bit kept; the clock is taken over at once (the board: next frame)
    void Turbo(bool on)
    {
        const EmulatorState& state = _context->emulatorState;
        const uint8_t value = on ? static_cast<uint8_t>(state.pFF77 | 0x08) : static_cast<uint8_t>(state.pFF77 & ~0x08);
        _decoder->DecodePortOut(state.aFF77 ? state.aFF77 : 0xFF77, value, 0x0000);
        _z80->ApplyHardwareTurboNow();
    }

    void Poke(uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _z80->DirectWrite(addr++, b);
    }

    /// Clocks of `count` instructions from `pc`, one after another from clock `start`
    std::vector<uint32_t> Run(uint16_t pc, uint32_t start, size_t count)
    {
        std::vector<uint32_t> clocks;
        _z80->pc = pc;
        _z80->t = start;
        for (size_t i = 0; i < count; i++)
        {
            const uint32_t before = _z80->t;
            _z80->Z80Step();
            clocks.push_back(_z80->t - before);
        }
        return clocks;
    }
};
}  // namespace

TEST_F(Atm710TurboOverlay_Test, TurboInstallsTheOverlay)
{
    auto report = [this]() {
        const StateNode node = DeviceState::Contention(_context);
        const StateNode* field = node.find("atm710_turbo_waits");
        return field ? field->s : std::string("(missing)");
    };
    Turbo(false);
    EXPECT_FALSE(_decoder->AreTurboRamWaitsInstalled());
    EXPECT_EQ(report(), "off");
    Turbo(true);
    EXPECT_TRUE(_decoder->AreTurboRamWaitsInstalled());
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio_applied, 2);
    EXPECT_EQ(report(), "active") << "the contention state report names the rule";
    _core->SetContentionSwitch(false);
    EXPECT_EQ(report(), "contention_off");
    _core->SetContentionSwitch(true);
    Turbo(false);
    EXPECT_FALSE(_decoder->AreTurboRamWaitsInstalled());
}

/// A RAM M1 waits 2 from an even clock and 3 from an odd one, and then ends on an even clock
TEST_F(Atm710TurboOverlay_Test, RamNopTakesSixOrSeven)
{
    Turbo(true);
    Poke(0x8000, {0x00, 0x00, 0x00, 0x00});

    EXPECT_EQ(Run(0x8000, kEven, 4), (std::vector<uint32_t>{6, 6, 6, 6}));
    EXPECT_EQ(Run(0x8000, kOdd, 4), (std::vector<uint32_t>{7, 6, 6, 6})) << "the first M1 aligns to the slot";
}

/// Reads and writes wait like fetches: M1 6 or 7, then the access 5
TEST_F(Atm710TurboOverlay_Test, RamReadsAndWritesWait)
{
    Turbo(true);
    Poke(0x8000, {0x7E});   // LD A,(HL)
    Poke(0x9000, {0x77});   // LD (HL),A
    _z80->hl = 0xC000;

    EXPECT_EQ(Run(0x8000, kEven, 1), (std::vector<uint32_t>{11})) << "LD A,(HL) from even";
    EXPECT_EQ(Run(0x8000, kOdd, 1), (std::vector<uint32_t>{12})) << "LD A,(HL) from odd";
    EXPECT_EQ(Run(0x9000, kEven, 1), (std::vector<uint32_t>{11})) << "LD (HL),A from even";
}

TEST_F(Atm710TurboOverlay_Test, RomAndIoDoNotWait)
{
    Turbo(true);
    // Window 0 = ROM page 0 through its #xFF7 register (type 0x300, "ROM from FFF7"), as software selects it
    EmulatorState& state = _context->emulatorState;
    state.pFFF7[((state.p7FFD & 0x10) ? 4 : 0) + 0] = 0x300;
    _context->pMemory->UpdateZ80Banks();
    ASSERT_TRUE(_context->pMemory->IsWindowRom(0));
    Poke(0x8000, {0x7E, 0xD3, 0xFE});   // LD A,(HL); OUT (#FE),A
    _z80->hl = 0x0100;

    EXPECT_EQ(Run(0x8000, kEven, 1), (std::vector<uint32_t>{6 + 3})) << "M1 from RAM 6, the ROM read 3";
    EXPECT_EQ(Run(0x8001, kEven, 1), (std::vector<uint32_t>{6 + 5 + 4})) << "M1 6, operand 5, the I/O cycle 4";
}

TEST_F(Atm710TurboOverlay_Test, NoWaitsAtThreeAndAHalfMegahertz)
{
    Turbo(false);
    Poke(0x8000, {0x00, 0x7E});
    _z80->hl = 0xC000;
    EXPECT_EQ(Run(0x8000, kOdd, 2), (std::vector<uint32_t>{4, 7}));
}

TEST_F(Atm710TurboOverlay_Test, TheContentionFeatureOffRemovesTheWaits)
{
    Turbo(true);
    _core->SetContentionSwitch(false);
    Poke(0x8000, {0x00, 0x7E});
    _z80->hl = 0xC000;
    EXPECT_EQ(Run(0x8000, kOdd, 2), (std::vector<uint32_t>{4, 7}));
    _core->SetContentionSwitch(true);
}

/// A TTD restore brings #FF77 back with the core state, past the decoder: the paging blob's load syncs the overlay
TEST_F(Atm710TurboOverlay_Test, ATtdRestoreSyncsTheOverlay)
{
    Turbo(false);
    ttd::TTDAtmPaging paging(_context);
    std::vector<uint8_t> blob(paging.TTDStateSize());
    paging.TTDSaveState(blob.data());

    _context->emulatorState.pFF77 |= 0x08;   // what the checkpoint's core state put back
    paging.TTDLoadState(blob.data());
    EXPECT_TRUE(_decoder->AreTurboRamWaitsInstalled());

    _context->emulatorState.pFF77 &= static_cast<uint8_t>(~0x08);
    paging.TTDLoadState(blob.data());
    EXPECT_FALSE(_decoder->AreTurboRamWaitsInstalled());
}

/// The ZX-Evo derives from the ATM710 decoder: its turbo waits are its FPGA's, not the v7.10 board's
TEST(Atm710TurboOverlayZxEvo_Test, TheZxEvoDoesNotGetThem)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    auto* decoder = dynamic_cast<PortDecoder_ATM710*>(emulator->GetContext()->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    decoder->SyncTurboRamWaits();
    EXPECT_FALSE(decoder->AreTurboRamWaitsInstalled());
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// v7.10 at 7 MHz: the WD1793's select (/VGCS) adds one wait state through R1 / C9 (reference Q5)
TEST_F(Atm710TurboOverlay_Test, FdcPortsWaitOneInTurbo)
{
    _context->emulatorState.flags |= CF_DOSPORTS;   // the shadow ports open, as inside TR-DOS
    auto waitOf = [this](uint16_t port) {
        const uint32_t before = _z80->t;
        _z80->in(port);
        return _z80->t - before;
    };
    Turbo(true);
    EXPECT_EQ(waitOf(0x001F), 1u) << "#1F: the VG93 status";
    EXPECT_EQ(waitOf(0x007F), 1u) << "#7F: the VG93 data";
    EXPECT_EQ(waitOf(0x00FF), 0u) << "#FF: the system register, not /VGCS";
    _core->SetContentionSwitch(false);
    EXPECT_EQ(waitOf(0x001F), 0u);
    _core->SetContentionSwitch(true);
    Turbo(false);
    EXPECT_EQ(waitOf(0x001F), 0u) << "3.5 MHz: no wait";
}
