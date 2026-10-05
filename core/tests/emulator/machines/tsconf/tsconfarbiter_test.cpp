// The TS-Conf DRAM arbiter at 14 MHz (TSConf implementation-plan phase 8
// TIM-1; tsconfarbiter.h). The expected costs were worked out independently
// with an fclk-level model of arbiter.v / zmem.v / zclock.v (all four block
// alignments tried): inside the fetch window a NOP run costs no more than
// outside it in every mode; 256C makes each write lose its own DRAM cycle.

#include "tsconffixture.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

/// A read whose request falls into a refused cycle waits for the grant and then as from c0 (zmem.v's "special
/// case"). 256C, a write and right after it a read, the write's T1 on 8 fclk in a row (two blocks of 2): where the
/// write is granted a block's first cycle, that cycle is refused to the CPU after it (cpu_next = 0). Write T1 at c1
/// of the cycle before (request c0, offset 5): the read's T1 falling edge is the edge already on its way when the
/// clock stops, so the clock stops for 1 fclk, the read's request lands in the refused cycle and waits for the
/// next block: 1 + 8 fclk, more than any read outside refused cycles (+4..+7). Offset 6: the read's MREQ / RD come
/// after the stop, which lasts the whole cycle (4 fclk, charged to the read: no read seen yet), then +4.
/// Offsets 0 and 7: the refused cycle falls into the write's own T3 (+4 on the write). The RTL simulation does
/// not run this pattern; the rules it exercises are the ones ARB6 checks
TEST_F(TsConfArbiter_Test, ARB5_RefusedReadWaitsForTheGrant)
{
    Mode(0x40 | 0x02);  // 256C 320x200
    const uint32_t base = (150 * TsConfArbiter::kLineCycles + 140) * 4;
    static constexpr uint32_t kWrite[8] = {4, 0, 0, 0, 0, 0, 0, 4};
    static constexpr uint32_t kRead[8] = {6, 5, 4, 7, 6, 9, 8, 7};
    for (uint32_t offset = 0; offset < 8; offset++)
    {
        SCOPED_TRACE(offset);
        TsConfArbiter arbiter(_decoder->GetEngine());
        const uint32_t t1 = base + offset;
        const uint32_t write = arbiter.CpuAccess(t1 + 3, Access::Write);
        EXPECT_EQ(write, kWrite[offset]);
        EXPECT_EQ(arbiter.CpuAccess(t1 + 6 + write + 3, Access::Read), kRead[offset]);
    }
}

namespace
{
    /// One test of tools/machines/tsconf/rtl-sim (results/cpu-waits.txt, format in its README)
    struct RtlCpuTest
    {
        struct Cycle
        {
            char kind = 'M';  ///< M1, R(ead), W(rite), I(nternal)
            uint16_t addr = 0;
            uint8_t data = 0;
            uint8_t tStates = 4;
        };
        std::string group;
        std::string name;
        uint8_t cache = 0;
        uint8_t vConfig = 0;
        uint32_t t0 = 0;
        uint32_t total = 0;
        std::vector<Cycle> prog;
        std::vector<int> wait;
    };

    /// The 14 MHz tests of the file; 3.5 / 7 MHz ones (no waits at all) are not in the copy
    std::vector<RtlCpuTest> LoadRtlCpuTests(const std::string& path)
    {
        std::vector<RtlCpuTest> tests;
        std::ifstream file(path);
        std::string text;
        while (std::getline(file, text))
        {
            if (text.empty() || text[0] == '#')
                continue;
            std::istringstream in(text);
            std::string key;
            in >> key;
            if (key == "run")
            {
                RtlCpuTest t;
                in >> t.group >> t.name;
                for (std::string kv; in >> kv;)
                {
                    const size_t eq = kv.find('=');
                    const std::string k = kv.substr(0, eq);
                    const std::string v = kv.substr(eq + 1);
                    if (k == "mhz" && v != "14")
                        t.group.clear();  // skipped
                    else if (k == "cache")
                        t.cache = static_cast<uint8_t>(std::stoul(v, nullptr, 16));
                    else if (k == "vconf")
                        t.vConfig = static_cast<uint8_t>(std::stoul(v, nullptr, 16));
                    else if (k == "t0")
                        t.t0 = static_cast<uint32_t>(std::stoul(v));
                    else if (k == "total")
                        t.total = static_cast<uint32_t>(std::stoul(v));
                }
                tests.push_back(t);
            }
            else if (tests.empty())
                continue;
            else if (key == "prog")
            {
                std::string list;
                in >> list;
                std::istringstream items(list);
                for (std::string item; std::getline(items, item, ',');)
                {
                    std::vector<std::string> f;
                    std::istringstream parts(item);
                    for (std::string part; std::getline(parts, part, '.');)
                        f.push_back(part);
                    RtlCpuTest::Cycle c;
                    if (f[0].rfind("ID", 0) == 0)
                    {
                        c.kind = 'I';
                        c.tStates = static_cast<uint8_t>(std::stoul(f[0].substr(2)));
                    }
                    else
                    {
                        c.kind = f[0] == "M1" ? 'M' : f[0][0];
                        c.addr = static_cast<uint16_t>(std::stoul(f[1], nullptr, 16));
                        c.tStates = c.kind == 'M' ? 4 : 3;
                        if (c.kind == 'W')
                            c.data = static_cast<uint8_t>(std::stoul(f[2], nullptr, 16));
                        const size_t lengthField = c.kind == 'W' ? 3 : 2;
                        if (f.size() > lengthField)
                            c.tStates = static_cast<uint8_t>(std::stoul(f[lengthField]));
                    }
                    tests.back().prog.push_back(c);
                }
            }
            else if (key == "wait")
            {
                for (int w; in >> w;)
                    tests.back().wait.push_back(w);
            }
        }
        std::erase_if(tests, [](const RtlCpuTest& t) { return t.group.empty(); });
        return tests;
    }

