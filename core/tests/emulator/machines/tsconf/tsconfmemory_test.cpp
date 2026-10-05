// TS-Conf memory: window 0 policy, windows 1-3, the CPU cache
// (TSConf implementation-plan phase 1, hardware-spec §2).

#include "tsconffixture.h"

#include <fstream>
#include <sstream>

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
    EXPECT_EQ(_core->GetBusOverlayCount(), 1u) << "the DRAM write overlay also invalidates: no overlay of its own";
    Reg(TsConfReg::SysConfig, 0x01);
    EXPECT_EQ(_decoder->GetState().regs[TsConfReg::CacheConfig], 0x00);
    EXPECT_EQ(_core->GetBusOverlayCount(), 1u) << "the DRAM write overlay stays (writes invalidate with the cache off too)";
}

namespace
{
    /// One case of testdata/machines/tsconf/rtl-sim/cache-retention.txt (tsconf-cpu-sim cache): the program
    /// tokens and, per read, the byte the Z80 got and whether it took a DRAM cycle (M) or hit (H)
    struct RtlCacheCase
    {
        std::string name;
        std::vector<std::string> prog;
        struct Read
        {
            uint16_t addr;
            uint8_t got;
            bool miss;
        };
        std::vector<Read> reads;
    };

    std::vector<RtlCacheCase> LoadRtlCacheCases(const std::string& path)
    {
        std::vector<RtlCacheCase> cases;
        std::ifstream in(FileHelper::ToFsPath(path));
        std::string line;
        while (std::getline(in, line))
        {
            std::istringstream ls(line);
            std::string key;
            ls >> key;
            if (key == "case")
            {
                cases.emplace_back();
                ls >> cases.back().name;
            }
            else if (key == "prog" && !cases.empty())
            {
                std::string all, token;
                ls >> all;
                std::istringstream ts(all);
                while (std::getline(ts, token, ','))
                    cases.back().prog.push_back(token);
            }
            else if (key == "reads" && !cases.empty())
            {
                std::string r;  // aaaa=gg/mmX[!]: address, byte the Z80 got, DRAM byte, H / M
                while (ls >> r)
                    cases.back().reads.push_back({static_cast<uint16_t>(std::stoul(r.substr(0, 4), nullptr, 16)),
                                                  static_cast<uint8_t>(std::stoul(r.substr(5, 2), nullptr, 16)),
                                                  r[10] == 'M'});
            }
        }
        return cases;
    }
}

