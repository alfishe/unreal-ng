// SectionTimerAnalyzer: a section's gross and net time with interrupts landing inside it, the stop at the exit of a
// pass that meets a break condition, the text specs every plane takes

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "debugger/analyzers/sectiontimer/sectiontimeranalyzer.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
/// Work (#8001 .. #8005): LD B,64 (7 T) and a DJNZ loop (63 * 13 + 8 T) = 834 T, uncontended on every model
const std::vector<uint8_t> kLoop = {
    0xFB,        // 8000 EI
    0x06, 0x40,  // 8001 LD B,#40     Work
    0x10, 0xFE,  // 8003 DJNZ #8003
    0x00,        // 8005 NOP          Work.End
    0x18, 0xF9,  // 8006 JR #8001
};
constexpr uint64_t kWorkT = 834;
/// The IM 2 handler at #A0A0 (a table of #A0 bytes gives it for any vector): EI : RETI. With the acknowledge an
/// interrupt costs 19 + 4 + 14 T
constexpr uint16_t kIm2Handler = 0xA0A0;
constexpr uint64_t kInterruptT = 19 + 4 + 14;
}  // namespace

class SectionTimerAnalyzer_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    SectionTimerAnalyzer* _timer = nullptr;

    void TearDown() override
    {
        if (_timer)
            _timer->Stop();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Start(const char* model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr) << model;
        _context = _emulator->GetContext();
        Memory* memory = _context->pMemory;
        for (size_t i = 0; i < kLoop.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), kLoop[i]);
        const uint8_t handler[] = {0xFB, 0xED, 0x4D};
        for (size_t i = 0; i < sizeof(handler); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kIm2Handler + i), handler[i]);
        for (uint32_t a = 0x9000; a <= 0x9100; a++)  // the IM 2 table (I = #90)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(a), 0xA0);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->sp = 0xBF00;
        z80->i = 0x90;
        z80->im = 2;
        z80->iff1 = z80->iff2 = 0;
        z80->halted = 0;
        _timer = SectionTimerAnalyzer::Find(_context);
        ASSERT_NE(_timer, nullptr);
    }

    void ExpectExactPasses(const char* model)
    {
        Start(model);
        ASSERT_FALSE(HasFatalFailure());
        SectionTimerAnalyzer::Spec spec;
        spec.sections = {"Work=#8001:#8005"};
        std::string error;
        ASSERT_TRUE(_timer->Start(spec, error)) << error;
        _emulator->RunNFrames(10, /*skipBreakpoints=*/false);

        const SectionTimerAnalyzer::Result r = _timer->GetResult();
        ASSERT_EQ(r.sections.size(), 1u);
        ASSERT_GT(r.passes.size(), 100u) << "about 80 passes a frame";
        uint64_t interrupted = 0;
        for (const auto& p : r.passes)
        {
            EXPECT_EQ(p.net, kWorkT) << "frame " << p.frame << " T " << p.t;
            EXPECT_EQ(p.elapsed, kWorkT + p.interrupts * kInterruptT) << "frame " << p.frame << " T " << p.t;
            interrupted += p.interrupts ? 1 : 0;
        }
        EXPECT_GE(interrupted, 8u) << "the frame interrupts mostly land inside Work";
        EXPECT_EQ(r.stats[0].count, r.passesTotal);
        EXPECT_EQ(r.stats[0].min, kWorkT);
        EXPECT_EQ(r.stats[0].max, kWorkT + kInterruptT);
        EXPECT_EQ(r.stats[0].netMax, kWorkT);
        EXPECT_EQ(r.stats[0].interrupted, interrupted);
        EXPECT_EQ(r.orphans, 0u);
        EXPECT_EQ(r.dropped, 0u);
        EXPECT_FALSE(r.stop.hit);
    }
};

TEST_F(SectionTimerAnalyzer_Test, NetTimeIsExactAcrossInterrupts48K)
{
    ExpectExactPasses("48K");
}