    /// Forwards the M1 hook to the decoder and notes the clock right after it: the opcode fetch's T1 once the
    /// clock stops of the previous machine cycle are charged (they belong to that cycle, as in the RTL)
    class M1Probe : public IMachineM1Hook
    {
    public:
        M1Probe(IMachineM1Hook& board, Z80& cpu) : _board(board), _cpu(cpu) {}
        void BeforeMachineM1(uint16_t address) override
        {
            _board.BeforeMachineM1(address);
            t1 = _cpu.tt;
        }
        void OnMachineM1(uint16_t address) override { _board.OnMachineM1(address); }
        uint32_t t1 = 0;

    private:
        IMachineM1Hook& _board;
        Z80& _cpu;
    };
}

/// The 14 MHz CPU waits against the real Verilog: every 14 MHz test of the RTL simulation
/// (tools/machines/tsconf/rtl-sim, testdata/machines/tsconf/rtl-sim/cpu-waits.txt) replayed through the
/// emulated CPU's own bus cycles (m1_cycle / rd / wd) on a TS-Conf at 14 MHz, with the harness's memory map
/// (MEM_CONFIG 04h: window 0 = ROM, pages 20h..22h), V_CONFIG and cache setting, each test's first T1 placed
/// on the RTL's frame fclk. Each machine cycle's length (T1 to the next T1) must equal the RTL's, fclk for
/// fclk: isolated M1 / read / write at every DRAM phase and 64 window positions per mode, three writes and a
/// read, the cache, ROM, and the NOP / LD A,(HL) / LD (HL),A / POP / PUSH / LDI loops. The refused cycles
/// (256C and TXT after the CPU took a block's spare cycles) stop the clock in the machine cycle they fall in,
/// except in the fclk the CPU is in a memory read (zmem.v stall14_cyc). Groups run in file order on one
/// arbiter each, as in the simulation. About 1200 tests of a few to 300 machine cycles: well under 50 ms
TEST_F(TsConfArbiter_Test, ARB6_CpuWaitsMatchTheRtl)
{
    const std::vector<RtlCpuTest> tests =
        LoadRtlCpuTests(TestPathHelper::GetTestDataPath("machines/tsconf/rtl-sim/cpu-waits.txt"));
    ASSERT_GT(tests.size(), 1000u);

    std::string group;
    M1Probe probe(*_decoder, *_z80);
    int failed = 0;
    int reported = 0;
    for (const RtlCpuTest& t : tests)
    {
        if (t.group != group)
        {
            group = t.group;
            Reg(TsConfReg::MemConfig, TsConfMemConfig::W0NoMap);
            Reg(TsConfReg::Page0, 0x00);
            Reg(TsConfReg::Page1, 0x20);
            Reg(TsConfReg::Page2, 0x21);
            Reg(TsConfReg::Page3, 0x22);
            Mode(t.vConfig);
            Reg(TsConfReg::SysConfig, 0x02);  // 14 MHz, a fresh arbiter
            // Each group is a fresh simulation: the cache RAM starts invalid (the FPGA configuration). Nothing
            // else clears it (zmem.v), so the previous group's entries go here
            std::memset(_decoder->GetState().cacheTag, 0, sizeof(_decoder->GetState().cacheTag));
            ASSERT_EQ(_context->emulatorState.hw_turbo_ratio, 4);
            Reg(TsConfReg::CacheConfig, t.cache);
            _z80->machineM1Hook = &probe;
        }

        const uint32_t fclk = _z80->rate / 2;
        _z80->tt = t.t0 * fclk;
        std::vector<uint32_t> t1;
        for (const RtlCpuTest::Cycle& c : t.prog)
        {
            t1.push_back(_z80->tt);
            switch (c.kind)
            {
                case 'M':
                    _z80->pc = c.addr;
                    _z80->m1_cycle();
                    t1.back() = probe.t1;
                    _z80->Idle(c.addr, static_cast<uint8_t>(c.tStates - 4));
                    break;
                case 'R':
                    _z80->rd(c.addr);
                    break;
                case 'W':
                    _z80->wd(c.addr, c.data);
                    _z80->Idle(c.addr, static_cast<uint8_t>(c.tStates - 3));
                    break;
                default:
                    _z80->Idle(0, c.tStates);
                    break;
            }
        }
        t1.push_back(_z80->tt);

        std::vector<int> wait;
        for (size_t i = 0; i < t.prog.size(); i++)
            wait.push_back(static_cast<int>((t1[i + 1] - t1[i]) / fclk) - 2 * t.prog[i].tStates);
        const uint32_t total = (t1.back() - t1.front()) / fclk;
        if (wait != t.wait || total != t.total)
        {
            failed++;
            if (reported++ < 20)
            {
                std::string rtl, emu;
                for (size_t i = 0; i < wait.size() && i < 8; i++)
                {
                    rtl += " " + std::to_string(t.wait[i]);
                    emu += " " + std::to_string(wait[i]);
                }
                ADD_FAILURE() << t.group << " " << t.name << " t0=" << t.t0 << ": total RTL " << t.total << " emulator "
                              << total << "; waits per machine cycle RTL" << rtl << " emulator" << emu;
            }
        }
    }
    _z80->machineM1Hook = _decoder;
    EXPECT_EQ(failed, 0) << "of " << tests.size() << " tests";
}
