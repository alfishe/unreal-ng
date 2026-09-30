// ZX-Evo BaseConf wait states at 14 MHz (evoturbooverlay.h): the DRAM's code and data cache words, the
// 2 + (t & 1) miss rule and the external ports' 3 clocks. The expected durations are the worked examples of
// docs/inprogress/2026-09-29-machine-waits/research-zxevo.md A.7, simulated on the BaseConf RTL. All clocks
// here are 14 MHz T-states (Z80::t counts the current clock).

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/ttd/atm/ttdevoturbocache.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_atm3.h"

namespace
{
class EvoTurboOverlay_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Core* _core = nullptr;
    Z80* _z80 = nullptr;
    Memory* _memory = nullptr;
    PortDecoder_ATM3* _decoder = nullptr;

    // Past the INT pulse at any clock rate, far from the frame's end
    static constexpr uint32_t kEvenStart = 40000;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _core = _context->pCore;
        _z80 = _core->GetZ80();
        _memory = _context->pMemory;
        _decoder = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);

        // TR-DOS boot map: ROM in window 0, RAM in windows 1-3, the shadow ports open
        _decoder->ApplyBootROMDefaults(RM_DOS);
        _context->emulatorState.pBF = 0x01;
        _z80->iff1 = 0;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// The clock select through #xx77 (bit 3: 14 MHz) or, with bit 3 clear and #EFF7 bit 4 clear, 7 MHz; the
    /// CPU takes the new rate at once (unreal-ng applies the ATM3's select at the next frame otherwise)
    void SelectClock(bool mhz14)
    {
        _decoder->DecodePortOut(0x0177, static_cast<uint8_t>(0x03 | (mhz14 ? 0x08 : 0x00)), 0x0000);
        _z80->ApplyHardwareTurboNow();
    }

    void Poke(uint16_t addr, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _z80->DirectWrite(addr++, b);
    }

    /// Clocks the instruction at `pc` takes, from clock `start`
    uint32_t Step(uint16_t pc, uint32_t start)
    {
        _z80->pc = pc;
        _z80->t = start;
        _z80->Z80Step();
        return _z80->t - start;
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

TEST_F(EvoTurboOverlay_Test, TheMapIsRomThenRam)
{
    SelectClock(true);
    EXPECT_TRUE(_decoder->AreTurboWaitsInstalled());
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio_applied, 4);
    EXPECT_TRUE(_memory->IsWindowRom(0));
    EXPECT_FALSE(_memory->IsWindowRom(1));
    EXPECT_FALSE(_memory->IsWindowRom(2));
    EXPECT_FALSE(_memory->IsWindowRom(3));
}

