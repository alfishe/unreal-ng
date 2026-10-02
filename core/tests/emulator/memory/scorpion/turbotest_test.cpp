#include "stdafx.h"
#include "pch.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/zxprogramfiles.h"
#include "debugger/assembler/z80textassembler.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

/// turbotest (tools/verification/contention/turbotest): how many times five bodies run in one frame, at 3.5 MHz
/// and in turbo, on a Scorpion ZS-256 Turbo+ (7 MHz) and a ZX-Evo (14 MHz). This suite
/// - builds it with the in-tree assembler and fills in its expected tables with what unreal-ng counts: the
///   Scorpion under each logic firmware (SC15.1, SC15.3), the ZX-Evo;
/// - checks those counts against the research's per-instruction figures (an independent estimate:
///   research-scorpion-turbo.md section 4.3, research-zxevo.md section A);
/// - runs the finished program on a Scorpion with each firmware and checks it names the firmware, on the ZX-Evo
///   that it matches, and on machines without either turbo that it leaves them alone;
/// - keeps the committed .tap / .trd / .sym in step with the source.

namespace
{
constexpr uint16_t kOrg = 36000;
constexpr size_t kBodies = 5;

std::string TurboPath(const std::string& file)
{
    return (TestPathHelper::FindProjectRoot() / "tools" / "verification" / "contention" / "turbotest" / file)
        .make_preferred()
        .string();
}

struct TurboProgram
{
    AsmResult asmResult;
    std::vector<uint8_t> bytes;

    uint16_t Sym(const char* name) const
    {
        auto it = asmResult.symbols.find(name);
        EXPECT_NE(it, asmResult.symbols.end()) << name;
        return it == asmResult.symbols.end() ? 0 : static_cast<uint16_t>(it->second);
    }
};

TurboProgram Assemble()
{
    TurboProgram p;
    Z80TextAssembler assembler;
    p.asmResult = assembler.Assemble(ZxProgramFiles::ReadText(TurboPath("turbotest.asm")), kOrg);
    if (!p.asmResult.ok)
        ADD_FAILURE() << p.asmResult.error.line << ": " << p.asmResult.error.message << " | "
                      << p.asmResult.error.sourceLine;
    else
        p.bytes = p.asmResult.bytes;
    return p;
}

/// Per body: the 3.5 MHz count, then the turbo count
using Counts = std::vector<uint32_t>;

std::string Describe(const Counts& c)
{
    static const char* const names[kBodies] = { "NOP", "LD A,(RAM)", "LD A,(ROM)", "LD (RAM),A", "OUT (FE),A" };
    std::string s;
    for (size_t b = 0; b < kBodies && 2 * b + 1 < c.size(); b++)
        s += std::string("\n  ") + names[b] + ": " + std::to_string(c[2 * b]) + " / " + std::to_string(c[2 * b + 1]);
    return s;
}
}  // namespace

class TurboTest_Test : public RomEditorFixture
{
protected:
    uint8_t Peek(uint16_t a) const { return _context->pMemory->DirectReadFromZ80Memory(a); }

