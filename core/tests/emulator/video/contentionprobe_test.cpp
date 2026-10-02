#include "stdafx.h"
#include "pch.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/romeditortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/basic-lang/commandtyper.h"
#include "debugger/debugmanager.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/cpu/core.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"

/// Emulated-side contention checks (design docs/inprogress/2026-09-28-m1-contention, suite H): programs that
/// measure contention from inside the machine and print their verdict.
///
/// ZX Spectrum Timing Tests 48K v1.0 (Richard and Tim Butler, testdata/loaders/sna/Timing_Tests-48k_v1.0.sna):
/// 37 tests, each an instruction group run in a loop until the frame interrupt: 1-35 from uncontended and
/// from contended RAM, 36-37 from contended RAM with the screen full of text, so that test 35's port reads
/// (whose results become the next port's high byte) see the floating bus. The program prints R, the loop
/// count and SP and compares them with the values measured on a real 48K ("Pass" / "Fail" + "Expecting"). The screen is read with ScreenOCR (the program uses the
/// ROM font).
///
/// Phase 1 (M1 and data contention) moves the contended results towards the hardware but cannot pass them:
/// every group also has internal (no-MREQ) cycles the 48K ULA contends (phase 2) and some have port accesses
/// with the multi-point I/O pattern (phase 3). The expectations below pin the phase-1 values and name the
/// hardware ones, so every later phase shows up as a deliberate change here.
namespace
{
/// Tests 1-35 from uncontended and from contended RAM, then 36 and 37 (contended only, a screen full of text:
/// the port reads see the floating bus)
constexpr size_t kButlerResults = 72;

struct ButlerResult
{
    int r = -1;
    int loop = -1;
    int sp = -1;
    bool pass = false;
};

/// "Test N {Contended|Uncontended}" followed by "R=.. loop=.. sp=.. Pass|Fail"
std::map<std::pair<int, std::string>, ButlerResult> ParseButlerScreen(const std::string& text)
{
    std::map<std::pair<int, std::string>, ButlerResult> results;
    static const std::regex header(R"(Test (\d+) \{(\w+)\})");
    static const std::regex values(R"(R=(\d+)\s+loop=(\d+)\s+sp=(\d+)\s+(Pass|Fail))");

    std::istringstream lines(text);
    std::string line;
    std::pair<int, std::string> current{ -1, "" };
    while (std::getline(lines, line))
    {
        std::smatch m;
        if (std::regex_search(line, m, header))
        {
            current = { std::stoi(m[1]), m[2] };
            continue;
        }
        if (current.first >= 0 && std::regex_search(line, m, values) && !results.count(current))
            results[current] = { std::stoi(m[1]), std::stoi(m[2]), std::stoi(m[3]), m[4] == "Pass" };
    }
    return results;
}

/// A machine running a Butler suite (the 48K one by default); frames are counted, keys typed by holding them for a
/// few frames
class ButlerRunner
{
public:
    explicit ButlerRunner(bool contention, const std::string& model = "48K",
                          const std::string& snapshot = "loaders/sna/Timing_Tests-48k_v1.0.sna")
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        if (!_emulator)
            return;
        _emulator->GetFeatureManager()->setFeature(Features::kContention, contention);
        // A bare 48K, as the suite was measured on: the standard machine fits a Kempston mouse, whose ports
        // (#xx1F with A9 set) answer test 35's IN r,(C) with the mouse counters - a byte that sends the next
        // port into the contended high-byte range
        _emulator->GetFeatureManager()->setFeature(Features::kKempstonMouse, false);
        _loaded = _emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(snapshot));
    }

    ~ButlerRunner()
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    bool Loaded() const { return _emulator && _loaded; }

    std::string Screen() const { return ScreenOCR::ocrScreen(_emulator->GetId()); }

    /// Run until the screen shows `text` (checked every 10 frames); false after `maxFrames`
    bool RunUntilScreenShows(const std::string& text, int maxFrames)
    {
        for (int f = 0; f < maxFrames; f += 10)
        {
            for (int i = 0; i < 10; i++)
                _emulator->RunFrame(true);
            if (Screen().find(text) != std::string::npos)
                return true;
        }
        return false;
    }

    void RunFrames(int frames)
    {
        for (int i = 0; i < frames; i++)
            _emulator->RunFrame(true);
    }

    void Type(ZXKeysEnum key)
    {
        Keyboard* keyboard = _emulator->GetContext()->pKeyboard;
        keyboard->PressKey(key);
        for (int i = 0; i < 6; i++)
            _emulator->RunFrame(true);
        keyboard->ReleaseKey(key);
        for (int i = 0; i < 6; i++)
            _emulator->RunFrame(true);
    }

    /// From "choose test", runs test `only` (empty: all) and collects every result until `count` are in. The
    /// results scroll up the screen: the ROM asks "scroll?" when it is full, and the program waits for a key after
    /// a failing test. ENTER answers both (SPACE is BREAK at "scroll?"); a key the program was not scanning for is
    /// lost, so it is pressed again while the page stays unchanged
    std::map<std::pair<int, std::string>, ButlerResult> RunSuite(const std::string& only, size_t count, int maxFrames)
    {
        for (char c : only)
            Type(static_cast<ZXKeysEnum>(ZXKEY_0 + (c - '0')));
        Type(ZXKEY_ENTER);
        std::map<std::pair<int, std::string>, ButlerResult> all;
        std::string previous;
        int lastPress = 0;
        for (int frame = 0; frame < maxFrames && all.size() < count; frame += 2)
        {
            RunFrames(2);  // a passing test's lines scroll away within a few frames
            const std::string screen = Screen();
            if (screen.find("scroll?") != std::string::npos)
            {
                Type(ZXKEY_ENTER);
                continue;
            }
            for (const auto& [key, result] : ParseButlerScreen(screen))
                all.emplace(key, result);  // every poll: passing tests do not stop, their lines scroll on
            if (screen != previous && screen.find("ress any") != std::string::npos)
            {
                previous = screen;
                Type(ZXKEY_ENTER);
                lastPress = frame;
            }
            else if (screen == previous && frame - lastPress > 200)
            {
                Type(ZXKEY_ENTER);
                lastPress = frame;
            }
        }
        return all;
    }

