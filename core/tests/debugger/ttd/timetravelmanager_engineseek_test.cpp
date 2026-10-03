/// @file timetravelmanager_engineseek_test.cpp
/// @brief Phase 3 A/B: a seek inside a frame from the engine's data alone
/// (its checkpoint - CPU, chipset, every memory region, every device - its
/// event log and its bus journals; TimeTravelManager::SetReplaySource) lands
/// on exactly the machine a seek from v1's data lands on: CPU, all RAM and
/// every device's state, at several points of several frames. Scenarios: a
/// live recording with key presses mid-frame, and the recorded fixtures with
/// keys (Dizzy X) and a tape load (Green Beret).
///
/// Boots machines and replays tens of frames: slower than the 50 ms
/// guideline, one acceptance check per scenario.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/bench/ttdv1feeder.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace fs = std::filesystem;

namespace
{
/// Everything a seek must reproduce
struct MachineState
{
    ttd::TTDCpuState cpu;
    uint64_t frame = 0;
    uint64_t t = 0;
    std::vector<uint8_t> ram;
    std::map<uint8_t, std::vector<uint8_t>> devices;
};
}  // namespace

class TimeTravelManager_EngineSeek_Test : public ::testing::Test
{
protected:
    SoundCardScope _soundCards;   // the fixtures were recorded on the shipped configs, cards included
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _v1 = nullptr;
    ttd::TimeTravelEngine _engine;
    size_t _busPlayed = 0;   ///< engine seeks whose CPU read the engine's IN journal

    void StartMachine(const std::string& model, GSTypeKind generalSound, bool fitGs)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _v1 = _context->pTimeTravelManager;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        if (fitGs)
            ASSERT_TRUE(FitGeneralSoundCard(_context->pSoundManager, generalSound));
    }

    void TearDown() override
    {
        if (_v1)
        {
            _v1->SetReplaySource(nullptr);
            _v1->SetShadowEngine(nullptr);
        }
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    MachineState Capture() const
    {
        MachineState s;
        Z80* z80 = _context->pCore->GetZ80();
        s.cpu = ttd::CaptureCpuState(*z80);
        s.frame = _context->emulatorState.frame_counter;
        s.t = z80->t;
        const size_t pages = _v1->GetModelRamPages();
        s.ram.assign(_context->pMemory->RAMPageAddress(0), _context->pMemory->RAMPageAddress(0) + pages * 0x4000);
        for (const auto& [id, device] : _v1->GetPeripheralRegistry().Devices())
            if (device && device->TTDStateSize() != 0)
                device->TTDSaveStateTo(s.devices[id]);
        return s;
    }

    /// Seek to @p target from v1's data, then from the engine's: the same machine
    void ExpectSameSeek(const ttd::TTDTimePoint& target)
    {
        SCOPED_TRACE("frame " + std::to_string(target.frame) + " T " + std::to_string(target.tInFrame));
        _v1->SetReplaySource(nullptr);
        ttd::TimeTravelManager::TTDSeekResult a, b;
        ASSERT_TRUE(_v1->SeekTo(target, &a));
        const MachineState fromV1 = Capture();
        _v1->SetReplaySource(&_engine);
        ASSERT_TRUE(_v1->SeekTo(target, &b));
        const MachineState fromEngine = Capture();
        _v1->SetReplaySource(nullptr);
        // Did the CPU read the engine's bus journal (played past the checkpoint's cursor)?
        const int64_t cp = _engine.CheckpointIndexOf({0, target.frame, 0});
        ASSERT_GE(cp, 0);
        if (_engine.BusReads().GetMode() == ttd::TTDPortJournal::Mode::Play &&
            _engine.BusReads().Cursor() > _engine.Checkpoint(size_t(cp))->busReadCursor)
            ++_busPlayed;

        EXPECT_EQ(a.arrivedAt, b.arrivedAt);
        EXPECT_EQ(std::memcmp(&fromV1.cpu, &fromEngine.cpu, sizeof(fromV1.cpu)), 0) << "CPU";
        EXPECT_EQ(fromV1.frame, fromEngine.frame);
        EXPECT_EQ(fromV1.t, fromEngine.t);
        ASSERT_EQ(fromV1.ram.size(), fromEngine.ram.size());
        for (size_t i = 0; i < fromV1.ram.size(); ++i)
            if (fromV1.ram[i] != fromEngine.ram[i])
            {
                ADD_FAILURE() << "RAM differs at " << i << " (page " << i / 0x4000 << ")";
                break;
            }
        for (const auto& [id, bytes] : fromV1.devices)
            EXPECT_TRUE(fromEngine.devices.at(id) == bytes) << "device " << int(id);
    }

    /// Points spread over the session: inside frames, at several offsets
    std::vector<ttd::TTDTimePoint> Targets(size_t perFrame = 3) const
    {
        std::vector<ttd::TTDTimePoint> out;
        const size_t count = _v1->GetCheckpointCount();
        const uint32_t span = static_cast<uint32_t>(_v1->FrameSpan());
        for (size_t i : {size_t(1), count / 3, count / 2, count - 2})
        {
            if (i >= count)
                continue;
            const uint64_t frame = _v1->GetCheckpoint(i)->time.frame;
            for (size_t k = 1; k <= perFrame; ++k)
                out.push_back({frame, static_cast<uint32_t>(span * k / (perFrame + 1))});
        }
        return out;
    }
};

