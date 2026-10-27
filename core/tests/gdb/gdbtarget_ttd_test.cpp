/// @file gdbtarget_ttd_test.cpp
/// @brief GDB register and memory writes are tool edits (Emulator::EditMemoryFromTool): while a TTD session records,
/// a seek into the edit's frame shows the machine as the user left it, not a replay without the edit.
///
/// Boots a machine and replays a few frames: slower than the 50 ms guideline, one acceptance check.

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "gdbtarget_z80.h"

namespace
{
struct SeenState
{
    ttd::TTDCpuState cpu;
    std::vector<uint8_t> ram;
};

SeenState Capture(EmulatorContext* context, const ttd::TimeTravelController& ttd)
{
    SeenState s;
    s.cpu = ttd::CaptureCpuState(*context->pCore->GetZ80());
    const uint8_t* ram = context->pMemory->RAMPageAddress(0);
    s.ram.assign(ram, ram + size_t(ttd.GetModelRamPages()) * 0x4000);
    return s;
}
}  // namespace

class GDBTargetZ80_TTD_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        // DI; LD HL,#C000; loop: LD A,#7F; IN A,(#FE); LD (HL),A; INC L; JR loop - its stores follow HL
        const uint8_t program[] = {0xF3, 0x21, 0x00, 0xC0, 0x3E, 0x7F, 0xDB, 0xFE, 0x77, 0x2C, 0x18, 0xF8};
        for (size_t i = 0; i < sizeof(program); ++i)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        _context->pCore->GetZ80()->pc = 0x8000;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

TEST_F(GDBTargetZ80_TTD_Test, RegisterAndMemoryWritesAreRecordedEdits)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    _emulator->RunTStates(20000, /*skipBreakpoints=*/true);

    // 'P': HL (regnum 3, little-endian) moves the program's stores; 'G': all CPU registers, DE changed;
    // 'M': a Z80 address and a physical RAM page (0x01PPAAAA)
    ASSERT_EQ(GDBTargetZ80::writeRegister(_context, 3, "23c1"), "OK");
    std::string all = GDBTargetZ80::serializeRegisters(_context);
    all.replace(8, 4, "3412");   // DE is the third 16-bit register
    ASSERT_TRUE(GDBTargetZ80::deserializeRegisters(_context, all));
    ASSERT_TRUE(GDBTargetZ80::writeMemory(_context, 0x8100, {0x5A, 0x5B}));
    ASSERT_TRUE(GDBTargetZ80::writeMemory(_context, 0x01040010, {0xA5}));
    ASSERT_EQ(_context->pCore->GetZ80()->hl, 0xC123);
    ASSERT_EQ(_context->pCore->GetZ80()->de, 0x1234);
    EXPECT_TRUE(_ttd->IsRecording());

    _emulator->RunTStates(10000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint afterEdit = _ttd->CurrentPosition();
    const SeenState live = Capture(_context, *_ttd);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->SeekTo({_ttd->GetCheckpoint(1)->time.frame, 0}, nullptr));
    ASSERT_TRUE(_ttd->SeekTo(afterEdit, nullptr));
    const SeenState seen = Capture(_context, *_ttd);
    EXPECT_EQ(seen.cpu.hl, live.cpu.hl);
    EXPECT_EQ(seen.cpu.de, live.cpu.de);
    EXPECT_EQ(seen.cpu.pc, live.cpu.pc);
    EXPECT_TRUE(seen.ram == live.ram) << "RAM inside the edit's frame";
}