private:
    Emulator* _emulator = nullptr;
    bool _loaded = false;
};
}  // namespace

/// Test 1 of the suite (JR; INC BC; LD BC,(nn); LD (nn),BC). Runs the real program to its verdict: about 600
/// frames of a 48K (the program's own loop counting), well over the 50 ms budget, and the one fast point
/// of the emulated-side suite in the default run
TEST(ContentionProbe_Test, ButlerTest1MatchesTheHardware)
{
    std::map<bool, ButlerResult> contended;
    for (bool contention : { true, false })
    {
        ButlerRunner runner(contention);
        ASSERT_TRUE(runner.Loaded()) << "Timing_Tests-48k_v1.0.sna";
        ASSERT_TRUE(runner.RunUntilScreenShows("choose test", 2000)) << runner.Screen();
        runner.Type(ZXKEY_1);
        runner.Type(ZXKEY_ENTER);
        // "Press any key for next test." after a failure, "All Tests Complete 100% Pass / Press any Key." after a pass
        ASSERT_TRUE(runner.RunUntilScreenShows("ress any", 3000)) << runner.Screen();

        const auto results = ParseButlerScreen(runner.Screen());
        ASSERT_TRUE(results.count({ 1, "Uncontended" }) && results.count({ 1, "Contended" })) << runner.Screen();
        const ButlerResult& uncontended = results.at({ 1, "Uncontended" });
        EXPECT_TRUE(uncontended.pass) << "uncontended code must match the hardware in any mode";
        EXPECT_EQ(uncontended.loop, 1201);
        contended[contention] = results.at({ 1, "Contended" });
    }

    // Hardware: R=74 loop 1014 SP 23296. Without contention the contended run is as fast as the uncontended
    // one (1201); phase 1 (fetch and data waits) brought it to 1036, phase 2 (the internal cycles of JR and
    // INC BC) to the hardware value
    EXPECT_EQ(contended[false].loop, 1201) << "switch off: nothing waits";
    EXPECT_EQ(contended[true].loop, 1014) << "the hardware value";
    EXPECT_TRUE(contended[true].pass);
}

