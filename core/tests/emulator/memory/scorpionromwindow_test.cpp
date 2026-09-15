#include "stdafx.h"
#include "pch.h"

#include "emulator/memory/scorpion/scorpionromwindow.h"
#include "emulator/ports/models/scorpionfixture.h"

/// @brief ProfROM quadrant window - pure policy tests (design §4.2, §4.3).
///
/// The window object holds no quadrant state of its own: EmulatorState carries
/// profrom_bank / p7EFD and TEMP carries the image-size masks. These tests
/// therefore work on plain structs - no machine, no ROM file.
class ScorpionRomWindow_Test : public ::testing::Test
{
protected:
    ScorpionRomWindow _window;
    EmulatorState _state {};
    TEMP _temp {};

    /// @brief Expected hardware transition (hardware-reference §5.2):
    ///        rows = selector S = A3:A2 of the read (#0100 + 4*S),
    ///        columns = current GAL 2-bit state
    static constexpr uint8_t ExpectedSwitch[4][4] =
            {
                    {0, 1, 2, 3},   // S = 0: #0100-#0103 - hold
                    {3, 3, 3, 2},   // S = 1: #0104-#0107
                    {2, 2, 0, 1},   // S = 2: #0108-#010B
                    {1, 0, 1, 0}    // S = 3: #010C-#010F
            };
};

/// Full 4x4 transition table against a 256 KB image (both GAL bits live);
/// A0/A1 inside the block are ignored by the GAL (unwired) - all 16 addresses
/// of #0100-#010F drive the row their A3:A2 selects
TEST_F(ScorpionRomWindow_Test, TransitionTableFollowsVerifiedTable)
{
    _window.Configure(_temp, 16);  // 4 quadrants
    EXPECT_EQ(_temp.profrom_mask, 3);

    for (uint8_t from = 0; from < 4; from++)
    {
        for (uint16_t addr = 0x0100; addr < 0x0110; addr++)
        {
            _state.profrom_bank = from;
            bool changed = _window.OnRomRead(_state, _temp, addr);

            uint8_t expected = ExpectedSwitch[(addr >> 2) & 0b11][from];
            EXPECT_EQ(_window.Quadrant(_state), expected)
                << "from Q" << static_cast<int>(from) << " read #" << std::hex << addr;
            EXPECT_EQ(changed, expected != from);
        }
    }
}

/// S=0 hold: reads of #0100-#0103 never move the machine from any quadrant -
/// this is what lets the monitor read its plane ID from #0101 safely
TEST_F(ScorpionRomWindow_Test, HoldRowNeverSwitches)
{
    _window.Configure(_temp, 16);

    for (uint8_t from = 0; from < 4; from++)
    {
        _state.profrom_bank = from;
        for (int i = 0; i < 100; i++)
            for (uint16_t addr = 0x0100; addr < 0x0104; addr++)
                EXPECT_FALSE(_window.OnRomRead(_state, _temp, addr));

        EXPECT_EQ(_window.Quadrant(_state), from);
    }
}

/// 128 KB image: the GAL carries a single bit - the S=2 target Q2 wraps to Q0
TEST_F(ScorpionRomWindow_Test, Mask128KImageWrapsQuadrants)
{
    _window.Configure(_temp, 8);  // 2 quadrants
    EXPECT_EQ(_temp.profrom_mask, 1);

    _state.profrom_bank = 0;
    _window.OnRomRead(_state, _temp, 0x0108);  // table says 2, single bit keeps 0
    EXPECT_EQ(_window.Quadrant(_state), 0);

    _window.OnRomRead(_state, _temp, 0x0104);  // table says 3, single bit keeps 1
    EXPECT_EQ(_window.Quadrant(_state), 1);
}

/// 64 KB image: no state-machine bits at all, every strobe is a no-op
TEST_F(ScorpionRomWindow_Test, Mask64KImageNeverSwitches)
{
    _window.Configure(_temp, 4);
    EXPECT_EQ(_temp.profrom_mask, 0);

    for (uint16_t addr = 0x0100; addr < 0x0110; addr++)
    {
        _state.profrom_bank = 0;
        EXPECT_FALSE(_window.OnRomRead(_state, _temp, addr));
    }
}

/// 512 KB image: #7EFD bit 4 selects quadrants 4-7 while the GAL state persists
TEST_F(ScorpionRomWindow_Test, WindowSelect512KImage)
{
    _window.Configure(_temp, 32);  // 8 quadrants
    EXPECT_EQ(_temp.profrom_window_mask, 1);

    _window.OnRomRead(_state, _temp, 0x0104);      // S=1: Q0 -> Q3
    ASSERT_EQ(_window.Quadrant(_state), 3);

    _window.OnWindowPortWrite(_state, _temp, 0x10);  // window 1, GAL keeps 3
    EXPECT_EQ(_window.Quadrant(_state), 7);
    EXPECT_EQ(_state.p7EFD, 0x10);

    _window.OnWindowPortWrite(_state, _temp, 0x00);  // window 0 again
    EXPECT_EQ(_window.Quadrant(_state), 3);
}

