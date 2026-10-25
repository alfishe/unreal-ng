/// @file coverageexport_test.cpp
/// @brief The coverage as SkoolKit's code execution map (coverageexport.h): the "$HHHH" lines of the recorded
/// addresses, the window, the options and their refusals, the file written and merged with an existing one, and, on
/// a real CPU, that the map holds instruction starts only (prefixed instructions counted once, at their first byte).

#include "pch.h"

#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/coverage/coverageanalyzer.h"
#include "debugger/analyzers/coverage/coverageexport.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
std::string ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

class CoverageExport_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _manager = new AnalyzerManager(_context);
        auto analyzer = std::make_unique<CoverageAnalyzer>();
        _coverage = analyzer.get();
        _manager->registerAnalyzer("coverage", std::move(analyzer));
        ASSERT_TRUE(_manager->activate("coverage"));
        for (const uint16_t pc : {0x8003, 0x0038, 0x8000, 0xFFFF, 0x8000})
            _manager->dispatchCPUStep(nullptr, pc);
    }

    void TearDown() override
    {
        delete _manager;
        delete _context;
    }

    EmulatorContext* _context = nullptr;
    AnalyzerManager* _manager = nullptr;
    CoverageAnalyzer* _coverage = nullptr;
};

TEST_F(CoverageExport_Test, MapLinesAscendingInTheWindow)
{
    coverageexport::Result r = coverageexport::Export(_coverage, {});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.format, "skoolkit-map");
    EXPECT_EQ(r.count, 4u);
    EXPECT_EQ(r.text, "$0038\n$8000\n$8003\n$FFFF\n");
    EXPECT_TRUE(r.path.empty());

    r = coverageexport::Export(_coverage, {{"format", "skoolkit-map"}, {"start", "0x4000"}, {"end", "$8000"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.text, "$8000\n");
    r = coverageexport::Export(_coverage, {{"start", "32769"}});
    EXPECT_EQ(r.text, "$8003\n$FFFF\n");
}

TEST_F(CoverageExport_Test, RefusesWhatItCannotDo)
{
    EXPECT_EQ(coverageexport::Export(nullptr, {}).error, coverageexport::Error::NotAvailable);
    EXPECT_EQ(coverageexport::Export(_coverage, {{"format", "fuse"}}).error, coverageexport::Error::BadRequest);
    EXPECT_EQ(coverageexport::Export(_coverage, {{"start", "0x10000"}}).error, coverageexport::Error::BadRequest);
    EXPECT_EQ(coverageexport::Export(_coverage, {{"start", "0x9000"}, {"end", "0x8000"}}).error, coverageexport::Error::BadRequest);
    EXPECT_EQ(coverageexport::Export(_coverage, {{"merge", "true"}}).error, coverageexport::Error::BadRequest);
    EXPECT_EQ(coverageexport::Export(_coverage, {{"limit", "3"}}).error, coverageexport::Error::BadRequest);
}

TEST_F(CoverageExport_Test, WritesAndMergesTheFile)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("coverage-export/cov.map");
    coverageexport::Result r = coverageexport::Export(_coverage, {{"path", path}, {"start", "0x8000"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.count, 3u);
    EXPECT_EQ(ReadFile(path), "$8000\n$8003\n$FFFF\n");

    // Another run's map (zxrun.py writes the same lines; a Fuse profile line "0x6000,12" is read too), merged in
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << "$6000\n0x6001,12\n\n$8003\n";
    }
    r = coverageexport::Export(_coverage, {{"path", path}, {"merge", "true"}, {"start", "0x8000"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.merged, 3u);
    EXPECT_EQ(r.count, 5u);
    EXPECT_EQ(ReadFile(path), "$6000\n$6001\n$8000\n$8003\n$FFFF\n");
}

/// On the CPU: LD IX,nn (DD), SLA A (CB), BIT 0,(IY+1) (FD CB), NEG (ED), JR $ - the map is the five starts
TEST(CoverageExportCpu_Test, InstructionStartsOnly)
{
    Emulator* emulator = EmulatorTestHelper::CreateDebugEmulator({"debugmode"});
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    AnalyzerManager* manager = context->pDebugManager->GetAnalyzerManager();
    CoverageAnalyzer* coverage = manager->getAnalyzer<CoverageAnalyzer>("coverage");
    ASSERT_NE(coverage, nullptr);

    const uint8_t code[] = {0xDD, 0x21, 0x34, 0x12, 0xCB, 0x27, 0xFD, 0xCB, 0x01, 0x46, 0xED, 0x44, 0x18, 0xFE};
    Memory* memory = emulator->GetMemory();
    for (size_t i = 0; i < sizeof(code); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code[i]);
    Z80* z80 = context->pCore->GetZ80();
    z80->pc = 0x8000;
    z80->iff1 = 0;
    z80->iff2 = 0;

    coverage->clear();
    ASSERT_TRUE(manager->activate("coverage"));
    emulator->RunNCPUCycles(8, true);
    manager->deactivate("coverage");

    const coverageexport::Result r = coverageexport::Export(coverage, {});
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(r.text, "$8000\n$8004\n$8006\n$800A\n$800C\n");
    EmulatorTestHelper::CleanupEmulator(emulator);
}