/// The whole suite, both switch settings: ~7000 frames per run (about 3 s each) - opt-in with
/// UNREAL_TIMING_SUITES=1, like the tape sweep. Prints the table; with contention on every result matches
/// the real 48K
class ContentionProbeSweep_Test : public ::testing::TestWithParam<bool>
{
};

TEST_P(ContentionProbeSweep_Test, ButlerFullSuite)
{
    const bool contention = GetParam();
    ButlerRunner runner(contention);
    ASSERT_TRUE(runner.Loaded());
    ASSERT_TRUE(runner.RunUntilScreenShows("choose test", 2000));
    const auto all = runner.RunSuite("", kButlerResults, 60000);
    EXPECT_EQ(all.size(), kButlerResults) << "last screen:\n" << runner.Screen();

    int passed = 0;
    for (const auto& [key, result] : all)
    {
        std::cout << "[Butler 48K, contention " << (contention ? "on" : "off") << "] test " << key.first << " "
                  << key.second << ": R=" << result.r << " loop=" << result.loop << " sp=" << result.sp << " "
                  << (result.pass ? "Pass" : "Fail") << std::endl;
        passed += result.pass ? 1 : 0;
    }
    std::cout << "[Butler 48K, contention " << (contention ? "on" : "off") << "] " << passed << " of " << all.size()
              << " passed" << std::endl;

    if (contention)
    {
        for (const auto& [key, result] : all)
            EXPECT_TRUE(result.pass) << "test " << key.first << " " << key.second;
    }
}

INSTANTIATE_TEST_SUITE_P(Opt, ContentionProbeSweep_Test,
                         ::testing::ValuesIn(std::getenv("UNREAL_TIMING_SUITES") ? std::vector<bool>{ true, false }
                                                                                 : std::vector<bool>{}));
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ContentionProbeSweep_Test);

/// ZX Spectrum Timing Tests 128K v1.0 (Richard and Tim Butler, 2015; testdata/contention/butler, the SpecEmu .szx
/// from the authors' site, the same bytes as the zxe.io depot copy). Tests 1-34 of the 48K suite with the values of
/// the late-timing +2 it was written on ("MUST RUN IN 48k MODE": the 48K ROM paged in; no test 35, the program
/// stops after 34). An early-timing 128K fails tests 4, 17, 18, 26 and 33 from contended RAM, with the values
/// photographed on two real Toastracks (Issue 6K and 6U, Brendon Alford, the ZX Spectrum tests wiki
/// "ZX-Spectrum-Timing-Tests-128K"); unreal-ng's 128K has the early timings
namespace
{
constexpr size_t kButler128Results = 68;

/// The early 128K's results that differ from the suite's own: test -> R, loop, SP (contended)
const std::map<int, ButlerResult>& Early128KFailures()
{
    static const std::map<int, ButlerResult> failures = {
        { 4, { 6, 174, 23305, false } },    { 17, { 22, 203, 23335, false } }, { 18, { 22, 203, 23335, false } },
        { 26, { 75, 147, 23345, false } }, { 33, { 119, 196, 23315, false } },
    };
    return failures;
}
}  // namespace