TEST_F(SectionTimerAnalyzer_Test, NetTimeIsExactAcrossInterruptsPentagon)
{
    ExpectExactPasses("PENTAGON");
}

// break_if pauses at the exit of the first pass that meets it, before the exit's instruction; the next run goes on
// from there and counts that pass once
TEST_F(SectionTimerAnalyzer_Test, BreakIfStopsAtTheExitOfTheSlowPass)
{
    Start("PENTAGON");
    ASSERT_FALSE(HasFatalFailure());
    SectionTimerAnalyzer::Spec spec;
    spec.sections = {"Work=#8001:#8005"};
    spec.breakIf = {"Work>" + std::to_string(kWorkT)};
    std::string error;
    ASSERT_TRUE(_timer->Start(spec, error)) << error;
    _emulator->RunNFrames(3, /*skipBreakpoints=*/false);

    SectionTimerAnalyzer::Result r = _timer->GetResult();
    ASSERT_TRUE(r.stop.hit);
    EXPECT_EQ(r.stop.condition, "Work>834");
    EXPECT_EQ(r.stop.pass.elapsed, kWorkT + kInterruptT);
    EXPECT_EQ(r.stop.pass.net, kWorkT);
    EXPECT_EQ(_context->pCore->GetZ80()->pc, 0x8005) << "stopped before the exit's instruction";
    EXPECT_TRUE(_emulator->LastDirectStop().hit);
    ASSERT_FALSE(r.passes.empty());
    EXPECT_EQ(r.passes.back().elapsed, kWorkT + kInterruptT) << "the slow pass is the last one";
    const uint64_t before = r.passesTotal;

    // On from the stop: the exit is not told again, the next pass is a fresh one
    _emulator->RunTStates(2000, /*skipBreakpoints=*/false);
    r = _timer->GetResult();
    EXPECT_GE(r.passesTotal, before + 1);
    for (size_t i = before; i < r.passes.size(); i++)
        EXPECT_EQ(r.passes[i].elapsed, kWorkT) << "pass " << i;
    EXPECT_EQ(r.orphans, 0u);
}

// A run that skips breakpoints (run_frames) is not stopped by a break condition, and records no stop it did not make
TEST_F(SectionTimerAnalyzer_Test, BreakIfNeitherStopsNorRecordsInARunThatSkipsBreakpoints)
{
    Start("PENTAGON");
    ASSERT_FALSE(HasFatalFailure());
    SectionTimerAnalyzer::Spec spec;
    spec.sections = {"Work=#8001:#8005"};
    spec.breakIf = {"Work>" + std::to_string(kWorkT)};
    std::string error;
    ASSERT_TRUE(_timer->Start(spec, error)) << error;
    _emulator->RunNFrames(3);

    const SectionTimerAnalyzer::Result r = _timer->GetResult();
    EXPECT_GE(r.stats[0].interrupted, 2u) << "passes met the condition";
    EXPECT_FALSE(r.stop.hit);
    EXPECT_FALSE(_emulator->LastDirectStop().hit);
}

