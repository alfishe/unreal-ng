/// @file machinetaps_test.cpp
/// @brief Script callback taps on the main CPU's machine events and on sound chip registers (platform-debugging,
/// docs/features/script-callbacks.md): a tap hears them out of the debug mode, never stops, and carries the detail

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/breakpoints/chipregister.h"
#include "debugger/callbacks/scriptcallbacks.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

class MachineTaps_Test : public ::testing::Test
{
protected:
    SoundCardScope _sound{TestSound::TurboSound};  // the AY of the chip register tap
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ScriptCallbackManager* _callbacks = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _callbacks = _context->pDebugManager->GetScriptCallbacks();
        ASSERT_NE(_callbacks, nullptr);
    }
    void TearDown() override
    {
        if (_callbacks)
            _callbacks->RemoveAll();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    uint32_t Tap(const BreakpointSpec& tap, DebugCpuId cpu, std::string& error)
    {
        CallbackSpec spec;
        spec.trigger = CallbackTrigger::Tap;
        spec.tap = tap;
        spec.cpu = cpu;
        spec.owner = "test";
        return _callbacks->Register(spec, error);
    }
    uint32_t ChipTap(uint16_t reg, int value = -1)
    {
        BreakpointSpec tap;
        tap.type = BRK_CHIPREG;
        tap.access = 0;
        tap.chip = static_cast<uint8_t>(SoundChipKind::Ay);
        tap.address = reg;
        if (value >= 0)
        {
            tap.nrValue = static_cast<uint8_t>(value);
            tap.nrMask = 0xFF;
        }
        std::string error;
        const uint32_t id = Tap(tap, DebugCpuId::Main, error);
        EXPECT_NE(id, 0u) << error;
        return id;
    }
};

// TR-DOS boots out of the debug mode: the tap hears its Read Sector with the track and the sector, the machine runs on
TEST_F(MachineTaps_Test, AnFdcCommandTapHearsTheBootOutOfTheDebugMode)
{
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd"), 0));
    _context->config.reset_rom = RM_DOS;
    _emulator->Reset();
    _emulator->EnableTurboMode();

    BreakpointSpec tap;
    tap.type = BRK_EVENT;
    tap.access = 0;
    tap.address = static_cast<uint16_t>(GSCardEvent::FdcCommand);
    tap.nrValue = 0x80;
    tap.nrMask = 0xE0;
    std::string error;
    const uint32_t id = Tap(tap, DebugCpuId::Main, error);
    ASSERT_NE(id, 0u) << error;
    EXPECT_FALSE(_context->pCore->GetZ80()->isDebugMode) << "a tap does not need the debug mode";

    std::vector<CallbackHit> hits;
    for (int frame = 0; frame < 300 && hits.empty(); frame++)
    {
        _emulator->RunNFrames(1, false);
        _callbacks->Drain(id, hits);
    }
    ASSERT_FALSE(hits.empty()) << "TR-DOS never read a sector";
    EXPECT_FALSE(_emulator->IsPaused()) << "a tap never stops";
    const CallbackHit& hit = hits.front();
    EXPECT_EQ(hit.kind, BRK_TAP_MACHINE_EVENT);
    EXPECT_STREQ(CallbackHitKindName(hit.kind), "machine_event");
    EXPECT_EQ(hit.address, static_cast<uint16_t>(GSCardEvent::FdcCommand));
    EXPECT_EQ(hit.value & 0xE0, 0x80) << "Read Sector";
    EXPECT_GE(hit.arg, 0) << "track << 8 | sector";
    EXPECT_EQ(hit.cpu, static_cast<uint8_t>(DebugCpuId::Main));
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(id, info));
    EXPECT_EQ(info.event, "fdc_command");
    EXPECT_EQ(info.target, "tap event fdc_command");
}

// OUT #FFFD selects AY register 7, OUT #BFFD writes #38: the tap on register 7 hears it, the one on 8 does not
TEST_F(MachineTaps_Test, AChipRegisterTapHearsTheWrite)
{
    const uint8_t program[] = {
        0xF3,              // DI
        0x01, 0xFD, 0xFF,  // LD BC,#FFFD
        0x3E, 0x07,        // LD A,7
        0xED, 0x79,        // OUT (C),A
        0x06, 0xBF,        // LD B,#BF
        0x3E, 0x38,        // LD A,#38
        0xED, 0x79,        // OUT (C),A
        0x18, 0xFE,        // JR $
    };
    for (size_t i = 0; i < sizeof(program); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;

    const uint32_t mixer = ChipTap(7);
    const uint32_t volume = ChipTap(8);
    const uint32_t other = ChipTap(7, 0x3F);
    EXPECT_FALSE(_context->pCore->GetZ80()->isDebugMode);
    _emulator->RunNFrames(1, false);

    std::vector<CallbackHit> hits;
    _callbacks->Drain(mixer, hits);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].kind, BRK_TAP_CHIPREG);
    EXPECT_STREQ(CallbackHitKindName(hits[0].kind), "chip_register");
    EXPECT_EQ(hits[0].address, 7);
    EXPECT_EQ(hits[0].value, 0x38);
    EXPECT_EQ(hits[0].arg, 0) << "the first chip";
    hits.clear();
    _callbacks->Drain(volume, hits);
    _callbacks->Drain(other, hits);
    EXPECT_TRUE(hits.empty()) << "another register, another byte";
    CallbackInfo info;
    ASSERT_TRUE(_callbacks->Get(mixer, info));
    EXPECT_EQ(info.event, "ay");
    EXPECT_EQ(info.target, "tap chip ay reg #0007");
}

// The machine's events are the main CPU's, the card's events the card's; chip registers only the main CPU's
TEST_F(MachineTaps_Test, EachSideTakesItsOwnEvents)
{
    BreakpointSpec cardEvent;
    cardEvent.type = BRK_EVENT;
    cardEvent.access = 0;
    cardEvent.address = static_cast<uint16_t>(GSCardEvent::Int);
    std::string error;
    EXPECT_EQ(Tap(cardEvent, DebugCpuId::Main, error), 0u);
    EXPECT_NE(error.find("machine's events"), std::string::npos) << error;

    BreakpointSpec condition;
    condition.type = BRK_EVENT;
    condition.access = 0;
    condition.address = static_cast<uint16_t>(GSCardEvent::CpuInt);
    condition.condition = "A == 1";
    EXPECT_EQ(Tap(condition, DebugCpuId::Main, error), 0u) << "an event takes no condition";
}