/// Test 4 alone, the first one where an early 128K differs from the suite's +2 values. About 700 frames of a 128K
/// (the program's own loop counting), well over the 50 ms budget: the default run's end-to-end check of the 128K
/// timing against a hardware photograph
TEST(ContentionProbe_Test, Butler128KTest4MatchesTheEarlyHardware)
{
    ButlerRunner runner(true, "128k", "contention/butler/Timing_Tests-128k_v1.0.szx");
    ASSERT_TRUE(runner.Loaded()) << "Timing_Tests-128k_v1.0.szx";
    ASSERT_TRUE(runner.RunUntilScreenShows("choose test", 2000)) << runner.Screen();
    const auto results = runner.RunSuite("4", 2, 6000);
    ASSERT_TRUE(results.count({ 4, "Uncontended" }) && results.count({ 4, "Contended" })) << runner.Screen();
    EXPECT_TRUE(results.at({ 4, "Uncontended" }).pass);
    const ButlerResult& contended = results.at({ 4, "Contended" });
    const ButlerResult& hardware = Early128KFailures().at(4);
    EXPECT_EQ(contended.r, hardware.r);
    EXPECT_EQ(contended.loop, hardware.loop);
    EXPECT_EQ(contended.sp, hardware.sp);
    EXPECT_FALSE(contended.pass) << "the suite's values are a late +2's";
}

/// The whole 128K suite: ~7 s - opt-in with UNREAL_TIMING_SUITES=1. Every result as on the early hardware: the
/// five above fail with the photographed values, all others pass
TEST(ContentionProbe_Test, Butler128KFullSuiteMatchesTheEarlyHardware)
{
    if (!std::getenv("UNREAL_TIMING_SUITES"))
        GTEST_SKIP() << "opt-in: UNREAL_TIMING_SUITES=1";
    ButlerRunner runner(true, "128k", "contention/butler/Timing_Tests-128k_v1.0.szx");
    ASSERT_TRUE(runner.Loaded());
    ASSERT_TRUE(runner.RunUntilScreenShows("choose test", 2000)) << runner.Screen();
    const auto all = runner.RunSuite("", kButler128Results, 60000);
    EXPECT_EQ(all.size(), kButler128Results) << "last screen:\n" << runner.Screen();
    for (const auto& [key, result] : all)
    {
        auto failure = Early128KFailures().find(key.first);
        if (key.second == "Contended" && failure != Early128KFailures().end())
        {
            EXPECT_FALSE(result.pass) << "test " << key.first;
            EXPECT_EQ(result.r, failure->second.r) << "test " << key.first;
            EXPECT_EQ(result.loop, failure->second.loop) << "test " << key.first;
            EXPECT_EQ(result.sp, failure->second.sp) << "test " << key.first;
        }
        else
        {
            EXPECT_TRUE(result.pass) << "test " << key.first << " " << key.second << ": R=" << result.r
                                     << " loop=" << result.loop << " sp=" << result.sp;
        }
    }
}

/// region <Patrik Rak's Timing Test v0.3>

