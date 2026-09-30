#include "stdafx.h"
#include "pch.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"

/// fusetest (tools/verification/contention/fusetest; docs/inprogress/2026-09-28-m1-contention/test-programs.md
/// §2.2): FUSE's own regression program by Philip Kendall (GPL), assembled from the FUSE source with pasmo. Loaded
/// from its tape the way a user does, it measures the frame length, guesses the machine, finds the first contended
/// T-state and runs twelve tests (flags, memory and I/O contention, the floating bus, paging port reads), printing
/// "passed", "failed (0xNN)", "incomplete (B=0xNN)" or "skipped" after each name. This suite reads that report off
/// the screen and compares every line with what the real machine prints (FUSE 1.6.0 prints the same).
///
/// Each case boots a ROM, loads the tape and runs the program for a few hundred frames: 0.1-0.3 s.

namespace
{
std::string FuseTestPath()
{
    return (TestPathHelper::FindProjectRoot() / "tools" / "verification" / "contention" / "fusetest" / "fusetest.tap")
        .make_preferred()
        .string();
}

/// The twelve tests, in the order the program runs them (fusetest.asm, _testdata)
const std::vector<std::string>& TestNames()
{
    static const std::vector<std::string> names = { "BIT n,(IX+d)",           "DAA",
                                                    "OUTI",                   "LDIR",
                                                    "Contended IN",           "Floating bus",
                                                    "Contended memory",       "High port contention 1",
                                                    "High port contention 2", "0xbffd read",
                                                    "0x3ffd read",            "0x7ffd read" };
    return names;
}

/// A line where unreal-ng does not print what the hardware prints: the test pins what unreal-ng prints today, so
/// a fix shows up as a failure here (then drop the entry)
struct KnownDeviation
{
    std::string printed;  // what unreal-ng prints now
    std::string why;
};

struct Machine
{
    const char* editor;                                // RomEditorFixture::BootEditor
    std::vector<std::string> commands;                 // typed in order to load the tape
    const char* machineLine;                           // what the program prints after "Machine type: "
    std::map<std::string, std::string> expected;       // test name -> "passed" / "skipped" (the hardware)
    std::map<std::string, KnownDeviation> deviations;  // unreal-ng bugs, see above
    std::vector<std::string> unchecked;                // lines whose output means nothing on this machine
};

void PrintTo(const Machine& m, std::ostream* os)
{
    *os << m.editor;
}

// The floating bus sample point. The Z80 takes the data of an IN at the end of the I/O cycle, after every wait
// state the ULA inserts. For a port whose high byte is in contended memory (#40xx-#7Fxx) the ULA stretches the
// cycle after IORQ too (pattern C:1 C:1 C:1 C:1), so the byte is read up to 12 T later than at IORQ. unreal-ng looks
// the byte up at IORQ (UlaContention::FetchedByte, called from the Z80 port read before the late wait states), which
// is right only when nothing stretches the cycle after IORQ. fusetest's floating bus test reads #40FF at 43046 T
// (48K): FUSE samples at 43069 T (the attribute of column 15, the #53 the program planted); unreal-ng samples at
// 43055 T, an idle phase, and reads #FF
const char* kLateSample = "floating bus sampled at IORQ, before the late ULA wait states of a contended-high-byte "
                          "port (the Z80 reads at the end of the stretched I/O cycle)";

std::vector<Machine> Machines()
{
    auto base = [](const std::string& floating, const std::string& paging) {
        std::map<std::string, std::string> e;
        for (const std::string& n : TestNames())
            e[n] = "passed";
        e["Floating bus"] = floating;  // mask: 48K, 128K
        e["0x3ffd read"] = paging;     // mask: 128K
        e["0x7ffd read"] = paging;
        return e;
    };
    return {
        { "48K", { "LOAD \"\"" }, "48K", base("passed", "skipped"),
          { { "Floating bus", { "failed (0xff)", kLateSample } } }, {} },

        { "128K-128BASIC", { "LOAD \"\"" }, "128K", base("passed", "passed"),
          { { "Floating bus", { "failed (0xff)", kLateSample } },
            // The 128K / +2 paging latch is clocked by IORQ with A15 = 0 and A1 = 0 and does not look at WR: an IN
            // from #7FFD (or a mirror such as #3FFD) latches whatever is on the data bus, here the floating bus
            // byte (FUSE periph.c readport, "writeback" for the 128 and +2; ZEsarUX blanks the screen on such a
            // read too). The program plants attribute bytes 2 and 4 at #5802 / #5803 and expects RAM page 2 / 4
            // at #C000 afterwards; unreal-ng never latches on IN, so page 0 stays. #7FFD also needs the late
            // sample above (its high byte is contended): with the latch alone it would read #FF, page 7
            { "0x3ffd read", { "failed (0x00)", "IN from a #7FFD-decoded port does not latch the bus into #7FFD" } },
            { "0x7ffd read",
              { "failed (0x00)", "IN from a #7FFD-decoded port does not latch the bus into #7FFD; and the late "
                                 "floating bus sample" } } },
          {} },

        { "Plus3-3BASIC", { "LOAD \"t:\"", "LOAD \"\"" }, "+3", base("skipped", "skipped"),
          // The +2A / +3 gate array decodes AY reads with A15 and A1 only: #BFFD reads the selected register like
          // #FFFD (FUSE ay_ports_plus3; ZEsarUX "BFFD R: +2A/+3 mirror of FFFD"). The program selects register
          // 11, writes #55 and expects #55 back; unreal-ng returns #FF (printed as #FF - #55)
          { { "0xbffd read", { "failed (0xaa)", "#BFFD read on the +2A/+3 is not a mirror of #FFFD" } } }, {} },

        // fusetest cannot test the Pentagon. guessmachine.asm (since FUSE SVN r3852, unchanged in the latest
        // revision) falls through from its Pentagon branch into the TS2068 one (no `jr _end` after
        // `ld hl, _mpentstring`), so a 71680 T frame prints "TS2068" and every table lookup takes the TS2068 entry
        // (index 4): the contention search gives up ("negative contention?"), the timing tests run with TS2068
        // delays, and the tables with fewer than five entries are read past their end. FUSE 1.6.0 prints exactly
        // the same report. Only the three tests that do not depend on the machine are checked
        { "Pentagon-128BASIC", { "LOAD \"\"" }, "TS2068", base("passed", "passed"), {},
          { "LDIR", "Contended IN", "Floating bus", "Contended memory", "High port contention 1",
            "High port contention 2", "0xbffd read", "0x3ffd read", "0x7ffd read" } },
    };
}

std::string MachineName(const ::testing::TestParamInfo<Machine>& info)
{
    std::string name = info.param.editor;
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)))
            c = '_';
    return name;
}
}  // namespace

