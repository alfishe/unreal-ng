// PcHistory (pchistory.h; debugger additions tdd §7): the instruction addresses with their window's page, newest
// first, recorded only while armed (the step-work bit), read at a coherent moment; the snapshot's pchist field.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "debugger/debugmanager.h"
#include "debugger/pchistory/pchistory.h"
#include "debugger/ports/portwrite.h"
#include "debugger/snapshot/debugsnapshot.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

class PcHistory_Test : public ::testing::Test
{
protected:
    void Create(const char* model)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("pchistory-test", model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
        _context = _emulator->GetContext();
        _history = _context->pDebugManager->GetPcHistory();
        ASSERT_NE(_history, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    /// "DI; NOP; NOP; JP origin" at origin, PC there
    void Loop(uint16_t origin)
    {
        const uint8_t code[] = {0xF3, 0x00, 0x00, 0xC3, static_cast<uint8_t>(origin), static_cast<uint8_t>(origin >> 8)};
        for (size_t i = 0; i < sizeof(code); ++i)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(origin + i), code[i]);
        _context->pCore->GetZ80()->pc = origin;
    }
    void Steps(int count)
    {
        for (int i = 0; i < count; ++i)
            _emulator->RunSingleCPUCycle(true);
    }

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PcHistory* _history = nullptr;
};

TEST_F(PcHistory_Test, OffUntilArmedThenNewestFirst)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K"));
    Loop(0x8000);
    EXPECT_FALSE(_history->IsArmed());
    EXPECT_EQ(_context->stepWork.load() & EmulatorContext::kStepWorkPcHistory, 0u) << "costs nothing while off";
    Steps(3);
    EXPECT_EQ(_history->Total(), 0u);

    _history->Arm(true);
    EXPECT_NE(_context->stepWork.load() & EmulatorContext::kStepWorkPcHistory, 0u);
    Steps(5);  // the 3 steps before ran DI, NOP, NOP: now JP 8003, DI 8000, NOP 8001, NOP 8002, JP 8003
    const auto entries = _history->Newest(10);
    ASSERT_EQ(entries.size(), 5u);
    EXPECT_EQ(entries[0].address, 0x8003) << "newest first";
    EXPECT_EQ(entries[1].address, 0x8002);
    EXPECT_EQ(entries[2].address, 0x8001);
    EXPECT_EQ(entries[3].address, 0x8000);
    EXPECT_EQ(entries[4].address, 0x8003);
    EXPECT_EQ(entries[0].kind, BANK_RAM);

    _history->Arm(false);
    Steps(2);
    EXPECT_EQ(_history->Total(), 5u) << "disarmed: no more entries, the old ones stay";
}

TEST_F(PcHistory_Test, RingKeepsTheNewest)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K"));
    Loop(0x8000);
    _history->Arm(true);
    Steps(static_cast<int>(PcHistory::kCapacity) + 3);
    EXPECT_EQ(_history->Total(), PcHistory::kCapacity + 3);
    EXPECT_EQ(_history->Newest(5000).size(), PcHistory::kCapacity);
}

TEST_F(PcHistory_Test, TsConfPageOfTheWindow)
{
    ASSERT_NO_FATAL_FAILURE(Create("TSL"));
    ASSERT_TRUE(PortWrite::Write(_emulator.get(), 0x13AF, 0x20, "test").ok);  // RAM page #20 into window 3
    Loop(0xC000);
    _history->Arm(true);
    Steps(1);
    const auto entries = _history->Newest(1);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].address, 0xC000);
    EXPECT_EQ(entries[0].kind, BANK_RAM);
    EXPECT_EQ(entries[0].page, 0x20);
}

TEST_F(PcHistory_Test, ReportArmsAndTheSnapshotCarriesIt)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K"));
    Loop(0x8000);
    const PcHistory::Result first = PcHistory::Report(_emulator.get(), 8);
    ASSERT_TRUE(first.error.empty()) << first.error;
    EXPECT_TRUE(first.report.find("started_now")->b) << "the first read starts the history";
    EXPECT_TRUE(first.report.find("entries")->items.empty());
    Steps(2);

    DebugSnapshot::Options options;
    options.pchist = 4;
    const DebugSnapshot::Result snap = DebugSnapshot::Build(_emulator.get(), options);
    ASSERT_TRUE(snap.error.empty()) << snap.error;
    const StateNode* pchist = snap.snapshot.find("pchist");
    ASSERT_NE(pchist, nullptr);
    EXPECT_FALSE(pchist->find("started_now")->b);
    ASSERT_EQ(pchist->find("entries")->items.size(), 2u);
    EXPECT_EQ(pchist->find("entries")->items[0].find("address")->i, 0x8001);
    EXPECT_EQ(pchist->find("entries")->items[0].find("kind")->s, "ram");

    options.pchist = 5000;
    EXPECT_NE(DebugSnapshot::Validate(options).find("pchist"), std::string::npos);
}