/// Timing Test v0.3 (Patrik Rak, after Jan Bobrowski's zxtests; GPL; testdata/contention/rak-timing-test):
/// a BASIC program that times one instruction group at 160 consecutive T-states (20 rows of 8, each row
/// labelled with its first T-state) and prints the durations. The reference grids below are transcribed
/// from the published result screens (zxe.io depot, "Timing Test v0.3 ... - 48K Early Timings / 128K Early
/// Timings / +3", linked from the ZX Spectrum tests wiki). Emulated 48K and 128K have early timings
namespace
{
/// Durations of one row of 8 consecutive T-states, repeated `count` times
struct RakRows
{
    const char* values;
    int count;
};

struct RakCase
{
    const char* model;       // Boot model: "48K", "128k", "PLUS3"
    int test;                // 0 contended NOP ... 8 128k page RET
    std::vector<RakRows> reference;
    const char* knownDiff;   // nullptr: must match; otherwise why it differs today (and must still differ)
};

const char* kAll4 = "4 4 4 4 4 4 4 4";

std::vector<std::string> Expand(const std::vector<RakRows>& rows)
{
    std::vector<std::string> out;
    for (const RakRows& r : rows)
        for (int i = 0; i < r.count; i++)
            out.push_back(r.values);
    return out;
}

const std::vector<RakCase>& RakCases()
{
    static const std::vector<RakCase> cases = {
        // 48K, early timings (rows from 14328)
        { "48K", 0, { { kAll4, 1 }, { "10 9 8 7 6 5 4 4", 16 }, { kAll4, 3 } }, nullptr },
        { "48K", 2, { { "4 4 4 4 4 4 4 10", 1 }, { "9 8 7 6 5 4 4 10", 15 }, { "9 8 7 6 5 4 4 4", 1 }, { kAll4, 3 } }, nullptr },
        { "48K", 3, { { kAll4, 20 } }, nullptr },
        { "48K", 4, { { "4 4 4 4 4 4 4 10", 1 }, { "10 9 8 7 6 5 4 10", 15 }, { "10 9 8 7 6 5 4 4", 1 }, { kAll4, 3 } },
          nullptr },
        { "48K", 5, { { "4 4 4 4 4 10 10 16", 1 }, { "16 15 14 13 12 11 10 16", 14 }, { "16 15 14 13 12 11 10 10", 1 },
                      { "10 9 8 7 6 5 4 4", 1 }, { kAll4, 3 } },
          nullptr },
        { "48K", 6, { { "4 4 4 4 4 4 4 10", 1 }, { "9 8 7 6 5 4 4 10", 15 }, { "9 8 7 6 5 4 4 4", 1 }, { kAll4, 3 } }, nullptr },
        { "48K", 7, { { kAll4, 20 } }, nullptr },
        // 128K, early timings (rows from 14336)
        { "128k", 0, { { kAll4, 3 }, { "4 4 10 9 8 7 6 5", 16 }, { kAll4, 1 } }, nullptr },
        { "128k", 2, { { kAll4, 3 }, { "4 10 9 8 7 6 5 4", 16 }, { kAll4, 1 } }, nullptr },
        { "128k", 4, { { kAll4, 3 }, { "4 10 10 9 8 7 6 5", 16 }, { kAll4, 1 } }, nullptr },
        { "128k", 5, { { kAll4, 2 }, { "4 4 4 4 4 4 4 10", 1 }, { "10 16 16 15 14 13 12 11", 15 }, { "10 10 10 9 8 7 6 5", 1 },
                       { kAll4, 1 } },
          nullptr },
        { "128k", 8, { { "0 0 0 0 0 0 0 0", 20 } }, nullptr },
        // +3 (rows from 14336)
        { "PLUS3", 0, { { kAll4, 3 }, { "4 4 5 4 11 10 9 8", 1 }, { "7 6 5 4 11 10 9 8", 15 }, { "7 6 5 4 4 4 4 4", 1 } },
          nullptr },
        { "PLUS3", 4, { { kAll4, 20 } }, nullptr },
        { "PLUS3", 8, { { "0 0 0 0 0 0 0 0", 20 } }, nullptr },
    };
    return cases;
}

std::string RakCaseName(const ::testing::TestParamInfo<RakCase>& info)
{
    return std::string(info.param.model) + "Test" + std::to_string(info.param.test);
}
}  // namespace