/// 1 MB image: both #7EFD select bits live; 2 MB adds the emulator extension bit
TEST_F(ScorpionRomWindow_Test, WindowSelect1MAnd2MExtension)
{
    _window.Configure(_temp, 64);  // 16 quadrants
    EXPECT_EQ(_temp.profrom_window_mask, 3);

    _window.OnWindowPortWrite(_state, _temp, 0x20);  // window 2
    EXPECT_EQ(_window.Quadrant(_state), 8);

    _window.Configure(_temp, 128);  // 32 quadrants (2 MB)
    _state.profrom_bank = 0;
    _window.OnWindowPortWrite(_state, _temp, 0x40);  // extension bit only
    EXPECT_EQ(_window.Quadrant(_state), 16);

    _window.OnWindowPortWrite(_state, _temp, 0x50);  // window 1 + extension
    EXPECT_EQ(_window.Quadrant(_state), 20);
}

/// #7EFD writes stay inert below 512 KB images (window mask 0), latch verbatim
TEST_F(ScorpionRomWindow_Test, WindowInertBelow512KImage)
{
    _window.Configure(_temp, 16);

    _window.OnWindowPortWrite(_state, _temp, 0x30);
    EXPECT_EQ(_window.Quadrant(_state), 0);
    EXPECT_EQ(_state.p7EFD, 0x30);
}

/// The window must never name a quadrant the image does not carry, and Reset()
/// returns to the power-on quadrant with a cleared latch
TEST_F(ScorpionRomWindow_Test, StateResidencyBoundsAndReset)
{
    _window.Configure(_temp, 128);  // 2 MB ladder top

    // A walk mixing strobes, window selects and the extension bit
    for (int step = 0; step < 200; step++)
    {
        switch (step % 3)
        {
            case 0:
                _window.OnRomRead(_state, _temp, static_cast<uint16_t>(0x0100 | (step & 0x0F)));
                break;
            case 1:
                _window.OnWindowPortWrite(_state, _temp, static_cast<uint8_t>(step));
                break;
            default:
                _window.OnRomRead(_state, _temp, static_cast<uint16_t>(0x0100 | ((step << 2) & 0x0C)));
                break;
        }

        EXPECT_EQ(_window.Quadrant(_state), _state.profrom_bank);
        EXPECT_LE(_window.Quadrant(_state), 31);
    }

    _window.Reset(_state);
    EXPECT_EQ(_window.Quadrant(_state), 0);
    EXPECT_EQ(_state.p7EFD, 0);
}

/// @brief ProfROM quadrant machine driven through the real Memory read path.
///
/// Uses the synthetic patterned ROM (every page self-identifying), so quadrant
/// identity reduces to reading the service-ROM tag through the #0000 window.
class ScorpionRomWindowMachine_Test : public ScorpionMachineFixture
{
protected:
    /// Tag of the Shadow Monitor page currently mapped at #0000:
    /// service page of quadrant q is page q*4 + 2 (hardware-reference §5.1)
    static constexpr uint8_t ServiceTag(uint8_t quadrant)
    {
        return static_cast<uint8_t>(ScorpionRomTagBase | (quadrant * 4 + 2));
    }

    /// CPU-path read (drives the read strobe), unlike BankTag()/DirectRead
    uint8_t FastRead(uint16_t addr)
    {
        return _memory->MemoryReadFast(addr, false);
    }

    void SetUpProf(uint16_t quadrants)
    {
        ASSERT_TRUE(RebuildWithModel(MM_PROFSCORP, RAM_256));
        ASSERT_TRUE(LoadSyntheticRom(quadrants));

        // Page the Shadow Monitor at #0000 (#1FFD bit 1) - the read strobe is
        // only armed while the service ROM window is visible (design §4.3)
        WritePort(0x1FFD, 0x02);

        // Power-on: quadrant 0, service page visible at #0000
        EXPECT_EQ(_context->emulatorState.profrom_bank, 0);
        ASSERT_EQ(BankTag(0x0000), ServiceTag(0));
    }
};

/// Reads outside the #0100-#010F block never clock the GAL: the block gate
/// lives in the ScorpionMemory read-path override (scorpionmemory.cpp), so
/// only FastRead proves it
TEST_F(ScorpionRomWindowMachine_Test, OutsideBlockDoesNotAdvance)
{
    SetUpProf(4);
    FastRead(0x0104);              // S=1: Q0 -> Q3
    ASSERT_EQ(_context->emulatorState.profrom_bank, 3);

    // #0000-#00FF and #0110-#01FF must stay inert, including the reset fetch
    for (uint16_t addr = 0; addr < 0x0100; addr += 7)
        FastRead(addr);
    for (uint16_t addr = 0x0110; addr < 0x0200; addr += 7)
        FastRead(addr);

    EXPECT_EQ(_context->emulatorState.profrom_bank, 3);
    EXPECT_EQ(BankTag(0x0000), ServiceTag(3));
}