/// A.7: the even-address NOP misses, the odd one hits the code word; an M1 miss always ends on an even clock
TEST_F(EvoTurboOverlay_Test, NopStreamInRamTakesSixThenFour)
{
    SelectClock(true);
    Poke(0x8000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

    EXPECT_EQ(Run(0x8000, kEvenStart, 6), (std::vector<uint32_t>{6, 4, 6, 4, 6, 4}));
    // From an odd clock the first miss waits 3 and ends even: the stream settles to 6, 4
    EXPECT_EQ(Run(0x8000, kEvenStart + 1, 4), (std::vector<uint32_t>{7, 4, 6, 4}));
}

/// A.7: `JR $` fetches its opcode and operand from one word: no wait after the first fetch
TEST_F(EvoTurboOverlay_Test, JumpToItselfWaitsOnlyOnce)
{
    SelectClock(true);
    Poke(0x8000, {0x18, 0xFE});

    EXPECT_EQ(Run(0x8000, kEvenStart, 3), (std::vector<uint32_t>{12 + 2, 12, 12}));
}

/// A.7: PC and HL walk so that every data read misses: 12 and 10 alternate after the first
TEST_F(EvoTurboOverlay_Test, LoadFromWalkingAddressesTakesTwelveThenTen)
{
    SelectClock(true);
    Poke(0x8000, {0x7E, 0x7E, 0x7E, 0x7E, 0x7E});

    std::vector<uint32_t> clocks;
    _z80->pc = 0x8000;
    _z80->t = kEvenStart;
    for (uint16_t i = 0; i < 5; i++)
    {
        _z80->hl = static_cast<uint16_t>(0xC000 + 2 * i);  // a new word each time
        const uint32_t before = _z80->t;
        _z80->Z80Step();
        clocks.push_back(_z80->t - before);
    }
    // The first from an even clock: M1 miss 4 + 2, read miss at an even clock 3 + 2
    EXPECT_EQ(clocks, (std::vector<uint32_t>{11, 10, 12, 10, 12}));

    // A read from the word the previous read loaded adds nothing
    _z80->hl = 0xC009;  // the word of #C008, the last read
    EXPECT_EQ(Step(0x8004, kEvenStart + 1), 4u + 3u) << "the last PC: code hit; data hit";
}

TEST_F(EvoTurboOverlay_Test, RomReadsDoNotWaitAndDropBothWords)
{
    SelectClock(true);
    Poke(0x8000, {0x00, 0x7E});

    EXPECT_EQ(Step(0x8000, kEvenStart), 6u) << "NOP: code miss, loads the word of #8000";
    _z80->hl = 0x0010;
    EXPECT_EQ(Step(0x8001, kEvenStart), 7u) << "LD A,(HL): code hit, a ROM read never waits";
    EXPECT_EQ(Step(0x8000, kEvenStart), 6u) << "the ROM read dropped the code word: a miss again";
}

TEST_F(EvoTurboOverlay_Test, WriteDropsTheMatchingWordWithoutWaiting)
{
    SelectClock(true);
    Poke(0x8000, {0x00, 0x77, 0x00});

    EXPECT_EQ(Step(0x8000, kEvenStart), 6u);
    _z80->hl = 0xA000;
    EXPECT_EQ(Step(0x8001, kEvenStart), 7u) << "LD (HL),A elsewhere: code hit, the write never waits";
    EXPECT_EQ(Step(0x8000, kEvenStart), 4u) << "the code word survives a write to another word";

    _z80->hl = 0x8000;
    _z80->a = 0x00;  // writes the NOP back
    EXPECT_EQ(Step(0x8001, kEvenStart), 7u) << "LD (HL),A into the code word";
    EXPECT_EQ(Step(0x8000, kEvenStart), 6u) << "the written word is dropped: a miss";
}

/// A.7: `OUT (#FE),A` adds nothing but drops both words, so the next fetch misses
TEST_F(EvoTurboOverlay_Test, IoDropsBothWords)
{
    SelectClock(true);
    Poke(0x8000, {0xD3, 0xFE});

    EXPECT_EQ(Step(0x8000, kEvenStart), 11u + 2u) << "M1 miss; the operand hits the code word; internal port";
    EXPECT_EQ(Step(0x8000, kEvenStart), 11u + 2u) << "the OUT dropped the code word";
}

/// A.4, A.7: an external port's I/O cycle is 7 clocks instead of 4; ED 79 from an odd clock is 18
TEST_F(EvoTurboOverlay_Test, ExternalPortTakesThreeMore)
{
    SelectClock(true);
    Poke(0x8000, {0xED, 0x79});

    _z80->bc = 0xFFFD;
    EXPECT_EQ(Step(0x8000, kEvenStart + 1), 18u) << "OUT (C),A to the AY: (4 + 3) + 4 + (4 + 3)";

    _z80->bc = 0x00FE;
    EXPECT_EQ(Step(0x8000, kEvenStart + 1), 12u + 3u) << "to #FE: only the code miss";

    // The VG93 ports are external only in shadow mode (#1F there is the joystick otherwise)
    _z80->bc = 0x001F;
    EXPECT_EQ(Step(0x8000, kEvenStart + 1), 12u + 3u + 3u) << "#1F in shadow: the VG93";
}

TEST_F(EvoTurboOverlay_Test, InterruptAcknowledgeDropsBothWords)
{
    SelectClock(true);
    Poke(0x8000, {0x00});
    Poke(0x0038, {0x00});

    EXPECT_EQ(Step(0x8000, kEvenStart), 6u);
    EXPECT_EQ(Step(0x8000, kEvenStart), 4u) << "code hit";

    _z80->iff1 = _z80->iff2 = 1;
    _z80->im = 1;
    _z80->pc = 0x8000;
    _z80->sp = 0xBFF0;
    _z80->HandleINT();
    _z80->iff1 = 0;
    EXPECT_EQ(Step(0x8000, kEvenStart), 6u) << "the acknowledge (an I/O cycle) dropped the code word";
}

TEST_F(EvoTurboOverlay_Test, NoWaitsBelowFourteenMegahertzOrWithTheFeatureOff)
{
    Poke(0x8000, {0x00, 0x00, 0xED, 0x79});
    _z80->bc = 0xFFFD;

    SelectClock(false);
    EXPECT_FALSE(_decoder->AreTurboWaitsInstalled()) << "7 MHz: no overlay";
    EXPECT_EQ(Run(0x8000, kEvenStart, 3), (std::vector<uint32_t>{4, 4, 12}));

    SelectClock(true);
    _core->SetContentionSwitch(false);
    EXPECT_EQ(Run(0x8000, kEvenStart, 3), (std::vector<uint32_t>{4, 4, 12})) << "the contention feature off";
    _core->SetContentionSwitch(true);
    EXPECT_EQ(Run(0x8000, kEvenStart, 3), (std::vector<uint32_t>{6, 4, 6 + 4 + 7}));

    SelectClock(false);
    EXPECT_FALSE(_decoder->AreTurboWaitsInstalled()) << "back to 7 MHz: removed";
    EXPECT_EQ(_core->GetBusOverlayCount(), 0u);
}

/// The cache words are timing state: a seek restores them, and installs the overlay for the restored clock
TEST_F(EvoTurboOverlay_Test, TtdBlobRestoresTheCacheWords)
{
    const auto ids = _decoder->GetTTDModelStateIds();
    EXPECT_NE(std::find(ids.begin(), ids.end(), ttd::PeripheralId::EvoTurboCache), ids.end());

    SelectClock(true);
    Poke(0x8000, {0x00, 0x00});
    ttd::TTDEvoTurboCache serializer(*_decoder);
    ASSERT_EQ(serializer.TTDStateSize(), 6u);

    Step(0x8000, kEvenStart);  // the code word of #8000
    std::vector<uint8_t> blob(serializer.TTDStateSize());
    serializer.TTDSaveState(blob.data());
    const uint64_t hash = serializer.TTDHashState();

    _decoder->DecodePortOut(0x00FE, 0x00, 0x0000);  // an I/O cycle drops it
    EXPECT_NE(serializer.TTDHashState(), hash);
    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(serializer.TTDHashState(), hash);
    EXPECT_EQ(Step(0x8000, kEvenStart), 4u) << "the restored code word hits";

    // A checkpoint taken at 7 MHz restored while at 14 MHz: the chipset copy sets the clock, the blob syncs
    SelectClock(false);
    serializer.TTDSaveState(blob.data());
    SelectClock(true);
    _context->emulatorState.hw_turbo_ratio = 2;
    serializer.TTDLoadState(blob.data());
    EXPECT_FALSE(_decoder->AreTurboWaitsInstalled());
}
