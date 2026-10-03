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
#include "emulator/ports/models/portdecoder_atm3.h"
#include "_helpers/zcsdtesthelper.h"

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

/// A tool edit while recording (Emulator::EditMemoryFromTool, mid-frame):
/// v1 keeps it as a barrier and stops before it; the engine has its bytes and
/// replays through it, landing on the machine the recording had at the target
TEST_F(TimeTravelManager_EngineSeek_Test, AToolEditReplaysWithItsBytes)
{
    ASSERT_NO_FATAL_FAILURE(StartMachine("PENTAGON", GSTypeKind::Z80, true));
    // loop: LD A,(#C000); LD (#C001),A; OUT (#FE),A; JR loop
    const uint8_t program[] = {0xF3, 0x3A, 0x00, 0xC0, 0x32, 0x01, 0xC0, 0xD3, 0xFE, 0x18, 0xF6};
    for (size_t i = 0; i < sizeof(program); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pMemory->DirectWriteToZ80Memory(0xC000, 0x01);
    _context->pCore->GetZ80()->pc = 0x8000;

    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    _emulator->RunTStates(20000, /*skipBreakpoints=*/true);
    _emulator->EditMemoryFromTool("test edit", [&] { _context->pMemory->ToolWriteToZ80Memory(0xC000, 0x5A); });
    _emulator->RunTStates(10000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint target{_context->emulatorState.frame_counter,
                                   _context->emulatorState.TtdTInFrame(_context->pCore->GetZ80()->t)};
    const MachineState recorded = Capture();
    ASSERT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xC001), 0x5A) << "the program copied the edited byte";
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);

    ttd::TimeTravelManager::TTDSeekResult result;
    EXPECT_FALSE(_v1->SeekTo(target, &result)) << "v1 stops at the edit's marker";

    _v1->SetReplaySource(&_engine);
    ASSERT_TRUE(_v1->SeekTo(target, &result)) << "the engine replays through the edit";
    const MachineState replayed = Capture();
    _v1->SetReplaySource(nullptr);
    EXPECT_EQ(std::memcmp(&recorded.cpu, &replayed.cpu, sizeof(recorded.cpu)), 0) << "CPU";
    EXPECT_EQ(recorded.t, replayed.t);
    EXPECT_TRUE(recorded.ram == replayed.ram) << "RAM";
    for (const auto& [id, bytes] : recorded.devices)
        EXPECT_TRUE(replayed.devices.at(id) == bytes) << "device " << int(id);
}

