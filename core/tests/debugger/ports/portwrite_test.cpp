// PortWrite (portwrite.h): a debugger's port write through the machine's decoder - the side effects of a CPU OUT,
// no breakpoint, no device waits, a tool edit for TTD (docs/inprogress/2026-10-04-debugger-additions/tdd.md §1).

#include <gtest/gtest.h>

#include "_helpers/testwaithelper.h"

#include <memory>
#include <string>

#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/ports/portwrite.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/platforms/tsconf/tsconfstate.h"
#include "emulator/ports/models/portdecoder_tsconf.h"

class PortWrite_Test : public ::testing::Test
{
protected:
    void Create(const char* model)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("portwrite-test", model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
        _context = _emulator->GetContext();
        _memory = _context->pMemory;
    }
    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
        }
    }

    /// RAM page `page` is in window 3 (#C000): its first byte, marked, shows there
    bool PageInWindow3(uint16_t page)
    {
        _memory->RAMPageAddress(page)[0] = 0xA7;
        _memory->RAMPageAddress(page)[1] = static_cast<uint8_t>(page);
        return _memory->DirectReadFromZ80Memory(0xC000) == 0xA7 &&
               _memory->DirectReadFromZ80Memory(0xC001) == static_cast<uint8_t>(page);
    }

    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    Memory* _memory = nullptr;
};

TEST_F(PortWrite_Test, ParseTakesTheDebuggerNumberForms)
{
    uint16_t port = 0;
    uint8_t value = 0;
    std::string error;
    ASSERT_TRUE(PortWrite::Parse("0x13AF", "0x20", port, value, error)) << error;
    EXPECT_EQ(port, 0x13AF);
    EXPECT_EQ(value, 0x20);
    ASSERT_TRUE(PortWrite::Parse("#7FFD", "16", port, value, error)) << error;
    EXPECT_EQ(port, 0x7FFD);
    EXPECT_EQ(value, 16);
    ASSERT_TRUE(PortWrite::Parse("FEh", "$07", port, value, error)) << error;
    EXPECT_EQ(port, 0x00FE);
    EXPECT_EQ(value, 7);

    EXPECT_FALSE(PortWrite::Parse("0x10000", "0", port, value, error));
    EXPECT_NE(error.find("bad port"), std::string::npos);
    EXPECT_FALSE(PortWrite::Parse("0x7FFD", "256", port, value, error));
    EXPECT_NE(error.find("bad value"), std::string::npos);
    EXPECT_FALSE(PortWrite::Parse("-1", "0", port, value, error)) << "no sign";
    EXPECT_FALSE(PortWrite::Parse("", "0", port, value, error));
    EXPECT_FALSE(PortWrite::Parse("7FFD", "0", port, value, error)) << "hex needs a prefix or the h suffix";
}

TEST_F(PortWrite_Test, PagingAsAfterACpuOut)
{
    ASSERT_NO_FATAL_FAILURE(Create("128K"));
    ASSERT_FALSE(PageInWindow3(3));

    const uint64_t seq = _emulator->DebugSeq();
    const PortWrite::Result result = PortWrite::Write(_emulator.get(), 0x7FFD, 0x03, "test");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.moment, "stopped") << "created, never started";
    EXPECT_TRUE(PageInWindow3(3)) << "#7FFD = 3 maps RAM page 3 at #C000";
    EXPECT_GT(_emulator->DebugSeq(), seq) << "a tool edit: debugger views redraw";
}

TEST_F(PortWrite_Test, PortBreakpointDoesNotFire)
{
    ASSERT_NO_FATAL_FAILURE(Create("128K"));
    _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    BreakpointManager& brk = *_context->pDebugManager->GetBreakpointsManager();
    const uint16_t id = brk.AddPortOutBreakpoint(0x7FFD);
    ASSERT_NE(id, BRK_INVALID);

    ASSERT_TRUE(PortWrite::Write(_emulator.get(), 0x7FFD, 0x04, "test").ok);
    EXPECT_TRUE(PageInWindow3(4));
    EXPECT_EQ(brk.GetBreakpointById(id)->hitCount, 0u) << "the user's own write is no breakpoint hit";
    EXPECT_FALSE(_emulator->IsPaused());
    EXPECT_EQ(brk.HandlePortOut(0x7FFD), id) << "precondition: the breakpoint is armed for a CPU OUT";
}

