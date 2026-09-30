#include "stdafx.h"
#include "pch.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/soundcardscope.h"
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
    bool hasAy;                                        // an AY on the board (fusetest reads #BFFD / #FFFD)
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

// Three lines once differed from the hardware and are right since the fixes of
// docs/inprogress/2026-09-30-fusetest-core-defects (checked against the RTL, the 128K service manual and a PAL
// readout, not against FUSE alone): the floating bus of a port whose high byte is in contended memory is sampled at
// the end of the stretched I/O cycle ("Floating bus"), an IN from the 128K's #7FFD decode writes the bus byte into
// the paging latch ("0x3ffd read", "0x7ffd read"), and #BFFD on the +2A / +3 reads the AY register ("0xbffd read")

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
        { "48K", false, { "LOAD \"\"" }, "48K", base("passed", "skipped"), {}, {} },

        { "128K-128BASIC", true, { "LOAD \"\"" }, "128K", base("passed", "passed"), {}, {} },

        { "Plus3-3BASIC", true, { "LOAD \"t:\"", "LOAD \"\"" }, "+3", base("skipped", "skipped"), {}, {} },

        // fusetest cannot test the Pentagon. guessmachine.asm (since FUSE SVN r3852, unchanged in the latest
        // revision) falls through from its Pentagon branch into the TS2068 one (no `jr _end` after
        // `ld hl, _mpentstring`), so a 71680 T frame prints "TS2068" and every table lookup takes the TS2068 entry
        // (index 4): the contention search gives up ("negative contention?"), the timing tests run with TS2068
        // delays, and the tables with fewer than five entries are read past their end. FUSE 1.6.0 prints exactly
        // the same report. Only the three tests that do not depend on the machine are checked
        { "Pentagon-128BASIC", true, { "LOAD \"\"" }, "TS2068", base("passed", "passed"), {},
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
    // The test runner leaves the sound slot empty; the 128K and +3 have their AY on the board, the 48K none
    std::optional<SoundCardScope> ay;
    if (m.hasAy)
        ay.emplace(TestSound::TurboSound);
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