class RakTimingTest_Test : public RomEditorFixture
{
protected:
    /// Boot the model's 48 BASIC, LOAD the tape, pick `test`, wait for its grid; the 20 rows' durations
    std::vector<std::string> RunRak(const std::string& model, int test)
    {
        std::vector<std::string> rows;
        if (model == "48K")
            Boot("48K", RM_SOS, "1982 Sinclair");
        else if (model == "128k")
            Boot("128k", RM_SOS, "1982 Sinclair");
        else
            Boot("PLUS3", RM_SOS, "1982 Amstrad");
        if (HasFatalFailure())
            return rows;

        _context->pFeatureManager->setFeature(Features::kFastTape, true);
        _context->coreState.tapeFilePath = TestPathHelper::GetTestDataPath("contention/rak-timing-test/timing.tap");
        CommandTyper* typer = _context->pDebugManager->GetCommandTyper();
        EXPECT_TRUE(typer->Request("LOAD \"\"", CommandTyper::Options{}));
        RunUntil([&] { return typer->GetStatus() == CommandTyper::Status::Done; }, 4000);
        EXPECT_TRUE(RunUntil([&] { return Screen().find("Choose test") != std::string::npos; }, 3000)) << Screen();

        auto type = [&](ZXKeysEnum key) {
            _context->pKeyboard->PressKey(key);
            RunFrames(6);
            _context->pKeyboard->ReleaseKey(key);
            RunFrames(6);
        };
        type(static_cast<ZXKeysEnum>(ZXKEY_0 + test));
        type(ZXKEY_ENTER);
        EXPECT_TRUE(RunUntil([&] { return Screen().find("Press any key") != std::string::npos; }, 20000)) << Screen();

        // "14328     4  4 ..." -> "4 4 ..."
        static const std::regex row(R"(^\s*1\d{4}\s+(.*\S)\s*$)");
        std::istringstream lines(Screen());
        std::string line;
        while (std::getline(lines, line))
        {
            std::smatch m;
            if (std::regex_match(line, m, row))
            {
                std::istringstream values(m[1].str());
                std::string value;
                std::string normalized;
                while (values >> value)
                    normalized += (normalized.empty() ? "" : " ") + value;
                rows.push_back(normalized);
            }
        }
        return rows;
    }
};

/// Contended NOP on the 48K against the published early-timing screen: the opcode fetch waits the ULA
/// pattern from the same T-state as the hardware. Boots the ROM and loads the tape (~1 s): the one
/// end-to-end check of M1 contention against a hardware reference in the default run
TEST_F(RakTimingTest_Test, ContendedNop48KMatchesTheHardware)
{
    const RakCase& c = RakCases().front();
    ASSERT_EQ(c.test, 0);
    const std::vector<std::string> rows = RunRak(c.model, c.test);
    EXPECT_EQ(rows, Expand(c.reference)) << Screen();
}

/// The whole reference matrix (~1 s per case): opt-in with UNREAL_TIMING_SUITES=1. Cases that differ today
/// must still differ - a phase that fixes one fails here and moves it to the matching list
class RakTimingTestSweep_Test : public RakTimingTest_Test, public ::testing::WithParamInterface<RakCase>
{
};

TEST_P(RakTimingTestSweep_Test, MatchesThePublishedScreen)
{
    const RakCase& c = GetParam();
    const std::vector<std::string> rows = RunRak(c.model, c.test);
    ASSERT_EQ(rows.size(), 20u) << Screen();
    const std::vector<std::string> reference = Expand(c.reference);
    if (c.knownDiff == nullptr)
    {
        EXPECT_EQ(rows, reference) << Screen();
    }
    else
    {
        EXPECT_NE(rows, reference) << "now matches the hardware - move it to the matching cases (" << c.knownDiff << ")";
        std::cout << "[Rak " << c.model << " test " << c.test << "] known difference: " << c.knownDiff << std::endl;
    }
}