TEST_F(PortWrite_Test, TsConfPageRegisterAndNoDeviceWaits)
{
    ASSERT_NO_FATAL_FAILURE(Create("TSL"));
    auto* tsconf = dynamic_cast<PortDecoder_TSConf*>(_context->pPortDecoder);
    ASSERT_NE(tsconf, nullptr);

    // tdd §1.1: #13AF = #20 maps page #20 into window 3
    ASSERT_TRUE(PortWrite::Write(_emulator.get(), 0x13AF, 0x20, "test").ok);
    EXPECT_EQ(tsconf->GetState().regs[TsConfReg::Page3], 0x20);
    EXPECT_TRUE(PageInWindow3(0x20));

    // With SysConfig bit 1 an AY write stalls the CPU 4 clocks; a tool write takes no time
    tsconf->GetState().regs[TsConfReg::SysConfig] |= 0x02;
    Z80* z80 = _context->pCore->GetZ80();
    const uint32_t tt = z80->tt;
    ASSERT_TRUE(PortWrite::Write(_emulator.get(), 0xFFFD, 0x07, "test").ok);
    EXPECT_EQ(z80->tt, tt) << "the device wait reached the CPU's clock";
}

TEST_F(PortWrite_Test, PausedAndRunningMachines)
{
    ASSERT_NO_FATAL_FAILURE(Create("128K"));
    // A real run: the 128K ROM boots; booting is not awaited
    _emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsRunning(); }));
    const PortWrite::Result running = PortWrite::Write(_emulator.get(), 0x00FE, 0x02, "test");
    ASSERT_TRUE(running.ok) << running.error;
    EXPECT_EQ(running.moment, "frame") << "between two frames, without a pause";
    EXPECT_FALSE(_emulator->IsPaused());

    _emulator->Pause();
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsEmulationParked(); }));
    const PortWrite::Result paused = PortWrite::Write(_emulator.get(), 0x7FFD, 0x05, "test");
    ASSERT_TRUE(paused.ok) << paused.error;
    EXPECT_EQ(paused.moment, "paused");
    EXPECT_TRUE(PageInWindow3(5));
}

TEST_F(PortWrite_Test, RecordingKeepsTheWriteAsAToolEdit)
{
    ASSERT_NO_FATAL_FAILURE(Create("128K"));
    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    // The CPU spins in "DI; JR $" at #8000 so the ROM never writes #7FFD itself
    _memory->DirectWriteToZ80Memory(0x8000, 0xF3);
    _memory->DirectWriteToZ80Memory(0x8001, 0x18);
    _memory->DirectWriteToZ80Memory(0x8002, 0xFE);
    Z80* z80 = _context->pCore->GetZ80();
    z80->pc = 0x8000;
    z80->iff1 = z80->iff2 = 0;
    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(1, true);
    const size_t before = ttd->GetCheckpointCount() - 1;

    ASSERT_TRUE(PortWrite::Write(_emulator.get(), 0x7FFD, 0x06, "test").ok);
    _emulator->RunNFrames(1, true);
    const size_t after = ttd->GetCheckpointCount() - 1;
    ttd->StopRecording();

    const auto markers = ttd->GetExternalEvents().SnapshotEvents();
    ASSERT_EQ(markers.size(), 1u);
    EXPECT_EQ(markers.front().kind, ttd::TTDExternalEventKind::DebuggerEdit);
    ASSERT_TRUE(ttd->RestoreCheckpointForTesting(before));
    EXPECT_FALSE(PageInWindow3(6));
    ASSERT_TRUE(ttd->RestoreCheckpointForTesting(after));
    EXPECT_TRUE(PageInWindow3(6)) << "the checkpoint after the write lost the paging";
}
