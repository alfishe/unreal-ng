// platform-debugging P8: the WD1793's commands and their ends as event breakpoints on the main CPU
// (docs/inprogress/2026-10-27-platform-debugging/README.md)

#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

class FdcMachineEvents_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    BreakpointManager* _manager = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _manager = _emulator->GetBreakpointManager();
        ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd"), 0));
        _context->config.reset_rom = RM_DOS;
        _emulator->Reset();
        _emulator->EnableTurboMode();
        _emulator->DebugOn();
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint16_t AddEvent(GSCardEvent kind, int value = -1, uint8_t mask = 0xFF)
    {
        BreakpointSpec spec;
        spec.type = BRK_EVENT;
        spec.address = static_cast<uint16_t>(kind);
        if (value >= 0)
        {
            spec.nrValue = static_cast<uint8_t>(value);
            spec.nrMask = mask;
        }
        std::string error;
        const uint16_t id = _manager->AddBreakpoint(spec, error);
        EXPECT_NE(id, BRK_INVALID) << error;
        return id;
    }

    /// Runs frames until a breakpoint stops the machine (the reset boots TR-DOS, which reads the disk at once)
    bool RunUntilStop()
    {
        for (int frame = 0; frame < 300; frame++)
        {
            _emulator->RunNFrames(1, false);
            if (_emulator->LastStop().reason == Emulator::DebugStop::Reason::Breakpoint)
                return true;
        }
        return false;
    }
};

// The reset boots TR-DOS: Restore, Seek, Read Address go by; the first Read Sector stops after the CPU's OUT
TEST_F(FdcMachineEvents_Test, ATrDosBootStopsOnItsFirstReadSector)
{
    const uint16_t read = AddEvent(GSCardEvent::FdcCommand, 0x80, 0xE0);
    ASSERT_TRUE(RunUntilStop()) << "TR-DOS never read a sector";
    const Emulator::DebugStop stop = _emulator->LastStop();
    EXPECT_EQ(stop.breakpoint.breakpointId, read);
    EXPECT_EQ(stop.breakpoint.kind, BreakpointHitKind::Event);
    const WD1793* fdc = _context->pBetaDisk;
    EXPECT_EQ(fdc->getCommandRegister() & 0xE0, 0x80);
    EXPECT_EQ(fdc->getTrackRegister(), 0) << "the catalogue is on track 0";
    EXPECT_NE(fdc->getStatusRegister() & 0x01, 0) << "the command is still busy at the stop";
}

// fdc_done fires when the command ends: the read sector finished with no error bits
TEST_F(FdcMachineEvents_Test, AReadSectorsEndIsFdcDone)
{
    AddEvent(GSCardEvent::FdcCommand, 0x80, 0xE0);
    ASSERT_TRUE(RunUntilStop());
    _manager->RemoveBreakpointsByType(BRK_EVENT);
    const uint16_t done = AddEvent(GSCardEvent::FdcDone);
    ASSERT_TRUE(RunUntilStop()) << "the read never ended";
    EXPECT_EQ(_emulator->LastStop().breakpoint.breakpointId, done);
    const WD1793* fdc = _context->pBetaDisk;
    EXPECT_EQ(fdc->getCommandRegister() & 0xE0, 0x80);
    EXPECT_EQ(fdc->getStatusRegister() & 0x1D, 0) << "not busy, no CRC, no record-not-found, no lost data";
}

TEST(FdcMachineEventNames_Test, TheFdcsEventsAreTheMainCpus)
{
    for (const char* name : {"fdc_command", "fdc_done"})
    {
        GSCardEvent kind = GSCardEvent::Count;
        ASSERT_TRUE(ParseGSCardEvent(name, kind)) << name;
        EXPECT_TRUE(GSCardEventIsHostSide(kind)) << name;
        EXPECT_STREQ(GSCardEventName(kind), name);
    }
}