class FuseTest_Test : public RomEditorFixture, public ::testing::WithParamInterface<Machine>
{
protected:
    /// The screen as the program printed it: the OCR rows joined (each is 32 characters, a carriage return pads
    /// the rest of its row with spaces), so a result that wrapped onto the next row reads as one string
    std::string Printed() const
    {
        std::string s;
        for (char c : Screen())
            if (c != '\n')
                s += c;
        return s;
    }

    /// The verdict after "<name>... ": "passed", "skipped", "failed (0xNN)", "incomplete (B=0xNN)"; empty if the
    /// line is not there (yet)
    static std::string Verdict(const std::string& printed, const std::string& name)
    {
        const std::string key = name + "... ";
        const size_t at = printed.find(key);
        if (at == std::string::npos)
            return {};
        const std::string rest = printed.substr(at + key.size());
        for (const char* word : { "passed", "skipped" })
            if (rest.rfind(word, 0) == 0)
                return word;
        for (const char* word : { "failed (", "incomplete (" })
        {
            if (rest.rfind(word, 0) == 0)
            {
                const size_t close = rest.find(')');
                return close == std::string::npos ? std::string{} : rest.substr(0, close + 1);
            }
        }
        return {};
    }
};

TEST_P(FuseTest_Test, EveryTestPrintsWhatTheHardwarePrints)
{
    const Machine& m = GetParam();
    BootEditor(m.editor);
    ASSERT_FALSE(HasFatalFailure());
    _context->pFeatureManager->setFeature(Features::kFastTape, true);
    ASSERT_TRUE(_emulator->LoadTape(FuseTestPath())) << FuseTestPath();

    CommandTyper* typer = _context->pDebugManager->GetCommandTyper();
    for (const std::string& command : m.commands)
    {
        ASSERT_TRUE(typer->Request(command, CommandTyper::Options{})) << command;
        ASSERT_TRUE(RunUntil([&] { return typer->GetStatus() == CommandTyper::Status::Done; }, 4000)) << command;
    }

    // The last test's verdict ends the run
    const std::string& last = TestNames().back();
    const bool finished = RunUntil(
        [&] {
            const std::string printed = Printed();
            return !Verdict(printed, last).empty() || printed.find("Frame length unknown") != std::string::npos;
        },
        6000);
    ASSERT_TRUE(finished) << "the program never finished:\n" << Screen();
    if (std::getenv("UNREAL_FUSETEST_PRINT"))  // the whole report, for a look at a new machine
        std::printf("[fusetest %s]\n%s", m.editor, Screen().c_str());

    const std::string printed = Printed();
    EXPECT_NE(printed.find(std::string("Machine type: ") + m.machineLine), std::string::npos) << Screen();
    for (const std::string& name : TestNames())
    {
        const std::string verdict = Verdict(printed, name);
        bool unchecked = false;
        for (const std::string& u : m.unchecked)
            unchecked = unchecked || u == name;
        if (unchecked)
            EXPECT_FALSE(verdict.empty()) << name << ": no verdict\n" << Screen();
        else if (auto d = m.deviations.find(name); d != m.deviations.end())
            EXPECT_EQ(verdict, d->second.printed)
                << name << ": known unreal-ng deviation changed (" << d->second.why << "); the hardware prints '"
                << m.expected.at(name) << "'. If unreal-ng now does too, drop the entry\n"
                << Screen();
        else
            EXPECT_EQ(verdict, m.expected.at(name)) << name << "\n" << Screen();
    }
}

INSTANTIATE_TEST_SUITE_P(Machines, FuseTest_Test, ::testing::ValuesIn(Machines()), MachineName);