/// The verified graph walk Q0 -> Q3 -> Q1 -> Q1 -> Q2 -> Q0 remaps the window
TEST_F(ScorpionRomWindowMachine_Test, StrobeWalkRemapsServiceRom)
{
    SetUpProf(4);  // 256 KB synthetic image

    FastRead(0x0104);              // S=1: Q0 -> Q3
    EXPECT_EQ(BankTag(0x0000), ServiceTag(3));

    FastRead(0x0108);              // S=2: Q3 -> Q1
    EXPECT_EQ(BankTag(0x0000), ServiceTag(1));

    FastRead(0x0100);              // S=0: from Q1 stays Q1
    EXPECT_EQ(BankTag(0x0000), ServiceTag(1));

    FastRead(0x0108);              // S=2: Q1 -> Q2
    EXPECT_EQ(BankTag(0x0000), ServiceTag(2));

    FastRead(0x0108);              // S=2: Q2 -> Q0
    EXPECT_EQ(BankTag(0x0000), ServiceTag(0));
    EXPECT_EQ(_memory->GetScorpionRomWindow()->Quadrant(_context->emulatorState), 0);
}

/// The strobed read itself returns the post-switch quadrant's byte
/// (mid-instruction remap, hardware-reference §12.4)
TEST_F(ScorpionRomWindowMachine_Test, ReadReturnsPostSwitchBytes)
{
    SetUpProf(4);

    FastRead(0x0104);              // S=1: Q0 -> Q3
    uint8_t byte = FastRead(0x010C);  // S=3: from Q3 -> Q0

    EXPECT_EQ(byte, ServiceTag(0));        // byte comes from Q0
    EXPECT_EQ(BankTag(0x0000), ServiceTag(0));  // and so does the window
}

/// Regression (profrom-nmi-gaps-and-findings GAP-2): the strobe grid is the
/// four A1:A0 = 0 addresses only, and only a data read clocks the GAL. The
/// monitor data-reads the whole #0100-#010F block during inter-plane
/// bookkeeping - the RST 30h dispatcher reads the plane signature at #0101
/// (LD HL,(#0101)) on every call, the reset stubs read the park selector at
/// #0108/#010C through LD L,(HL) - so an off-grid read or an opcode fetch
/// must never switch the plane
TEST_F(ScorpionRomWindowMachine_Test, OffGridAndFetchReadsDoNotStrobe)
{
    SetUpProf(4);

    for (uint16_t addr = 0x0101; addr < 0x0110; addr++)
    {
        if ((addr & 0x0003) == 0)
            continue;  // #0104/#0108/#010C sit on the grid
        FastRead(addr);  // data read one byte off the grid
        ASSERT_EQ(_context->emulatorState.profrom_bank, 0) << "off-grid read #" << addr;
    }

    // An opcode fetch never clocks the GAL even on the grid: /M1 masks the
    // strobe (Xpeccy gates on !m1)
    _memory->MemoryReadFast(0x0104, true);
    EXPECT_EQ(_context->emulatorState.profrom_bank, 0) << "instruction fetches never strobe";

    // The grid itself still clocks on a data read: S=1 moves Q0 -> Q3
    FastRead(0x0104);
    EXPECT_EQ(BankTag(0x0000), ServiceTag(3));
}

/// Debugger-side reads never advance the quadrant machine - the plane ID
/// byte at #0101 read through the debugger path must stay inert
TEST_F(ScorpionRomWindowMachine_Test, DebuggerReadsDoNotStrobe)
{
    SetUpProf(4);

    for (int i = 0; i < 100; i++)
        BankTag(0x0101);  // DirectReadFromZ80Memory path

    EXPECT_EQ(_context->emulatorState.profrom_bank, 0);
    EXPECT_EQ(BankTag(0x0000), ServiceTag(0));
}

/// The base Scorpion never switches even with a multi-quadrant image loaded
TEST_F(ScorpionRomWindowMachine_Test, BaseModelNeverSwitches)
{
    ASSERT_TRUE(LoadSyntheticRom(1));  // MM_SCORP demands exactly 64 KB (1 quadrant)
    WritePort(0x1FFD, 0x02);          // service window paged, like the ProfROM tests

    for (int i = 0; i < 50; i++)
        for (uint16_t addr = 0x0100; addr < 0x0110; addr++)
            FastRead(addr);

    EXPECT_EQ(_context->emulatorState.profrom_bank, 0);
    EXPECT_EQ(BankTag(0x0000), ServiceTag(0));
}

/// #7EFD window select on a 512 KB image: quadrant 4 without any strobe
TEST_F(ScorpionRomWindowMachine_Test, WindowSelectRemapsRomDiskQuadrant)
{
    SetUpProf(8);  // 512 KB synthetic image

    WritePort(0x7EFD, 0x10);       // window bit 4
    EXPECT_EQ(_context->emulatorState.p7EFD, 0x10);
    EXPECT_EQ(BankTag(0x0000), ServiceTag(4));

    // The GAL state machine keeps working inside the selected window
    FastRead(0x0104);              // S=1: gal 0 -> 3, window 1 -> Q7
    EXPECT_EQ(BankTag(0x0000), ServiceTag(7));

    WritePort(0x7EFD, 0x00);       // window back to quadrants 0-3
    EXPECT_EQ(BankTag(0x0000), ServiceTag(3));
}