    /// Boots `editor`, optionally sets the Scorpion's logic firmware, runs `p` from its host entry until DONE.
    /// The boot is the slow part (the program itself runs 11 frames)
    void Run(const char* editor, const TurboProgram& p, const ScorpionTurboLogic* logic, bool force)
    {
        BootEditor(editor);
        ASSERT_FALSE(HasFatalFailure());
        if (logic)
        {
            // What [MISC] ScorpionTurboLogic gives a new machine; the decoder makes a new overlay when turbo is
            // switched on next
            _context->config.scorpionTurboLogic = *logic;
            _context->config.even_M1 = *logic == ScorpionTurboLogic::SC151 ? 1 : 0;
        }
        ASSERT_LT(kOrg + p.bytes.size(), static_cast<size_t>(p.Sym("LOOPBUF"))) << "the program runs into its loop";
        for (size_t i = 0; i < p.bytes.size(); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kOrg + i), p.bytes[i]);
        _context->pMemory->DirectWriteToZ80Memory(p.Sym("FORCE"), force ? 1 : 0);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = p.Sym("HOSTENTRY");
        z80->sp = 0xBDFE;
        ASSERT_TRUE(RunUntil([&] { return Peek(p.Sym("DONE")) == 1; }, 200))
            << "turbotest never finished: PC #" << std::hex << z80->pc << "\n" << Screen();
    }

    Counts Read(uint16_t at) const
    {
        Counts c;
        for (size_t i = 0; i < 2 * kBodies; i++)
            c.push_back(Peek(static_cast<uint16_t>(at + 3 * i)) | (Peek(static_cast<uint16_t>(at + 3 * i + 1)) << 8) |
                        (Peek(static_cast<uint16_t>(at + 3 * i + 2)) << 16));
        return c;
    }

    /// The counts unreal-ng gives under `logic`, turbo forced (the tables are not there yet)
    Counts Measure(ScorpionTurboLogic logic)
    {
        const TurboProgram plain = Assemble();
        Run("Scorpion-48BASIC", plain, &logic, true);
        const Counts c = HasFatalFailure() ? Counts{} : Read(plain.Sym("COUNTS"));
        TearDown();
        SetUp();
        return c;
    }

    /// The counts unreal-ng gives on the ZX-Evo, and its frame length at 3.5 MHz
    Counts MeasureEvo(uint32_t* frame = nullptr)
    {
        const TurboProgram plain = Assemble();
        Run("ATM3-TRDOS", plain, nullptr, false);
        const Counts c = HasFatalFailure() ? Counts{} : Read(plain.Sym("COUNTS"));
        if (frame && _context)
            *frame = _context->config.frame;
        TearDown();
        SetUp();
        return c;
    }
};

namespace
{
/// The program with both tables filled in by unreal-ng (a fixture, because filling them needs two runs)
class Generator : public TurboTest_Test
{
public:
    void TestBody() override {}
    TurboProgram Build(Counts* sc151, Counts* sc153, Counts* evo, uint32_t* evoFrame)
    {
        SetUp();
        const Counts a = Measure(ScorpionTurboLogic::SC151);
        const Counts b = Measure(ScorpionTurboLogic::SC153);
        const Counts e = MeasureEvo(evoFrame);
        TearDown();
        TurboProgram p = Assemble();
        auto fill = [&](const char* table, const Counts& c) {
            const uint16_t at = p.Sym(table);
            for (size_t i = 0; i < c.size(); i++)
                for (size_t k = 0; k < 3; k++)
                    p.bytes[at - kOrg + 3 * i + k] = static_cast<uint8_t>(c[i] >> (8 * k));
        };
        fill("EXP151", a);
        fill("EXP153", b);
        fill("EXPEVO", e);
        *sc151 = a;
        *sc153 = b;
        *evo = e;
        return p;
    }
};

/// The counts behind the tables
struct Tables
{
    Counts sc151, sc153, evo;
    uint32_t evoFrame = 0;  ///< the ZX-Evo's frame at 3.5 MHz
};

/// Built once per process: the tables take three machine boots
TurboProgram BuildTurboTest(Tables* tables = nullptr)
{
    static TurboProgram program;
    static Tables made;
    if (program.bytes.empty())
    {
        Generator g;
        program = g.Build(&made.sc151, &made.sc153, &made.evo, &made.evoFrame);
    }
    if (tables)
        *tables = made;
    return program;
}

/// The research's steady costs (research-scorpion-turbo.md 4.3) give a count per frame: a loop pass is 32 bodies
/// and its tail (INC DE / JP: in turbo T, `tailPaper` in the picture, `tailBorder` in the border), the frame
/// 2 x 69888 turbo T of which 192 lines x 256 T are in the picture. The interrupt and its 3.5 MHz stretch are left
/// out: the estimate is good to a few percent
double EstimatedTurboCount(double paperCost, double borderCost, double tailPaper, double tailBorder)
{
    const double frame = 2.0 * 69888;
    const double paper = 192.0 * 256;
    return paper / (paperCost + tailPaper / 32) + (frame - paper) / (borderCost + tailBorder / 32);
}

/// At 3.5 MHz: a pass is 32 bodies of `cost` T and the 16 T tail; with Even M1 every instruction from RAM is
/// rounded up to an even length
double EstimatedNormalCount(double cost, bool evenM1, double frame = 69888)
{
    if (evenM1)
        cost += static_cast<int>(cost) & 1;
    return frame * 32 / (32 * cost + 16);
}

/// The ZX-Evo at 14 MHz (research-zxevo.md A.2-A.5): a pass of 32 bodies of `cost` and the tail, in 14 MHz T, a
/// frame of 4 x `frame` of them (no picture / border difference)
double EstimatedEvoCount(double cost, double tail, uint32_t frame)
{
    return 4.0 * frame * 32 / (32 * cost + tail);
}
}  // namespace