/// CCH-3 (hs §2.5, [V] zmem.v:213-266, arbiter.v:214): the cache's fill and retention replayed from the RTL
/// (tools/machines/tsconf/rtl-sim `tsconf-cpu-sim cache`, testdata/machines/tsconf/rtl-sim/cache-retention.txt).
/// Every CPU DRAM read fills its entry whatever CACHE_CONFIG says (cpu_strobe writes the cache RAM on every CPU
/// read cycle); CACHE_CONFIG only decides whether a valid entry answers instead of DRAM. Nothing clears the cache:
/// not switching it off, not a reset. A CPU write to RAM invalidates the entry it hits, cache on or off. So a
/// word read with the cache off and then changed by DMA is answered stale once the cache is switched on.
/// Each case starts from a fresh FPGA configuration (cache RAM zeroed: all invalid), MEM_CONFIG 04h and pages
/// 00h, 20h..22h as in the harness; a PK token changes DRAM directly, as a DMA write does
TEST_F(TsConfMemory_Test, CCH3_CacheFillAndRetentionMatchTheRtl)
{
    const std::vector<RtlCacheCase> cases =
        LoadRtlCacheCases(TestPathHelper::GetTestDataPath("machines/tsconf/rtl-sim/cache-retention.txt"));
    ASSERT_GE(cases.size(), 10u);

    TsConfState& ts = _decoder->GetState();
    static constexpr uint8_t kPages[4] = {0x00, 0x20, 0x21, 0x22};
    auto mapWindows = [&] {
        Reg(TsConfReg::MemConfig, TsConfMemConfig::W0NoMap);
        for (uint8_t w = 0; w < 4; w++)
            Reg(static_cast<uint8_t>(TsConfReg::Page0 + w), kPages[w]);
    };
    auto dram = [&](uint16_t addr) -> uint8_t& { return Ram(kPages[addr >> 14], addr & 0x3FFF); };

    for (const RtlCacheCase& c : cases)
    {
        SCOPED_TRACE(c.name);
        std::memset(ts.cacheTag, 0, sizeof(ts.cacheTag));  // FPGA configuration: every entry invalid
        std::memset(ts.cacheWord, 0, sizeof(ts.cacheWord));
        Reg(TsConfReg::CacheConfig, 0x00);
        mapWindows();
        for (uint32_t a = 0x8000; a <= 0xBFFF; a++)
            dram(static_cast<uint16_t>(a)) = 0x00;
        for (uint32_t a = 0xC000; a <= 0xFFFF; a++)
            dram(static_cast<uint16_t>(a)) = static_cast<uint8_t>((a * 7 + 3) & 0x7F);

        size_t read = 0;
        for (const std::string& t : c.prog)
        {
            auto hex = [&](size_t from, size_t len) { return std::stoul(t.substr(from, len), nullptr, 16); };
            if (t.rfind("CE.", 0) == 0)
                Reg(TsConfReg::CacheConfig, static_cast<uint8_t>(hex(3, 1)));
            else if (t.rfind("PK.", 0) == 0)
                dram(static_cast<uint16_t>(hex(3, 4))) = static_cast<uint8_t>(hex(8, 2));  // a DMA write
            else if (t.rfind("WR.", 0) == 0)
                Poke(static_cast<uint16_t>(hex(3, 4)), static_cast<uint8_t>(hex(8, 2)));
            else if (t == "RST1")
                _decoder->reset();
            else if (t == "RST0")
                mapWindows();  // the harness keeps the pages; after a real reset the program sets them again
            else if (t.rfind("RD.", 0) == 0)
            {
                ASSERT_LT(read, c.reads.size());
                const RtlCacheCase::Read& r = c.reads[read++];
                const uint32_t before = ts.cpuAccesses;
                const uint8_t got = Peek(r.addr);
                EXPECT_EQ(got, r.got) << "read #" << read << " at " << std::hex << r.addr << ": byte";
                EXPECT_EQ(ts.cpuAccesses != before, r.miss) << "read #" << read << " at " << std::hex << r.addr
                                                            << ": DRAM cycle (miss) or hit";
            }
        }
        EXPECT_EQ(read, c.reads.size());
    }
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

    // LD A,(HL) (M1 + read, a read waits one fclk longer than M1: +4..+7 by
    // phase, the RTL's release at c2): settles at M1 c1 (+5) and read c2 (+5),
    // 24 fclk = 12 clocks. LD (HL),A (M1 + write, the write does not wait
    // outside the fetch window): M1 c0 (+6), 20 fclk = 10 clocks
    _z80->hl = 0x8800;
    std::vector<uint8_t> loads;
    for (int i = 0; i < 32; i++)
        loads.push_back(0x7E);
    RunCode(loads);
    EXPECT_DOUBLE_EQ(ClocksOf(_z80, [&] { RunCode(loads); }), 32 * 12.0);
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

// The debugger's "writable" per window (Memory::IsWindowWritable, debugger additions D9): what the mapper does with a
// CPU write - window 0 RAM is writable only with W0_WE, ROM never, windows 1-3 always
TEST_F(TsConfMemory_Test, WindowWritableFollowsTheMapper)
{
    Reg(TsConfReg::MemConfig, TsConfMemConfig::W0NoMap);  // ROM in window 0
    EXPECT_FALSE(_memory->IsWindowWritable(0));
    Reg(TsConfReg::MemConfig, TsConfMemConfig::W0NoMap | TsConfMemConfig::W0Ram);  // RAM, W0_WE off
    EXPECT_FALSE(_memory->IsWindowWritable(0)) << "RAM without W0_WE: writes go nowhere";
    Reg(TsConfReg::MemConfig, TsConfMemConfig::W0NoMap | TsConfMemConfig::W0Ram | TsConfMemConfig::W0We);
    EXPECT_TRUE(_memory->IsWindowWritable(0));
    EXPECT_TRUE(_memory->IsWindowWritable(1));
    EXPECT_TRUE(_memory->IsWindowWritable(3));
}

/// TIM-6: a CPU write to RAM takes a DRAM cycle ([V] zmem.v:121 ramreq = ... || (memwr && ramwr_en)), so it comes
/// off the line's free budget like a read; a write to ROM or to a write-protected window starts none. Writes were
/// not counted: write-heavy code left the DMA and the TSU more cycles than the hardware ([U] counts them too)
TEST_F(TsConfMemory_Test, TIM6_CpuWritesTakeADramCycle)
{
    TsConfState& ts = _decoder->GetState();
    uint32_t before = ts.cpuAccesses;
    Poke(0x8000, 0x11);
    EXPECT_EQ(ts.cpuAccesses, before + 1) << "RAM in window 2";

    before = ts.cpuAccesses;
    Poke(0x0100, 0x22);
    EXPECT_EQ(ts.cpuAccesses, before) << "ROM in window 0";

    Reg(TsConfReg::MemConfig, 0x0C);  // window 0 = RAM, write-protected
    before = ts.cpuAccesses;
    Poke(0x0100, 0x33);
    EXPECT_EQ(ts.cpuAccesses, before) << "a write-protected window starts no DRAM cycle";

    Reg(TsConfReg::MemConfig, 0x0E);
    before = ts.cpuAccesses;
    Poke(0x0100, 0x44);
    EXPECT_EQ(ts.cpuAccesses, before + 1);
}
