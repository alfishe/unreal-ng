#include "stdafx.h"
#include "pch.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "_helpers/romeditortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/zxprogramfiles.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/assembler/z80textassembler.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
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
///
/// fusetest-coemu (the same folder) wraps it for the co-emulation harness: a copy of everything it prints goes to
/// a buffer in memory and a DONE byte is set when it returns. FuseTestCoemu_Test checks that buffer against the
/// screen, and the files built from the wrapper and fusetest's own code.

namespace
{
std::string FuseTestPath(const std::string& file = "fusetest.tap")
{
    return (TestPathHelper::FindProjectRoot() / "tools" / "verification" / "contention" / "fusetest" / file)
        .make_preferred()
        .string();
}

constexpr uint16_t kCoemuOrg = 0x9000;
constexpr uint16_t kFuseOrg = 0xA000;

/// fusetest's code block (loaded at #A000) from its tape: the second data block, without its flag and checksum
std::vector<uint8_t> FuseTestCode()
{
    const std::vector<uint8_t> tap = ZxProgramFiles::ReadBinary(FuseTestPath());
    std::vector<std::vector<uint8_t>> blocks;
    for (size_t i = 0; i + 2 <= tap.size();)
    {
        const size_t n = tap[i] | (tap[i + 1] << 8);
        if (n < 2 || i + 2 + n > tap.size())
            break;
        blocks.emplace_back(tap.begin() + static_cast<std::ptrdiff_t>(i + 2), tap.begin() + static_cast<std::ptrdiff_t>(i + 2 + n));
        i += 2 + n;
    }
    // header, BASIC, header (type 3, start #A000), code
    if (blocks.size() != 4 || blocks[2].size() < 16 || blocks[2][1] != 3 || (blocks[2][14] | (blocks[2][15] << 8)) != kFuseOrg)
        return {};
    return std::vector<uint8_t>(blocks[3].begin() + 1, blocks[3].end() - 1);
}

/// The wrapper, padded to #A000, then fusetest's code
struct CoemuProgram
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

CoemuProgram BuildCoemu()
{
    CoemuProgram p;
    Z80TextAssembler assembler;
    p.asmResult = assembler.Assemble(ZxProgramFiles::ReadText(FuseTestPath("fusetest-coemu.asm")), kCoemuOrg);
    if (!p.asmResult.ok)
    {
        ADD_FAILURE() << p.asmResult.error.line << ": " << p.asmResult.error.message << " | " << p.asmResult.error.sourceLine;
        return p;
    }
    const std::vector<uint8_t> code = FuseTestCode();
    if (code.empty() || p.asmResult.bytes.size() > kFuseOrg - kCoemuOrg)
    {
        ADD_FAILURE() << "fusetest.tap has no code block at #A000, or the wrapper runs into it";
        return p;
    }
    p.bytes = p.asmResult.bytes;
    p.bytes.resize(kFuseOrg - kCoemuOrg, 0);
    p.bytes.insert(p.bytes.end(), code.begin(), code.end());
    return p;
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

/// The wrapper's buffer holds the report the screen shows, on the 48K
class FuseTestCoemu_Test : public FuseTest_Test
{
};

TEST_F(FuseTestCoemu_Test, BufferHoldsTheReport)
{
    const CoemuProgram p = BuildCoemu();
    ASSERT_FALSE(p.bytes.empty());
    BootEditor("48K");
    ASSERT_FALSE(HasFatalFailure());
    for (size_t i = 0; i < p.bytes.size(); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kCoemuOrg + i), p.bytes[i]);
    Z80* z80 = _context->pCore->GetZ80();
    z80->pc = p.Sym("HOSTENTRY");
    z80->sp = static_cast<uint16_t>(kCoemuOrg - 2);  // as CLEAR 36863 leaves it
    ASSERT_TRUE(RunUntil([&] { return _context->pMemory->DirectReadFromZ80Memory(p.Sym("DONE")) == 1; }, 6000))
        << "fusetest never returned:\n" << Screen();

    std::string buffer;
    for (uint16_t a = p.Sym("BUFFER"); a < p.Sym("BUFEND"); a++)
    {
        const uint8_t c = _context->pMemory->DirectReadFromZ80Memory(a);
        if (c == 0)
            break;
        buffer += c == 13 ? '\n' : static_cast<char>(c);
    }
    std::string joined;  // as Printed(): the lines run together
    for (char c : buffer)
        if (c != '\n')
            joined += c;
    const std::string printed = Printed();
    EXPECT_NE(buffer.find("Machine type: 48K"), std::string::npos) << buffer;
    const std::vector<Machine> machines = Machines();
    const Machine& m = machines.front();
    for (const std::string& name : TestNames())
    {
        EXPECT_EQ(Verdict(joined, name), m.expected.at(name)) << name << "\n" << buffer;
        EXPECT_EQ(Verdict(joined, name), Verdict(printed, name)) << name << ": the buffer and the screen differ";
    }
}

/// The committed harness files are what the wrapper and fusetest.tap build (no drift)
TEST(FuseTestCoemuFiles_Test, CommittedFilesMatchTheSource)
{
    const CoemuProgram p = BuildCoemu();
    ASSERT_FALSE(p.bytes.empty());
    const std::string heading = "; fusetest-coemu symbols (generated by fusetest_test.cpp)";
    EXPECT_TRUE(ZxProgramFiles::ReadBinary(FuseTestPath("fusetest-coemu.tap")) ==
                ZxProgramFiles::BuildTap("fusetest", kCoemuOrg, p.bytes))
        << "rebuild with UNREAL_FUSETEST_EXPORT=1";
    EXPECT_TRUE(ZxProgramFiles::ReadBinary(FuseTestPath("fusetest-coemu.trd")) ==
                ZxProgramFiles::BuildTrd("fusetest", kCoemuOrg, p.bytes))
        << "rebuild with UNREAL_FUSETEST_EXPORT=1";
    EXPECT_TRUE(ZxProgramFiles::ReadBinary(FuseTestPath("fusetest-coemu.sym")) ==
                ZxProgramFiles::BuildSym(heading, p.asmResult.symbols))
        << "rebuild with UNREAL_FUSETEST_EXPORT=1";
}

/// UNREAL_FUSETEST_EXPORT=1 writes them
TEST(FuseTestCoemuFiles_Test, Export)
{
    if (!std::getenv("UNREAL_FUSETEST_EXPORT"))
        GTEST_SKIP() << "set UNREAL_FUSETEST_EXPORT=1 to write fusetest-coemu.tap / .trd / .sym";
    const CoemuProgram p = BuildCoemu();
    ASSERT_FALSE(p.bytes.empty());
    const std::string heading = "; fusetest-coemu symbols (generated by fusetest_test.cpp)";
    const std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        { "fusetest-coemu.tap", ZxProgramFiles::BuildTap("fusetest", kCoemuOrg, p.bytes) },
        { "fusetest-coemu.trd", ZxProgramFiles::BuildTrd("fusetest", kCoemuOrg, p.bytes) },
        { "fusetest-coemu.sym", ZxProgramFiles::BuildSym(heading, p.asmResult.symbols) }
    };
    for (const auto& [file, data] : files)
    {
        std::ofstream out(FuseTestPath(file), std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        ASSERT_TRUE(out.good()) << file;
    }
}
