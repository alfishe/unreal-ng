// The z80test program itself (Patrik Rak, z80full.sna) run in the emulator, its screen read through
// ScreenOCR. Unlike Z80TestVerification (the vectors driven straight through Z80Step on a Pentagon),
// this runs the whole program on the machine: real port reads, ULA contention, interrupts.
//
// A run is ~16,500 frames (about 5 s per model in turbo), so the tests are DISABLED_ and run on demand:
//   cmake-build-agent-release/bin/core-tests --gtest_also_run_disabled_tests --gtest_filter='*Z80TestProgram_Test*'
// Recipe: docs/inprogress/2026-10-08-z80test-in-ear/README.md

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/emulator.h"

#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{

struct ProgramResult
{
    bool finished = false;
    int frames = 0;
    std::string resultLine;              // "Result: ..." as printed by the program
    std::vector<std::string> failures;   // every screen line with "FAILED", the CRC line after it
    std::string lastScreen;
};

std::string Trim(const std::string& s)
{
    size_t b = s.find_first_not_of(' ');
    size_t e = s.find_last_not_of(' ');
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

/// Load the snapshot on the model and run it until the program prints its "Result:" line.
/// The screen scrolls, so it is read every few frames and each FAILED line kept once.
ProgramResult RunProgram(const std::string& model, const std::string& snapshot, int maxFrames)
{
    ProgramResult result;
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
    if (!emulator)
        return result;
    emulator->EnableTurboMode(false);

    if (!emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(snapshot)))
    {
        EmulatorTestHelper::CleanupEmulator(emulator);
        return result;
    }

    std::set<std::string> seen;
    constexpr int kPollFrames = 5;  // a test line takes longer than this to scroll away
    constexpr uint16_t kScrCt = 23692;  // SCR_CT: the ROM asks "scroll?" when it counts down to 1
    for (int frame = 0; frame < maxFrames; frame += kPollFrames)
    {
        emulator->GetMemory()->DirectWriteToZ80Memory(kScrCt, 0xFF);
        emulator->RunNFrames(kPollFrames);
        result.frames = frame + kPollFrames;

        std::string screen = ScreenOCR::ocrScreen(emulator->GetId());
        std::istringstream lines(screen);
        std::vector<std::string> rows;
        for (std::string line; std::getline(lines, line);)
            rows.push_back(Trim(line));

        for (size_t i = 0; i < rows.size(); ++i)
        {
            // A '?' is a cell OCR could not match: a line caught mid-print, read again on the next poll
            if (rows[i].find("FAILED") == std::string::npos || rows[i].find('?') != std::string::npos)
                continue;
            std::string entry = rows[i];
            if (i + 1 < rows.size() && rows[i + 1].find("CRC") != std::string::npos)
                entry += " | " + rows[i + 1];
            if (seen.insert(entry).second)
                result.failures.push_back(entry);
        }

        for (const std::string& row : rows)
        {
            if (row.rfind("Result:", 0) == 0)
            {
                result.resultLine = row;
                result.finished = true;
            }
        }
        result.lastScreen = screen;
        if (result.finished)
            break;
    }

    EmulatorTestHelper::CleanupEmulator(emulator);
    return result;
}

void RunAndReport(const std::string& model)
{
    // z80full: ~16,500 frames on a 48K (5 s in turbo)
    constexpr int kMaxFrames = 50 * 60 * 40;
    ProgramResult r = RunProgram(model, "loaders/sna/z80full.sna", kMaxFrames);

    std::cout << "[z80full@" << model << "] frames=" << r.frames << " " << r.resultLine << "\n";
    for (const std::string& f : r.failures)
        std::cout << "  " << f << "\n";

    ASSERT_TRUE(r.finished) << "no Result: line after " << r.frames << " frames; last screen:\n" << r.lastScreen;
    EXPECT_TRUE(r.failures.empty()) << r.resultLine << "\nlast screen:\n" << r.lastScreen;
    EXPECT_NE(r.resultLine.find("all tests passed"), std::string::npos) << r.resultLine;
}

}  // namespace

class Z80TestProgram_Test : public ::testing::TestWithParam<std::string>
{
};

TEST_P(Z80TestProgram_Test, DISABLED_Z80Full)
{
    RunAndReport(GetParam());
}

// Every model whose #FE read is the standard Spectrum one. Left out, the board differs (IN tests fail by design):
//   SCORPION, PROFSCORP - EAR pulled up with no tape (the ProfROM's tape check), bit 6 = 1
//   ATM3                - bit 5 of #FE reads 0 (zports.v)
//   ATM450              - bit 7 of #FE is the PAL marker, a function of the T-state since INT
//   SPRINTER            - refuses snapshots
INSTANTIATE_TEST_SUITE_P(Models, Z80TestProgram_Test,
                         ::testing::Values("48K", "128k", "PLUS2", "PLUS2A", "PLUS3", "PENTAGON", "ATM710", "PROFI",
                                           "PROFI3", "TSL"),
                         [](const ::testing::TestParamInfo<std::string>& info) { return info.param; });