// The text specs: labels (NAME and NAME.End, or every X with an X.End), numbers, several exits, the errors
TEST_F(SectionTimerAnalyzer_Test, SpecsResolveLabelsAndNumbers)
{
    Start("48K");
    ASSERT_FALSE(HasFatalFailure());
    LabelManager* labels = _context->pDebugManager->GetLabelManager();
    labels->AddLabel("Work", 0x8001, 0xFFFF, 0xFFFF);
    labels->AddLabel("Work.End", 0x8005, 0xFFFF, 0xFFFF);
    labels->AddLabel("Loop", 0x8003, 0xFFFF, 0xFFFF);

    SectionTimerAnalyzer::Config config;
    std::string error;
    ASSERT_TRUE(_timer->Resolve(SectionTimerAnalyzer::Spec{}, config, error)) << error;
    ASSERT_EQ(config.sections.size(), 1u) << "only Work has a Work.End";
    EXPECT_EQ(config.sections[0].name, "Work");
    EXPECT_EQ(config.sections[0].entry, 0x8001);
    EXPECT_EQ(config.sections[0].exits, std::vector<uint16_t>{0x8005});

    SectionTimerAnalyzer::Spec spec;
    spec.sections = {"Work", "inner=Loop:$8005,0x8006,32775"};
    spec.breakIf = {"net:inner >= 100", "Work!=834"};
    spec.extra = {"#8000"};
    ASSERT_TRUE(_timer->Resolve(spec, config, error)) << error;
    ASSERT_EQ(config.sections.size(), 2u);
    EXPECT_EQ(config.sections[1].entry, 0x8003);
    EXPECT_EQ(config.sections[1].exits, (std::vector<uint16_t>{0x8005, 0x8006, 0x8007}));
    ASSERT_EQ(config.breakIf.size(), 2u);
    EXPECT_TRUE(config.breakIf[0].net);
    EXPECT_EQ(config.breakIf[0].section, 1);
    EXPECT_EQ(config.breakIf[0].op, SectionTimerAnalyzer::Op::GreaterEqual);
    EXPECT_EQ(config.breakIf[0].value, 100u);
    EXPECT_EQ(config.breakIf[1].op, SectionTimerAnalyzer::Op::NotEqual);
    EXPECT_EQ(config.extra, std::vector<uint16_t>{0x8000});

    for (const auto& bad : std::vector<SectionTimerAnalyzer::Spec>{
             {{"Nope"}, {}, {}, false, 0, 10},
             {{"x=#8000"}, {}, {}, false, 0, 10},
             {{"Work"}, {}, {"Other>5"}, false, 0, 10},
             {{"Work"}, {}, {"Work~5"}, false, 0, 10},
             {{"Work", "Work=#1:#2"}, {}, {}, false, 0, 10}})
    {
        EXPECT_FALSE(_timer->Resolve(bad, config, error)) << bad.sections[0];
        EXPECT_FALSE(error.empty());
    }

    // A spec that does not resolve leaves a running timer as it was
    ASSERT_TRUE(_timer->Start(spec, error)) << error;
    EXPECT_FALSE(_timer->Start(SectionTimerAnalyzer::Spec{{"Nope"}}, error));
    EXPECT_TRUE(_timer->IsActive());
    EXPECT_EQ(_timer->GetResult(0, 0).sections.size(), 2u);
    EXPECT_TRUE(_timer->Stop());
    EXPECT_FALSE(_timer->Stop());
}

// The JSON and the table every plane answers with
TEST_F(SectionTimerAnalyzer_Test, ResultFormats)
{
    Start("PENTAGON");
    ASSERT_FALSE(HasFatalFailure());
    SectionTimerAnalyzer::Spec spec;
    spec.sections = {"Work=#8001:#8005"};
    std::string error;
    ASSERT_TRUE(_timer->Start(spec, error)) << error;
    _emulator->RunNFrames(2, /*skipBreakpoints=*/false);

    const SectionTimerAnalyzer::Result r = _timer->GetResult(0, 2);
    EXPECT_EQ(r.passes.size(), 2u);
    EXPECT_GT(r.passesKept, 2u);
    const std::string json = SectionTimerAnalyzer::ToJson(r);
    EXPECT_NE(json.find("\"name\":\"Work\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"min\":834"), std::string::npos) << json;
    EXPECT_NE(json.find("[\"Work\","), std::string::npos) << json;
    EXPECT_NE(json.find("\"stopped\":null"), std::string::npos) << json;
    const std::string text = SectionTimerAnalyzer::ToText(r);
    EXPECT_NE(text.find("Work"), std::string::npos) << text;
    EXPECT_NE(text.find("#8001..#8005"), std::string::npos) << text;

    _timer->Clear();
    EXPECT_EQ(_timer->GetResult().passesTotal, 0u);
    EXPECT_TRUE(_timer->IsActive());
}
