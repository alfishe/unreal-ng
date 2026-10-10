// Every memory time travel records is in the device-memory registry (memory-spaces design, step 1,
// docs/inprogress/2026-10-08-memory-spaces): the NeoGS card's RAM and flash and the MoonSound's wave memory by their
// engine names ("neogs.ram", "neogs.flash", "moonsound.wave"), read and written
// on every automation interface like the declared regions. Listing a device's regions binds its dirty tracker again,
// which must keep the marks: a debugger edit during a recording lists them (TimeTravelController::EndToolEdit), and
// before 2026-10-08 that dropped the device memory written since the last checkpoint from the recording.
//
// Runtime: ATM710 with its sound cards (NeoGS, MoonSound); a few frames recorded.

#include <gtest/gtest.h>

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/devicememory.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"

class DeviceMemoryTtd_Test : public ::testing::Test
{
protected:
    SoundCardScope _soundCards;   ///< first: the GS slot is fitted when the machine is created
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        // ATM710 fits a NeoGS and a MoonSound (data/configs/atm710/unreal.ini) while the scope keeps sound cards
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM710", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    std::vector<uint8_t> Read(const char* name, uint32_t offset, uint32_t length)
    {
        std::vector<uint8_t> bytes;
        std::string error;
        EXPECT_TRUE(DeviceMemory::Read(_context, name, offset, length, bytes, error)) << error;
        return bytes;
    }
};

TEST_F(DeviceMemoryTtd_Test, TheSoundCardsMemoriesAreRegions)
{
    IDeviceMemoryRegion* ram = DeviceMemory::Find(_context, "neogs.ram");
    ASSERT_NE(ram, nullptr) << "the card's RAM, by the engine's name";
    EXPECT_GE(ram->Size(), 512u * 1024u);
    EXPECT_STREQ(ram->TtdRegion(), "neogs.ram");
    EXPECT_TRUE(ram->Writable()) << "plain bytes; the card's own tracker marks the write";
    EXPECT_EQ(DeviceMemory::Find(_context, "NeoGS.RAM"), ram) << "the same view on every call";
    ASSERT_NE(DeviceMemory::Find(_context, "neogs.flash"), nullptr);
    ASSERT_NE(DeviceMemory::Find(_context, "moonsound.wave"), nullptr);

    const uint32_t at = ram->Size() - 0x100;
    std::string error;
    ASSERT_TRUE(DeviceMemory::Write(_context, "neogs.ram", at, {0xDE, 0xAD}, "test", error)) << error;
    EXPECT_EQ(Read("neogs.ram", at, 2), (std::vector<uint8_t>{0xDE, 0xAD}));
    std::vector<uint8_t> past;
    EXPECT_FALSE(DeviceMemory::Read(_context, "neogs.ram", ram->Size() - 1, 2, past, error)) << "ranges are checked";

    const StateNode list = DeviceState::MemoryRegions(_context);
    bool listed = false;
    for (const StateNode& item : list.find("regions")->items)
        if (item.find("name")->s == "neogs.ram")
            listed = item.find("ttd_region")->s == "neogs.ram";
    EXPECT_TRUE(listed) << "in the region list every interface shows";
}

// A device-memory write and a later debugger edit inside one recorded frame: the checkpoint after them holds the
// write. Before the fix the edit's region listing reset the card's tracker, so the piece was neither in the edit's
// record nor in the next capture, and a seek there gave the old bytes
TEST_F(DeviceMemoryTtd_Test, AToolEditDuringARecordingKeepsTheDevicesUnsavedWrites)
{
    FeatureManager* features = _emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    _context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(2);

    const uint32_t at = DeviceMemory::Find(_context, "neogs.ram")->Size() - 0x200;
    const std::vector<uint8_t> before = Read("neogs.ram", at, 4);
    const std::vector<uint8_t> written = {0x11, 0x22, 0x33, 0x44};
    ASSERT_NE(before, written);
    std::string error;
    ASSERT_TRUE(DeviceMemory::Write(_context, "neogs.ram", at, written, "test: card RAM", error)) << error;
    // Another edit before the frame ends (any debugger poke)
    _emulator->EditMemoryFromTool("test: a poke", [&] { _context->pMemory->DirectWriteToZ80Memory(0x9000, 0x5A); });
    _emulator->RunNFrames(2);
    ttd->StopRecording();
    const size_t last = ttd->GetCheckpointCount() - 1;
    const uint64_t lastFrame = ttd->GetCheckpoint(last)->time.frame;

    ASSERT_TRUE(ttd->SeekTo({ttd->GetCheckpoint(0)->time.frame, 0}));
    EXPECT_EQ(Read("neogs.ram", at, 4), before) << "the start of the recording: the old bytes";
    ASSERT_TRUE(ttd->SeekTo({lastFrame, 0}));
    EXPECT_EQ(Read("neogs.ram", at, 4), written) << "the write is in the recording";
}

// Telemetry and a seek (state registry §5). The NeoGS firmware runs every frame, so its activity counters (session
// totals the automation reports) grow; a seek inside a frame replays recorded history and must not count it again.
// After the seek the machine stands paused: the audio activity indicators are dark
TEST_F(DeviceMemoryTtd_Test, ASeekNeitherCountsReplayedActivityNorLeavesIndicatorsLit)
{
    FeatureManager* features = _emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    _context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    GeneralSoundCard* gs = _context->pSoundManager->getGeneralSound();
    ASSERT_NE(gs, nullptr) << "the NeoGS card";
    // A beeper tone while recording: DI; loop: LD A,#10 : OUT (#FE),A : XOR A : OUT (#FE),A : JR loop
    const uint8_t beep[] = {0xF3, 0x3E, 0x10, 0xD3, 0xFE, 0xAF, 0xD3, 0xFE, 0x18, 0xF7};
    for (size_t i = 0; i < sizeof beep; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), beep[i]);
    Z80* z80 = _context->pCore->GetZ80();
    z80->pc = 0x8000;
    z80->halted = 0;
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(4);
    ttd->StopRecording();
    const uint64_t steps = gs->getActivityCounters().cpuSteps;
    ASSERT_GT(steps, 0u) << "the card's CPU runs";
    bool lit = false;
    for (const AudioDeviceInfo& device : _context->pSoundManager->devices())
        lit = lit || device.activeRecently;
    ASSERT_TRUE(lit) << "the beeper played";

    const uint64_t frame = ttd->GetCheckpoint(1)->time.frame;
    ASSERT_TRUE(ttd->SeekTo({frame, 30000}));   // inside the frame: a replay
    EXPECT_EQ(gs->getActivityCounters().cpuSteps, steps) << "the replayed frame is not counted again";
    for (const AudioDeviceInfo& device : _context->pSoundManager->devices())
        EXPECT_FALSE(device.activeRecently) << device.name << ": paused at the target, nothing plays";
}