/// unreal-ng's counts under each firmware agree with the research's per-instruction figures
TEST_F(TurboTest_Test, CountsAgreeWithTheResearchFigures)
{
    Tables t;
    BuildTurboTest(&t);
    const Counts& sc151 = t.sc151;
    const Counts& sc153 = t.sc153;
    ASSERT_EQ(sc151.size(), 2 * kBodies);
    ASSERT_EQ(sc153.size(), 2 * kBodies);
    auto near = [](uint32_t got, double estimate) { return got > 0.97 * estimate && got < 1.03 * estimate; };
    auto within = [](uint32_t got, double low, double high) { return got > 0.97 * low && got < 1.03 * high; };

    // 3.5 MHz: no waits at #A000 on a Scorpion; a NOP is 4 T, LD A,(HL) / LD (HL),A 7, OUT (n),A 11; Even M1
    // with SC15.1 only
    for (const Counts* c : { &sc151, &sc153 })
    {
        const bool evenM1 = c == &sc151;
        EXPECT_TRUE(near((*c)[0], EstimatedNormalCount(4, evenM1))) << Describe(*c);
        EXPECT_TRUE(near((*c)[2], EstimatedNormalCount(7, evenM1))) << Describe(*c);
        EXPECT_TRUE(near((*c)[4], EstimatedNormalCount(7, evenM1))) << Describe(*c);
        EXPECT_TRUE(near((*c)[6], EstimatedNormalCount(7, evenM1))) << Describe(*c);
        EXPECT_TRUE(near((*c)[8], EstimatedNormalCount(11, evenM1))) << Describe(*c);
    }

    // Turbo, SC15.1: NOP 8 / 6, LD A,(HL) and LD (HL),A 12 / 10, OUT (n),A 17-20 / 15-16. The tail: INC DE is a
    // NOP and 2 T, JP a NOP and two reads (a read adds 4: LD A,(HL) minus a NOP): 26 / 22
    EXPECT_TRUE(near(sc151[1], EstimatedTurboCount(8, 6, 26, 22))) << Describe(sc151);
    EXPECT_TRUE(near(sc151[3], EstimatedTurboCount(12, 10, 26, 22))) << Describe(sc151);
    EXPECT_TRUE(near(sc151[7], EstimatedTurboCount(12, 10, 26, 22))) << Describe(sc151);
    EXPECT_TRUE(within(sc151[9], EstimatedTurboCount(20, 16, 26, 22), EstimatedTurboCount(17, 15, 26, 22)))
        << Describe(sc151);
    // SC15.3: NOP 4 / 4, LD 8 / 7-8, OUT 12-15 / 12-13. The tail: 4 + 2, and 4 + 4 + 4 (3.5-4 in the border)
    EXPECT_TRUE(near(sc153[1], EstimatedTurboCount(4, 4, 18, 17))) << Describe(sc153);
    EXPECT_TRUE(within(sc153[3], EstimatedTurboCount(8, 8, 18, 18), EstimatedTurboCount(8, 7, 18, 17)))
        << Describe(sc153);
    EXPECT_TRUE(within(sc153[7], EstimatedTurboCount(8, 8, 18, 18), EstimatedTurboCount(8, 7, 18, 17)))
        << Describe(sc153);
    EXPECT_TRUE(within(sc153[9], EstimatedTurboCount(15, 13, 18, 18), EstimatedTurboCount(12, 12, 18, 17)))
        << Describe(sc153);

    // A read from ROM does not wait for the slot: faster than from RAM under SC15.1, where the read waits; under
    // SC15.3 the fetch's wait already puts the read on its slot, so it is no slower
    EXPECT_GT(sc151[5], sc151[3]) << Describe(sc151);
    EXPECT_GE(sc153[5], sc153[3]) << Describe(sc153);

    // The ZX-Evo. 3.5 MHz: no waits. 14 MHz: a RAM read (M1 or data) that misses both one-word caches waits
    // 2 or 3 T by the T-state's parity; ROM never waits but empties both caches, as does any I/O.
    // - NOP: the fetch misses on every even address (a new word): 4 + 2 or 3, then 4: 5 on average;
    // - LD A,(RAM) / LD (RAM),A: the fetches as the NOPs (a miss starts on an odd T after the 3 T access: 7),
    //   the data read hits (the same word every time), the write waits never: 8 to 8.5;
    // - LD A,(ROM): every fetch misses (the ROM read empties the caches) and starts on an odd T: 7 + 3 = 10;
    // - OUT (FE),A: the fetch misses (the I/O empties the caches), the operand hits (the opcode's word),
    //   the I/O cycle 4: 7 + 3 + 4 = 14 (13 when the fetch starts on an even T).
    // The tail: INC DE misses (8 or 9), JP hits and its operand misses once (12 or 13): 20 to 22
    const Counts& evo = t.evo;
    ASSERT_EQ(evo.size(), 2 * kBodies);
    ASSERT_GT(t.evoFrame, 0u);
    EXPECT_TRUE(near(evo[0], EstimatedNormalCount(4, false, t.evoFrame))) << Describe(evo);
    EXPECT_TRUE(near(evo[2], EstimatedNormalCount(7, false, t.evoFrame))) << Describe(evo);
    EXPECT_TRUE(near(evo[8], EstimatedNormalCount(11, false, t.evoFrame))) << Describe(evo);
    EXPECT_TRUE(within(evo[1], EstimatedEvoCount(5, 22, t.evoFrame), EstimatedEvoCount(5, 20, t.evoFrame)))
        << Describe(evo);
    EXPECT_TRUE(within(evo[3], EstimatedEvoCount(8.5, 22, t.evoFrame), EstimatedEvoCount(8, 20, t.evoFrame)))
        << Describe(evo);
    EXPECT_TRUE(within(evo[5], EstimatedEvoCount(10, 22, t.evoFrame), EstimatedEvoCount(10, 20, t.evoFrame)))
        << Describe(evo);
    EXPECT_TRUE(within(evo[7], EstimatedEvoCount(8.5, 22, t.evoFrame), EstimatedEvoCount(8, 20, t.evoFrame)))
        << Describe(evo);
    EXPECT_TRUE(within(evo[9], EstimatedEvoCount(14, 22, t.evoFrame), EstimatedEvoCount(13, 20, t.evoFrame)))
        << Describe(evo);
}

