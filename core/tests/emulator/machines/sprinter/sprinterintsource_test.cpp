// SprinterIntSource: the INT positions from the mode table and the pulse
// (Sprinter tdd-video §5, test-plan T-VID-8 / T-VID-9; MAME update_int).

#include "sprinterfixture.h"

#include "emulator/video/sprinter/sprinterintsource.h"
#include "emulator/video/sprinter/sprintervideoram.h"

class SprinterIntSource_Test : public SprinterFixture
{
protected:
    /// Squares a0..a1 (inclusive) of mode-table row b get Mode0 = value
    void SetRun(SprinterVideoRam& vram, uint8_t b, uint8_t a0, uint8_t a1, uint8_t value, uint8_t page = 0)
    {
        for (uint8_t a = a0; a <= a1; a++)
            vram.Write(SprinterVideoRam::ModeAddress(a, b, page), value);
    }
};

// The header's worked example: row 30, squares 40-45 blank + INT, square 46 not -> T 59 110 (MAME 59 120)
TEST_F(SprinterIntSource_Test, Positions_WorkedExample)
{
    SprinterVideoRam vram;
    SetRun(vram, 30, 40, 45, 0xFD);
    const auto positions = SprinterIntSource::ComputePositions(vram, 0, 320);
    ASSERT_EQ(positions.size(), 1u);
    EXPECT_EQ(positions[0], 263u * 224 + 832 / 4 - SprinterIntSource::kIntBeforeMameT);
    EXPECT_EQ(positions[0], 59110u);

    // The other mode page has no INT; 312 lines shift the rows (b = scr_b - 2 mod 39)
    EXPECT_TRUE(SprinterIntSource::ComputePositions(vram, 1, 320).empty());
    const auto p312 = SprinterIntSource::ComputePositions(vram, 0, 312);
    ASSERT_EQ(p312.size(), 1u);
    EXPECT_EQ(p312[0], 59110u);
}

// A run that reaches the end of the row never fires; #FC (blank, no INT) does not arm
TEST_F(SprinterIntSource_Test, Positions_NeedAFollowingSquare)
{
    SprinterVideoRam vram;
    SetRun(vram, 5, 44, 49, 0xFD);  // scr_a = a + 6: 50..55, the row ends
    EXPECT_TRUE(SprinterIntSource::ComputePositions(vram, 0, 320).empty());
    SetRun(vram, 7, 0, 3, 0xFC);
    EXPECT_TRUE(SprinterIntSource::ComputePositions(vram, 0, 320).empty());
    SetRun(vram, 7, 0, 3, 0xFF);  // %1111 11x1 matches #FF too
    EXPECT_EQ(SprinterIntSource::ComputePositions(vram, 0, 320).size(), 1u);
}

// The CPU sees /INT for 32 T from the position, once (the acknowledge ends it); the list follows VRAM writes and RGMOD
TEST_F(SprinterIntSource_Test, Pulse_AssertAcknowledgeAndInvalidation)
{
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    SprinterIntSource& source = _decoder->GetIntSource();
    // The CPU's interrupt source is the Z84C15's daisy chain with the PLD's /INT behind it
    IInterruptSource* cpuSource = _z80->GetInterruptSource();
    ASSERT_NE(cpuSource, nullptr);
    ASSERT_NE(_z80->GetEngine(), nullptr) << "the Sprinter runs on the Z84C15 library";
    EXPECT_FALSE(source.IsIntAsserted(59110));

    SetRun(vram, 30, 40, 45, 0xFD);
    const uint32_t ratio = _context->emulatorState.current_z80_frequency_multiplier;
    EXPECT_FALSE(source.IsIntAsserted(59109 * ratio));
    EXPECT_TRUE(source.IsIntAsserted(59110 * ratio));
    EXPECT_TRUE(cpuSource->IsIntAsserted(59110 * ratio)) << "the chain passes the PLD's /INT through";
    EXPECT_TRUE(source.IsIntAsserted((59110 + 31) * ratio));
    EXPECT_FALSE(source.IsIntAsserted((59110 + 32) * ratio));

    EXPECT_EQ(source.AcknowledgeInterrupt(59115 * ratio), 0xFF);
    EXPECT_FALSE(source.IsIntAsserted(59116 * ratio)) << "one INT per pulse";

    // RGMOD bit 0 selects the other mode page: no INT there
    source.SetModePage(1);
    EXPECT_TRUE(source.Positions().empty());
    source.SetModePage(0);
    EXPECT_EQ(source.Positions().size(), 1u);
}