INSTANTIATE_TEST_SUITE_P(Opt, RakTimingTestSweep_Test,
                         ::testing::ValuesIn(std::getenv("UNREAL_TIMING_SUITES") ? RakCases() : std::vector<RakCase>{}),
                         RakCaseName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(RakTimingTestSweep_Test);

/// endregion </Patrik Rak's Timing Test v0.3>

/// region <HALT2INT v3 (Mark Woodmass)>

/// HALT2INT v3 (testdata/contention/halt2int-v3, GPL v2): when the Z80 accepts the frame interrupt after a HALT, read
/// as the R its handler sees, for a HALT at the start and the end of each 16K bank and five interrupt points (the
/// top border, the first contended T of an early and a late 48K, line 2, the last contended run); the first column
/// does the same for the floating bus. It covers what ctprobe's engine cannot: the contended HALT fetches (M1-10)
/// and the interrupt acknowledge (D-05). Expected: the published screen for an early 48K, the same values as the
/// photo of a real early 48K in the release zip
namespace
{
const std::vector<std::string>& Halt2Int48KEarly()
{
    static const std::vector<std::string> lines = {
        "Float: Early    HALT: Early",
        "16384/12000:0C  32767/12000:0C", "32768/12000:0C  49151/12000:0C", "49152/12000:0C  65535/12000:0C",
        "16384/14335:44  32767/14335:43", "32768/14335:45  49151/14335:45", "49152/14335:45  65535/14335:45",
        "16384/14336:44  32767/14336:43", "32768/14336:44  49151/14336:44", "49152/14336:44  65535/14336:44",
        "16384/14562:1C  32767/14562:0B", "32768/14562:0C  49151/14562:0C", "49152/14562:0C  65535/14562:0C",
        "16384/57239:5D  32767/57239:5D", "32768/57239:5F  49151/57239:5F", "49152/57239:5F  65535/57239:5F",
    };
    return lines;
}

}  // namespace

class Halt2Int_Test : public RomEditorFixture
{
protected:
    /// Boots `model`'s 48 BASIC, loads `tape` and runs it to "0 OK"; the result lines without their padding (the
    /// blank rows between the groups dropped)
    std::vector<std::string> RunHalt2Int(const char* model, const char* tape)
    {
        std::vector<std::string> shown;
        Boot(model, RM_SOS, "1982 Sinclair");
        if (HasFatalFailure())
            return shown;
        _context->pFeatureManager->setFeature(Features::kFastTape, true);
        _context->coreState.tapeFilePath = TestPathHelper::GetTestDataPath(std::string("contention/halt2int-v3/") + tape);
        CommandTyper* typer = _context->pDebugManager->GetCommandTyper();
        EXPECT_TRUE(typer->Request("LOAD \"\"", CommandTyper::Options{}));
        RunUntil([&] { return typer->GetStatus() == CommandTyper::Status::Done; }, 4000);
        EXPECT_TRUE(RunUntil([&] { return Screen().find("0 OK") != std::string::npos; }, 3000)) << Screen();
        std::istringstream rows(Screen());
        std::string row;
        while (std::getline(rows, row))
        {
            const size_t end = row.find_last_not_of(' ');
            if (end == std::string::npos)
                continue;
            row.resize(end + 1);
            if (row.find(':') != std::string::npos && row.find("OK") == std::string::npos)
                shown.push_back(row);
        }
        return shown;
    }
};

/// The 48K program against the published early screen (~1 s: the ROM boot and the tape). Every line as on the
/// hardware (the HALT column since the halted CPU fetches the byte after the HALT, 2026-10-02)
TEST_F(Halt2Int_Test, EarlyMatchesTheHardware)
{
    EXPECT_EQ(RunHalt2Int("48K", "halt2int.tap"), Halt2Int48KEarly()) << Screen();
}

/// The 128K program (no published screen): it compares its own measurements with an early and a late 128K and
/// says which one it found, for the floating bus and for the HALT. unreal-ng's 128K has the early timings
TEST_F(Halt2Int_Test, Early128KIsRecognized)
{
    const std::vector<std::string> shown = RunHalt2Int("128k", "halt2int128.tap");
    ASSERT_FALSE(shown.empty()) << Screen();
    EXPECT_EQ(shown.front(), "Float: Early    HALT: Early") << Screen();
    if (std::getenv("UNREAL_HALT2INT_PRINT"))
        std::printf("%s\n", Screen().c_str());
}

/// endregion </HALT2INT v3 (Mark Woodmass)>