/// The finished program names the firmware the Scorpion has, starting from the turbo its ROM leaves on
TEST_F(TurboTest_Test, NamesTheScorpionFirmware)
{
    const TurboProgram p = BuildTurboTest();
    ASSERT_FALSE(p.bytes.empty());
    SetUp();
    for (ScorpionTurboLogic logic : { ScorpionTurboLogic::SC151, ScorpionTurboLogic::SC153 })
    {
        Run("Scorpion-48BASIC", p, &logic, false);
        ASSERT_FALSE(HasFatalFailure());
        const uint8_t want = logic == ScorpionTurboLogic::SC151 ? 1 : 2;
        EXPECT_EQ(Peek(p.Sym("MATCH")), want) << Describe(Read(p.Sym("COUNTS"))) << "\n" << Screen();
        EXPECT_EQ(Peek(p.Sym("FAILS")) | (Peek(p.Sym("FAILS") + 1) << 8), 0) << Screen();
        EXPECT_TRUE(ScreenHas(want == 1 ? "Turbo+ logic: SC15.1" : "Turbo+ logic: SC15.3")) << Screen();
        if (std::getenv("TURBOTEST_SCREEN"))
            std::printf("%s\n", Screen().c_str());
        TearDown();
        SetUp();
    }
}

/// The finished program on the ZX-Evo: as modeled, and back at 3.5 MHz at the end
TEST_F(TurboTest_Test, MatchesOnTheZxEvo)
{
    const TurboProgram p = BuildTurboTest();
    ASSERT_FALSE(p.bytes.empty());
    SetUp();
    Run("ATM3-TRDOS", p, nullptr, false);
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(Peek(p.Sym("EVO")), 1) << Screen();
    EXPECT_EQ(Peek(p.Sym("MATCH")), 3) << Describe(Read(p.Sym("COUNTS"))) << "\n" << Screen();
    EXPECT_TRUE(ScreenHas("ZX-Evo 14 MHz: as modeled")) << Screen();
    EXPECT_EQ(_context->emulatorState.hw_turbo_ratio, 1) << "3.5 MHz again";
    if (std::getenv("TURBOTEST_SCREEN"))
        std::printf("%s\n", Screen().c_str());
}

