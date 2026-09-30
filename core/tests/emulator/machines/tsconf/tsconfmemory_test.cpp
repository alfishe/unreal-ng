// TS-Conf memory: window 0 policy, windows 1-3, the CPU cache
// (TSConf implementation-plan phase 1, hardware-spec §2).

#include "tsconffixture.h"

class TsConfMemory_Test : public TsConfFixture
{
};

/// MEM-1 (hs §1, §2.2): normal mode shows PAGE0 directly; ROM uses page[4:0]
TEST_F(TsConfMemory_Test, MEM1_NormalModeRomPage)
{
    Reg(TsConfReg::Page0, 0x1F);
    EXPECT_FALSE(IsRam(0x0000));
    EXPECT_EQ(Tag(0x0000), 0x1F);
    Reg(TsConfReg::Page0, 0x25);
    EXPECT_EQ(Tag(0x0000), 0x05);
}

/// MEM-2: W0_RAM shows RAM PAGE0; writes need W0_WE
TEST_F(TsConfMemory_Test, MEM2_WindowZeroRamAndWriteEnable)
{
    Reg(TsConfReg::MemConfig, 0x0C);
    Reg(TsConfReg::Page0, 0x80);
    EXPECT_TRUE(IsRam(0x0000));
    EXPECT_EQ(Tag(0x0000), 0x80);

    Poke(0x0100, 0xAA);
    EXPECT_EQ(Ram(0x80, 0x0100), 0x80) << "write-protected";
    EXPECT_EQ(_memory->GetPhysPageForZ80Address(0x0000), 0x80) << "still RAM page #80 for TTD";

    Reg(TsConfReg::MemConfig, 0x0E);
    Poke(0x0100, 0xAA);
    EXPECT_EQ(Ram(0x80, 0x0100), 0xAA);
    EXPECT_EQ(_memory->GetPhysPageForZ80Address(0x0000), 0x80);
}

/// MEM-3: the mapped-mode group layout {service, TR-DOS, 128, 48}
TEST_F(TsConfMemory_Test, MEM3_MappedModeGroupLayout)
{
    Reg(TsConfReg::Page0, 0x04);
    Reg(TsConfReg::MemConfig, 0x00);
    EXPECT_EQ(Tag(0x0000), 0x06) << "BASIC-128";
    Reg(TsConfReg::MemConfig, 0x01);
    EXPECT_EQ(Tag(0x0000), 0x07) << "BASIC-48";

    _decoder->GetState().dos = 1;
    _decoder->ApplyState();
    EXPECT_EQ(Tag(0x0000), 0x05) << "TR-DOS";
    Reg(TsConfReg::MemConfig, 0x00);
    EXPECT_EQ(Tag(0x0000), 0x04) << "service";
}

/// MEM-4: the mapped formula applies to RAM too
TEST_F(TsConfMemory_Test, MEM4_MappedRam)
{
    Reg(TsConfReg::MemConfig, 0x09);
    Reg(TsConfReg::Page0, 0x40);
    EXPECT_TRUE(IsRam(0x0000));
    EXPECT_EQ(Tag(0x0000), 0x43);
}

/// MEM-5 (hs §2.1): windows 1-3 reach the whole 4 MB; page 255 is tracked by TTD
TEST_F(TsConfMemory_Test, MEM5_WindowsReachTheTopPage)
{
    Reg(TsConfReg::Page1, 0xFF);
    EXPECT_EQ(Tag(0x4000), 0xFF);
    EXPECT_EQ(_memory->GetPhysPageForZ80Address(0x4000), 0xFF);
    Poke(0x4100, 0x12);
    EXPECT_EQ(Ram(0xFF, 0x0100), 0x12);
}

/// CCH-1 (hs §2.5): a hit returns the cached word even when RAM changed under
/// it (DMA-like); a CPU write invalidates the entry
TEST_F(TsConfMemory_Test, CCH1_HitReturnsTheCachedWord)
{
    Reg(TsConfReg::CacheConfig, 0x04);  // W2
    EXPECT_EQ(Peek(0x8000), 0x02) << "fills";
    Ram(2, 0x0000) = 0x77;              // not a CPU write
    Ram(2, 0x0001) = 0x78;
    EXPECT_EQ(Peek(0x8000), 0x02) << "hit: the old value";
    EXPECT_EQ(Peek(0x8001), 0x02) << "the whole word was cached";

    Poke(0x8000, 0x99);
    EXPECT_EQ(Peek(0x8000), 0x99) << "invalidated by the CPU write, refilled";
    EXPECT_EQ(Peek(0x8001), 0x78);

    Ram(3, 0x0010) = 0x55;
    Reg(TsConfReg::Page2, 0x03);
    EXPECT_EQ(Peek(0x8010), 0x55) << "another page is another tag";
}