TEST_F(TimeTravelManager_EngineSeek_Test, LiveRecordingWithKeys_EngineSeeksLandOnV1sMachine)
{
    // The classic GS card: with NeoGS v1 keeps no port journals (its ZX-DMA is not isolated)
    ASSERT_NO_FATAL_FAILURE(StartMachine("PENTAGON", GSTypeKind::Z80, true));
    // A program whose memory follows the keyboard: DI; LD HL,#C000;
    // loop: LD A,#7F; IN A,(#FE); LD (HL),A; INC L; JR loop
    const uint8_t program[] = {0xF3, 0x21, 0x00, 0xC0, 0x3E, 0x7F, 0xDB, 0xFE, 0x77, 0x2C, 0x18, 0xF8};
    for (size_t i = 0; i < sizeof(program); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;

    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    ttd::TTDInputEvent key;
    key.kind = ttd::TTDInputKind::Key;
    key.key = ZXKEY_SPACE;
    for (int f = 0; f < 24; ++f)
    {
        if (f == 5 || f == 13)
        {
            _emulator->RunTStates(20000, /*skipBreakpoints=*/true);   // mid-frame
            key.pressed = f == 5;
            ASSERT_TRUE(_v1->SubmitLiveInput(key));
        }
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);
    ASSERT_EQ(_engine.CheckpointCount(), _v1->GetCheckpointCount());

    for (const ttd::TTDTimePoint& target : Targets())
        ASSERT_NO_FATAL_FAILURE(ExpectSameSeek(target));
    // Around the key events: the press lands inside these frames
    const uint32_t span = static_cast<uint32_t>(_v1->FrameSpan());
    for (const ttd::TTDInputEvent& e : _v1->GetInputJournal().Events())
        for (int32_t d : {-500, 1, 900})
            if (int64_t(e.time.tInFrame) + d > 0 && int64_t(e.time.tInFrame) + d < span)
                ASSERT_NO_FATAL_FAILURE(
                    ExpectSameSeek({e.time.frame, static_cast<uint32_t>(int64_t(e.time.tInFrame) + d)}));
    ASSERT_GT(_v1->GetPortReadJournal().Size(), 1000u) << "port journals on";
    EXPECT_GT(_busPlayed, 0u) << "the replays read the engine's IN journal";
    EXPECT_EQ(_engine.BusReads().ValueMismatches(), 0u) << "the devices answered as recorded";
}

TEST_F(TimeTravelManager_EngineSeek_Test, RecordedFixtures_EngineSeeksLandOnV1sMachine)
{
    const fs::path root = TestPathHelper::FindProjectRoot() / "testdata/ttd/port-journals";
    for (const char* name : {"dizzyx.ttd", "greenberet-load.ttd"})
    {
        SCOPED_TRACE(name);
        ttd::TTDFileInfo info;
        std::string err;
        ASSERT_TRUE(ttd::ReadTTDFileInfo((root / name).string(), info, err)) << err;
        ASSERT_NO_FATAL_FAILURE(StartMachine(info.machine.model, info.machine.generalSound, true));
        std::ifstream in(root / name, std::ios::binary);
        ASSERT_TRUE(_v1->DeserializeSession(in, err)) << err;
        ASSERT_TRUE(ttd::bench::FeedV1Session(*_v1, _engine, err)) << err;

        _busPlayed = 0;
        for (const ttd::TTDTimePoint& target : Targets(2))
            ASSERT_NO_FATAL_FAILURE(ExpectSameSeek(target));
        EXPECT_GT(_busPlayed, 0u) << "the replays read the engine's IN journal";

        _v1->SetReplaySource(nullptr);
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
        _v1 = nullptr;
        _engine.EndSession();
    }
}
