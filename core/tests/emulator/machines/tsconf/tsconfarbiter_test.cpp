// The TS-Conf DRAM arbiter at 14 MHz (TSConf implementation-plan phase 8
// TIM-1; tsconfarbiter.h). The expected costs were worked out independently
// with an fclk-level model of arbiter.v / zmem.v / zclock.v (all four block
// alignments tried): inside the fetch window a NOP run costs no more than
// outside it in every mode; 256C makes each write lose its own DRAM cycle.

#include "tsconffixture.h"

#include "emulator/platforms/tsconf/tsconfarbiter.h"
#include "emulator/platforms/tsconf/tsconfengine.h"

class TsConfArbiter_Test : public TsConfFixture
{
protected:
    using Access = TsConfArbiter::Access;

    /// Every line of the frame set with `vConfig`
    void Mode(uint8_t vConfig)
    {
        Reg(TsConfReg::VConfig, vConfig);
        TsConfEngine& engine = _decoder->GetEngine();
        engine.OnMachineFrameRollover(TsConfEngine::kFrameTacts);
        engine.CatchUp(TsConfEngine::kFrameTacts - 1);
    }

    /// Average fclk per instruction of a loop run on line `line` from DRAM
    /// cycle `from` to `to` (the second half measured). `write`: M1 then a
    /// write 3 T (6 fclk) after the M1's 4 T; else a NOP
    double Loop(uint32_t line, uint32_t from, uint32_t to, bool write)
    {
        TsConfArbiter arbiter(_decoder->GetEngine());
        const uint32_t lineStart = line * TsConfArbiter::kLineCycles * 4;
        uint32_t f = lineStart + from * 4;  // T1 of the instruction, fclk
        const uint32_t end = lineStart + to * 4;
        const uint32_t middle = (f + end) / 2;
        uint32_t count = 0;
        uint32_t spent = 0;
        while (f < end)
        {
            const uint32_t begin = f;
            f += 8 + arbiter.CpuAccess(f + 3, Access::M1);  // M1: 4 T
            if (write)
                f += 6 + arbiter.CpuAccess(f + 3, Access::Write);  // write: 3 T
            if (begin >= middle)
            {
                count++;
                spent += f - begin;
            }
        }
        return count ? static_cast<double>(spent) / count : 0.0;
    }
};

/// The fetch window of each mode (video_sync.v:237): h0 = hpix_beg - go_offs - x_offs
TEST_F(TsConfArbiter_Test, ARB1_FetchWindows)
{
    TsConfLine set;
    set.vConfig = 0x00;  // ZX 256x192
    TsConfArbiter::Fetch f = TsConfArbiter::FetchOf(set, 100);
    EXPECT_TRUE(f.active);
    EXPECT_EQ(f.h0, 122);
    EXPECT_EQ(f.h1, 382);
    EXPECT_EQ(f.length, 8);
    EXPECT_EQ(f.need, 1);
    EXPECT_FALSE(TsConfArbiter::FetchOf(set, 79).active) << "above the window";

    set.vConfig = 0x40 | 0x02;  // 256C 320x200
    set.gxOffs = 3;
    f = TsConfArbiter::FetchOf(set, 100);
    EXPECT_EQ(f.h0, 108 - 4 - 1) << "256C: x_offs = GX[0]";
    EXPECT_EQ(f.length, 2);

    set.vConfig = 0x80 | 0x03;  // TXT 320x240
    set.gxOffs = 3;
    f = TsConfArbiter::FetchOf(set, 100);
    EXPECT_EQ(f.h0, 108 - 10 - 3);
    EXPECT_EQ(f.need, 4);

    set.vConfig = 0x20;  // NOGFX
    EXPECT_FALSE(TsConfArbiter::FetchOf(set, 100).active);
}

/// Border (no fetch): the zmem.v table alone. A NOP run settles at M1 c2
/// (+4): 12 fclk; M1 + write at c0 (+6), the write free: 20 fclk
TEST_F(TsConfArbiter_Test, ARB2_BorderIsTheTable)
{
    Mode(0x80 | 0x03);  // TXT 320x240: lines 56..295
    EXPECT_DOUBLE_EQ(Loop(10, 0, 440, false), 12.0);
    EXPECT_DOUBLE_EQ(Loop(10, 0, 440, true), 20.0);
    EXPECT_DOUBLE_EQ(Loop(100, 0, 90, false), 12.0) << "left of the fetch window";
}

/// Inside the window: the CPU takes at most 3 of every 8 cycles in a NOP run,
/// video never has to refuse it - TXT and 256C as in the border
TEST_F(TsConfArbiter_Test, ARB3_NopsAreNotDelayedByVideo)
{
    for (uint8_t vConfig : {uint8_t(0x80 | 0x03), uint8_t(0x40 | 0x02), uint8_t(0x40 | 0x01), uint8_t(0x00)})
    {
        SCOPED_TRACE(int(vConfig));
        Mode(vConfig);
        EXPECT_DOUBLE_EQ(Loop(150, 140, 400, false), 12.0);
    }
}

/// 256C: blocks of 2 with one video cycle - a write granted a block's first
/// cycle leaves vid_rem == blk_rem for its own cycle, which stops the CPU
/// clock (stall14_cyc): LD (HL),A 20 -> 24 fclk. ZX / 16C: no change
TEST_F(TsConfArbiter_Test, ARB4_WritesIn256C)
{
    Mode(0x40 | 0x02);
    EXPECT_DOUBLE_EQ(Loop(150, 140, 400, true), 24.0);
    Mode(0x40 | 0x01);
    EXPECT_DOUBLE_EQ(Loop(150, 140, 400, true), 20.0) << "16C";
    Mode(0x00);
    EXPECT_DOUBLE_EQ(Loop(150, 150, 370, true), 20.0) << "ZX";
}

/// A request in a cycle video must have: the read waits for the grant (G > D)
/// and then as from c0 - zmem.v's "special case"
TEST_F(TsConfArbiter_Test, ARB5_RefusedReadWaitsForTheGrant)
{
    Mode(0x80 | 0x03);  // TXT: 4 video cycles per 8
    TsConfArbiter arbiter(_decoder->GetEngine());
    const uint32_t line = 150 * TsConfArbiter::kLineCycles;
    const uint32_t block = line + 98;  // h0 of TXT 320x240: the first block decision
    // Four writes back to back from the block decision take four cycles; the
    // remaining four must be video's
    uint32_t f = block * 4 + 3;
    for (int i = 0; i < 4; i++)
        f += 4 + arbiter.CpuAccess(f, Access::Write);
    const uint32_t wait = arbiter.CpuAccess(f, Access::Read);
    EXPECT_GE(wait, 4u + 4u) << "at least one refused cycle plus the c0 cost";
}