/// Machines without either turbo: the 128K and the Pentagon are not touched (no read of #7FFD / #1FFD, which can
/// page memory on a 128K); the 48K and unreal-ng's ATM Turbo 2+ have the Scorpion's frame and pass its check, the
/// reads change nothing there; nothing is written to #xx77, the ATM's system port
TEST_F(TurboTest_Test, LeavesOtherMachinesAlone)
{
    const TurboProgram p = BuildTurboTest();
    ASSERT_FALSE(p.bytes.empty());
    SetUp();
    const struct
    {
        const char* editor;
        uint8_t match;
    } machines[] = { { "128K-48BASIC", 0xFE }, { "Pentagon-48BASIC", 0xFE }, { "ATM710-TRDOS", 0xFF }, { "48K", 0xFF } };
    for (const auto& m : machines)
    {
        BootEditor(m.editor);
        ASSERT_FALSE(HasFatalFailure()) << m.editor;
        const uint8_t value77 = _context->emulatorState.pFF77;
        const unsigned port77 = _context->emulatorState.aFF77;
        TearDown();
        SetUp();
        Run(m.editor, p, nullptr, false);
        ASSERT_FALSE(HasFatalFailure()) << m.editor;
        EXPECT_EQ(_context->emulatorState.pFF77, value77) << m.editor << ": #xx77 written";
        EXPECT_EQ(_context->emulatorState.aFF77, port77) << m.editor << ": #xx77 written";
        EXPECT_EQ(Peek(p.Sym("MATCH")), m.match) << m.editor << Describe(Read(p.Sym("COUNTS"))) << "\n" << Screen();
        EXPECT_EQ(Peek(p.Sym("EVO")), 0) << m.editor;
        TearDown();
        SetUp();
    }
}

/// The committed reference files are what the source and the model build (no drift)
TEST(TurboTestFiles_Test, CommittedFilesMatchTheSource)
{
    const TurboProgram p = BuildTurboTest();
    ASSERT_FALSE(p.bytes.empty());
    const std::string heading = "; turbotest symbols (generated by turbotest_test.cpp)";
    EXPECT_TRUE(ZxProgramFiles::ReadBinary(TurboPath("turbotest.tap")) == ZxProgramFiles::BuildTap("turbotest", kOrg, p.bytes))
        << "rebuild with UNREAL_TURBOTEST_EXPORT=1";
    EXPECT_TRUE(ZxProgramFiles::ReadBinary(TurboPath("turbotest.trd")) == ZxProgramFiles::BuildTrd("turbotest", kOrg, p.bytes))
        << "rebuild with UNREAL_TURBOTEST_EXPORT=1";
    EXPECT_TRUE(ZxProgramFiles::ReadBinary(TurboPath("turbotest.sym")) ==
                ZxProgramFiles::BuildSym(heading, p.asmResult.symbols))
        << "rebuild with UNREAL_TURBOTEST_EXPORT=1";
}

/// UNREAL_TURBOTEST_EXPORT=1 writes the reference files
TEST(TurboTestFiles_Test, Export)
{
    if (!std::getenv("UNREAL_TURBOTEST_EXPORT"))
        GTEST_SKIP() << "set UNREAL_TURBOTEST_EXPORT=1 to write turbotest.tap / .trd / .sym";
    const TurboProgram p = BuildTurboTest();
    ASSERT_FALSE(p.bytes.empty());
    const std::string heading = "; turbotest symbols (generated by turbotest_test.cpp)";
    const std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        { "turbotest.tap", ZxProgramFiles::BuildTap("turbotest", kOrg, p.bytes) },
        { "turbotest.trd", ZxProgramFiles::BuildTrd("turbotest", kOrg, p.bytes) },
        { "turbotest.sym", ZxProgramFiles::BuildSym(heading, p.asmResult.symbols) }
    };
    for (const auto& [file, data] : files)
    {
        std::ofstream out(TurboPath(file), std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        ASSERT_TRUE(out.good()) << file;
    }
}