/// CCH-1 companion: windows without the enable bit read RAM
TEST_F(TsConfMemory_Test, CacheOnlyForEnabledWindows)
{
    Reg(TsConfReg::CacheConfig, 0x04);
    EXPECT_EQ(Peek(0xC000), 0x00);
    Ram(0, 0x0000) = 0x44;
    EXPECT_EQ(Peek(0xC000), 0x44) << "W3 is not cached";
}

/// CCH-2: SYS_CONFIG bit 2 writes all four CACHE_CONFIG bits
TEST_F(TsConfMemory_Test, CCH2_SysConfigCopiesTheCacheBit)
{
    Reg(TsConfReg::SysConfig, 0x04);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::CacheConfig], 0x0F);
    EXPECT_EQ(_core->GetBusOverlayCount(), 1u) << "the invalidation snoop";
    Reg(TsConfReg::SysConfig, 0x01);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::CacheConfig], 0x00);
    EXPECT_EQ(_core->GetBusOverlayCount(), 0u);
}

namespace
{
    /// Z80 clocks at the current rate that `run` took
    template <typename Run>
    double ClocksOf(Z80* z80, Run run)
    {
        const uint32_t before = z80->tt;
        run();
        return static_cast<double>(z80->tt - before) / z80->rate;
    }
}

/// TIM-1: 14 MHz DRAM waits ([V] zmem.v:154-172, fclk = half a 14 MHz clock).
/// A straight run of NOPs from uncached RAM settles at M1 phase c2 (+4 fclk):
/// 6 clocks per NOP. Worked out by hand from the table: NOP starting at fclk
/// F has its request at F + 3 in phase (F + 3) mod 4 and ends 8 + wait later;
/// from any start the run reaches F = 3 (mod 4), wait 4, next F = F + 12
TEST_F(TsConfMemory_Test, TIM1_UncachedRamWaitsAt14MHz)
{
    Reg(TsConfReg::SysConfig, 0x02);  // 14 MHz, cache off
    ASSERT_EQ(_context->emulatorState.hw_turbo_ratio, 4);
    std::vector<uint8_t> nops(65, 0x00);
    _memory->DirectWriteToZ80Memory(0x8000, 0x00);
    RunCode({0x00});  // settle the phase
    const double clocks = ClocksOf(_z80, [&] { RunCode(nops); });
    EXPECT_DOUBLE_EQ(clocks, 65 * 6.0) << "NOP from DRAM: 4 + 2 clocks";

    // LD A,(HL) (M1 + read): 20 fclk = 10 clocks; LD (HL),A (M1 + write, the
    // write does not wait): 20 fclk = 10 clocks - both settle by the table
    _z80->hl = 0x8800;
    std::vector<uint8_t> loads;
    for (int i = 0; i < 32; i++)
        loads.push_back(0x7E);
    RunCode(loads);
    EXPECT_DOUBLE_EQ(ClocksOf(_z80, [&] { RunCode(loads); }), 32 * 10.0);
    std::vector<uint8_t> stores(32, 0x77);
    RunCode(stores);
    EXPECT_DOUBLE_EQ(ClocksOf(_z80, [&] { RunCode(stores); }), 32 * 10.0);
}

/// TIM-1: no waits at 3.5 / 7 MHz, from ROM, or on cache hits at 14 MHz
TEST_F(TsConfMemory_Test, TIM1_NoWaitsOffDram)
{
    std::vector<uint8_t> nops(64, 0x00);
    Reg(TsConfReg::SysConfig, 0x01);  // 7 MHz
    EXPECT_DOUBLE_EQ(ClocksOf(_z80, [&] { RunCode(nops); }), 64 * 4.0) << "7 MHz";

    Reg(TsConfReg::SysConfig, 0x02);  // 14 MHz: ROM (the tagged ROM page 0 is NOPs)
    _z80->pc = 0x0000;
    EXPECT_DOUBLE_EQ(ClocksOf(_z80, [&] {
                         for (int i = 0; i < 64; i++)
                             _z80->Z80Step();
                     }),
                     64 * 4.0)
        << "ROM is a separate chip";

    Reg(TsConfReg::SysConfig, 0x06);  // 14 MHz + cache in every window
    RunCode(nops);                    // fills
    EXPECT_DOUBLE_EQ(ClocksOf(_z80, [&] { RunCode(nops); }), 64 * 4.0) << "every fetch hits";
}