/// The media read journal (Phase 3, sector reads): a ZX-Evo program reads a
/// sector from its SD card while recording; the image file is then changed.
/// A seek from the engine's data still lands on the machine the original
/// image gave (the card's buffer, RAM, CPU), because the sector comes from the
/// session; a seek from v1's data now differs (the image is read again)
TEST_F(TimeTravelManager_EngineSeek_Test, SectorReadsComeFromTheSessionNotTheImage)
{
    ASSERT_NO_FATAL_FAILURE(StartMachine("ATM3", GSTypeKind::Z80, true));
    auto* decoder = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    const std::string image = TestPathHelper::GetUniqueTestScratchPath("ttd-media-reads.img");
    {
        std::ofstream out(image, std::ios::binary | std::ios::trunc);
        for (uint32_t i = 0; i < 64 * 512; ++i)
            out.put(static_cast<char>(i / 512 + i % 7));
    }
    ASSERT_TRUE(decoder->InsertSdCard(image, SdCardSpi::WriteMode::Session, false));
    _context->emulatorState.aFF77 = PortDecoder_ATM3::ATM_AFF77_PEN | PortDecoder_ATM3::ATM_AFF77_CPM;
    _context->emulatorState.flags &= ~CF_TRDOS;
    _context->emulatorState.pBF = 0x00;
    _context->pMemory->UpdateZ80Banks();
    decoder->DecodePortOut(0x0077, 0x00, 0);
    ASSERT_TRUE(zcsdtest::SdInit(decoder, 0x0057));

    // Wait about two frames, then CMD17 (byte address #400: sector 2) through
    // #57 and the 512 data bytes into #C000
    const uint8_t program[] = {
        0xF3, 0x11, 0x00, 0x16, 0x1B, 0x7A, 0xB3, 0x20, 0xFB,                  // DI; LD DE,#1600; delay
        0x01, 0x57, 0x00, 0x21, 0x3B, 0x80, 0x1E, 0x06,                        // LD BC,#57; LD HL,cmd; LD E,6
        0x7E, 0xED, 0x79, 0x23, 0x1D, 0x20, 0xF9,                              // send the command
        0xED, 0x78,                                                            // NCR
        0xED, 0x78, 0xFE, 0xFF, 0x28, 0xFA,                                    // R1
        0xED, 0x78, 0xFE, 0xFE, 0x20, 0xFA,                                    // data token
        0x21, 0x00, 0xC0, 0x11, 0x00, 0x02,                                    // LD HL,#C000; LD DE,512
        0xED, 0x78, 0x77, 0x23, 0x1B, 0x7A, 0xB3, 0x20, 0xF7,                  // the 512 bytes
        0xED, 0x78, 0xED, 0x78, 0x18, 0xFE,                                    // CRC; spin
        0x51, 0x00, 0x00, 0x04, 0x00, 0xFF};                                   // cmd at #803B
    for (size_t i = 0; i < sizeof(program); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;

    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(6, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);
    ASSERT_GT(_engine.MediaReads().Size(), 0u) << "the card read a sector while recording";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xC000), 2 + (2 * 512) % 7) << "sector 2's first byte";

    // The machine at points around the read, from v1's data with the image intact
    std::vector<ttd::TTDTimePoint> targets;
    const uint32_t span = static_cast<uint32_t>(_v1->FrameSpan());
    for (size_t i = 1; i + 1 < _v1->GetCheckpointCount(); ++i)
        for (uint32_t k : {1u, 2u, 3u})
            targets.push_back({_v1->GetCheckpoint(i)->time.frame, span * k / 4});
    std::vector<MachineState> original;
    for (const ttd::TTDTimePoint& t : targets)
    {
        ASSERT_TRUE(_v1->SeekTo(t));
        original.push_back(Capture());
    }

    // The image changes on disk
    {
        std::fstream f(image, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(2 * 512);
        for (int i = 0; i < 512; ++i)
            f.put(static_cast<char>(0xE5));
    }
    size_t v1Differs = 0;
    for (size_t n = 0; n < targets.size(); ++n)
    {
        SCOPED_TRACE("frame " + std::to_string(targets[n].frame) + " T " + std::to_string(targets[n].tInFrame));
        _v1->SetReplaySource(&_engine);
        ASSERT_TRUE(_v1->SeekTo(targets[n]));
        const MachineState fromEngine = Capture();
        _v1->SetReplaySource(nullptr);
        EXPECT_EQ(std::memcmp(&fromEngine.cpu, &original[n].cpu, sizeof(fromEngine.cpu)), 0) << "CPU";
        EXPECT_TRUE(fromEngine.ram == original[n].ram) << "RAM";
        for (const auto& [id, bytes] : original[n].devices)
            EXPECT_TRUE(fromEngine.devices.at(id) == bytes) << "device " << int(id);

        ASSERT_TRUE(_v1->SeekTo(targets[n]));
        const MachineState fromV1 = Capture();
        bool same = fromV1.ram == original[n].ram;
        for (const auto& [id, bytes] : original[n].devices)
            same = same && fromV1.devices.at(id) == bytes;
        v1Differs += same ? 0 : 1;
    }
    EXPECT_GT(v1Differs, 0u) << "v1 reads the changed image: the test can see a wrong sector";
    std::remove(image.c_str());
}
