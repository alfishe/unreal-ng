/// @file ttdenginerecording_test.cpp
/// @brief What the controller's engine records from the running emulator:
/// every frame it records restores the machine the recording had there live,
/// a restore writes only what
/// differs, every input event and marker lands at its time, and machine time
/// follows the measured frame lengths
/// (docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.5).
/// (Until Phase 6 the engine recorded as v1's TimeTravelManager's shadow and
/// v1's checkpoints were the reference.)
///
/// Boots a Pentagon and runs a game for a few hundred frames: slower than the
/// 50 ms guideline, the live counterpart of the corpus oracle.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/io/rtc/ds12887.h"

namespace
{
uint64_t Fnv1a(const uint8_t* data, size_t size)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i)
        h = (h ^ data[i]) * 1099511628211ull;
    return h;
}

/// What a restore must reproduce; RAM as a hash (a point per frame)
struct LiveState
{
    ttd::TTDTimePoint at;
    ttd::TTDCpuState cpu;
    uint64_t t = 0;
    uint64_t ram = 0;
    std::map<uint8_t, std::vector<uint8_t>> devices;
};
}  // namespace

class TTDEngineRecording_Test : public ::testing::Test
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
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        const auto sna = TestPathHelper::FindProjectRoot() / "testdata/loaders/sna/action.sna";
        ASSERT_TRUE(_emulator->LoadSnapshot(sna.string())) << sna;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _ttd->StopRecording();
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    }

    ttd::TimeTravelEngine& Engine() const { return _ttd->GetEngine(); }

    LiveState Capture() const
    {
        LiveState s;
        s.at = _ttd->CurrentPosition();
        const Z80* z80 = _context->pCore->GetZ80();
        s.cpu = ttd::CaptureCpuState(*z80);
        s.t = z80->t;
        s.ram = Fnv1a(_context->pMemory->RAMPageAddress(0), size_t(_ttd->GetModelRamPages()) * 0x4000);
        for (const ttd::TTDRegistryEntry& e : _ttd->GetPeripheralRegistry().Entries())
            if (e.device && e.device->TTDStateSize() != 0 && e.legacyId != ttd::PeripheralId::None)
                e.device->TTDSaveStateTo(s.devices[static_cast<uint8_t>(e.legacyId)]);
        return s;
    }

    /// Records @p frames frames, keeping the machine after each
    std::vector<LiveState> RecordKeepingEveryFrame(int frames)
    {
        std::vector<LiveState> live;
        EXPECT_TRUE(_ttd->StartRecording());
        for (int f = 0; f < frames; ++f)
        {
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
            live.push_back(Capture());
        }
        _ttd->StopRecording();
        return live;
    }
};

TEST_F(TTDEngineRecording_Test, EveryRecordedFrameRestoresTheLiveMachine)
{
    const std::vector<LiveState> live = RecordKeepingEveryFrame(300);
    ASSERT_GE(Engine().CheckpointCount(), 300u);

    // The engine stored only real changes: fewer memory versions than a quarter of a full copy per checkpoint (the
    // game changes a few pieces a frame; about 1400 versions for 301 checkpoints of 32 pieces; device states not
    // counted)
    uint64_t memoryVersions = 0;
    for (uint32_t r = 0; r < Engine().Regions().size(); ++r)
        if (!Engine().IsDeviceStateRegion(r))
            memoryVersions += Engine().RegionVersionCount(r);
    EXPECT_LT(memoryVersions, uint64_t(Engine().CheckpointCount()) * Engine().Regions()[0].pieces / 4);

    // Every frame, newest first: CPU, RAM and every device as the machine was
    for (auto it = live.rbegin(); it != live.rend(); ++it)
    {
        SCOPED_TRACE("frame " + std::to_string(it->at.frame) + " T " + std::to_string(it->at.tInFrame));
        ttd::TTDSeekResult r;
        ASSERT_TRUE(_ttd->SeekTo(it->at, &r));
        const LiveState seen = Capture();
        EXPECT_EQ(std::memcmp(&seen.cpu, &it->cpu, sizeof(seen.cpu)), 0) << "CPU";
        EXPECT_EQ(seen.t, it->t);
        EXPECT_EQ(seen.ram, it->ram) << "RAM";
        for (const auto& [id, bytes] : it->devices)
            EXPECT_TRUE(seen.devices.count(id) && seen.devices.at(id) == bytes) << "device " << int(id);
        if (HasFailure())
            return;   // the first divergence is the useful one
    }
}

TEST_F(TTDEngineRecording_Test, RestoreToMemoryWritesOnlyWhatDiffers)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(200, /*skipBreakpoints=*/true);
    _ttd->StopRecording();
    ttd::TimeTravelEngine& engine = Engine();
    const size_t count = engine.CheckpointCount();
    ASSERT_GE(count, 150u);
    const uint32_t pieces = engine.Regions()[0].pieces;
    uint8_t* live = _context->pMemory->RAMPageAddress(0);

    // Pieces whose version differs between two checkpoints
    auto differing = [&](size_t a, size_t b) {
        uint64_t n = 0;
        for (uint32_t p = 0; p < pieces; ++p)
            n += engine.VersionAt(a, 0, p) != engine.VersionAt(b, 0, p) ? 1 : 0;
        return n;
    };
    // Live memory is the checkpoint as decoded from scratch
    auto expectLiveIs = [&](size_t index) {
        std::vector<uint8_t> ram(size_t(pieces) * ttd::kTTDPieceSize), present;
        ASSERT_TRUE(engine.RestoreRegion(index, 0, ram.data(), &present).Ok());
        for (uint32_t p = 0; p < pieces; ++p)
            if (present[p])
            {
                ASSERT_EQ(std::memcmp(live + size_t(p) * ttd::kTTDPieceSize, ram.data() + size_t(p) * ttd::kTTDPieceSize,
                                      ttd::kTTDPieceSize),
                          0)
                    << "piece " << p << " after restoring checkpoint " << index;
            }
    };

    // Live memory holds the last frame: one frame back decodes what that frame changed
    ttd::TTDRestoreStats stats;
    size_t at = count - 1;
    for (const size_t target : {count - 2, count - 3, size_t(17), count / 2, size_t(0), count - 1, size_t(99)})
    {
        ASSERT_TRUE(engine.RestoreToMemory(target, nullptr, &stats).Ok());
        EXPECT_EQ(stats.piecesDecoded, differing(at, target)) << "seek " << at << " -> " << target;
        ASSERT_NO_FATAL_FAILURE(expectLiveIs(target));
        at = target;
    }

    // The same position again: nothing to write
    ASSERT_TRUE(engine.RestoreToMemory(at, nullptr, &stats).Ok());
    EXPECT_EQ(stats.piecesDecoded, 0u);

    // A piece written since is restored even though its version matches
    live[5 * ttd::kTTDPieceSize + 7] ^= 0xFF;
    ASSERT_TRUE(engine.RestoreToMemory(at, [](uint32_t, uint32_t p) { return p == 5; }, &stats).Ok());
    EXPECT_EQ(stats.piecesDecoded, 1u);
    ASSERT_NO_FATAL_FAILURE(expectLiveIs(at));

    // Memory changed behind the engine's back: everything is written
    engine.ForgetMemory();
    ASSERT_TRUE(engine.RestoreToMemory(at, nullptr, &stats).Ok());
    EXPECT_GT(stats.piecesDecoded, 0u);
    EXPECT_EQ(stats.piecesSkipped, 0u);
}

/// Phase 3, Step 1: while recording, the engine gets key presses and releases
/// and a marker with its reason, each at the time it happened, and every IN
/// and OUT, with the journals' positions at each checkpoint
TEST_F(TTDEngineRecording_Test, EveryJournaledEventReachesTheEngineAtItsTime)
{
    // A program that reads the keyboard and writes the border all the time:
    // DI; loop: IN A,(#FE); OUT (#FE),A; JR loop
    const uint8_t program[] = {0xF3, 0xDB, 0xFE, 0xD3, 0xFE, 0x18, 0xFA};
    for (size_t i = 0; i < sizeof(program); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    ttd::TTDInputEvent key;
    key.kind = ttd::TTDInputKind::Key;
    key.key = 12;
    key.pressed = true;
    std::vector<ttd::TTDTimePoint> at;   // press, marker, release
    at.push_back(_ttd->CurrentPosition());
    ASSERT_TRUE(_ttd->SubmitLiveInput(key));
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    at.push_back(_ttd->CurrentPosition());
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::TapeControl, "tape play (test)");
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    key.pressed = false;
    at.push_back(_ttd->CurrentPosition());
    ASSERT_TRUE(_ttd->SubmitLiveInput(key));
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    const ttd::TimeTravelEngine& engine = Engine();
    auto machineTime = [&](const ttd::TTDTimePoint& p) {
        ttd::TTDMachineTime start = 0;
        EXPECT_TRUE(engine.Frames().Start(p.frame, start)) << "frame " << p.frame;
        return start + p.tInFrame;
    };
    std::vector<const ttd::TTDEvent*> inputs, markers;
    for (const ttd::TTDEvent& e : engine.Events().Events())
        (ttd::IsInputKind(e.kind) ? inputs : markers).push_back(&e);
    ASSERT_EQ(inputs.size(), 2u) << "the press and the release";
    ASSERT_EQ(markers.size(), 1u);
    for (size_t i = 0; i < 2; ++i)
    {
        ttd::TTDInputEvent back;
        ttd::TTDEventLog::ToInput(*inputs[i], back);
        EXPECT_EQ(back.kind, ttd::TTDInputKind::Key) << "input " << i;
        EXPECT_EQ(back.key, 12) << "input " << i;
        EXPECT_EQ(back.pressed, i == 0) << "input " << i;
        EXPECT_EQ(inputs[i]->machineTime, machineTime(at[i == 0 ? 0 : 2])) << "input " << i;
    }
    EXPECT_EQ(markers[0]->machineTime, machineTime(at[1]));
    EXPECT_EQ(static_cast<uint16_t>(markers[0]->kind),
              0x0100 + static_cast<uint16_t>(ttd::TTDExternalEventKind::TapeControl));
    const std::vector<uint8_t>& reason = engine.Payloads().Bytes(markers[0]->payload);
    EXPECT_EQ(std::string(reason.begin(), reason.end()), "tape play (test)");
    EXPECT_EQ(engine.Events().FirstBarrierIn(0, ~ttd::TTDMachineTime(0)), nullptr) << "a tape command is input";

    // Every IN and OUT of the loop, each checkpoint's cursors in order
    ASSERT_GT(engine.BusReads().Size(), 1000u) << "the program reads the keyboard";
    ASSERT_GT(engine.BusWrites().Size(), 1000u) << "and writes the border";
    ttd::TTDPortRecord r;
    ASSERT_TRUE(engine.BusReads().Get(engine.BusReads().Size() - 1, r));
    EXPECT_EQ(r.port & 0xFF, 0xFEu);
    ASSERT_TRUE(engine.BusWrites().Get(engine.BusWrites().Size() - 1, r));
    EXPECT_EQ(r.port & 0xFF, 0xFEu);
    for (size_t i = 1; i < engine.CheckpointCount(); ++i)
    {
        ASSERT_GT(engine.Checkpoint(i)->busReadCursor, engine.Checkpoint(i - 1)->busReadCursor) << "checkpoint " << i;
        ASSERT_GT(engine.Checkpoint(i)->busWriteCursor, engine.Checkpoint(i - 1)->busWriteCursor) << "checkpoint " << i;
    }
    EXPECT_LE(engine.Checkpoint(engine.CheckpointCount() - 1)->busReadCursor, engine.BusReads().Size());
}

/// Phase 3, Step 3: machine time comes from the frame table. The Sprinter
/// switches between 320- and 312-line frames at a frame start (#2C / #2D;
/// here through its PLD state, which the screen applies at the next frame
/// start, as after the port write). Each frame starts where the last one
/// started plus its measured length, so machine time never goes back -
/// frame x the current length would - and each change of
/// length is a fact in the event log
TEST(TTDEngineFrameTable_Test, FrameStartsFollowTheMeasuredFrameLengths)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("SPRINTER", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    auto* sprinter = dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder);
    ASSERT_NE(sprinter, nullptr);
    ttd::TimeTravelController* controller = context->pTimeTravelController;
    ASSERT_NE(controller, nullptr);
    const ttd::TimeTravelEngine& engine = controller->GetEngine();

    ASSERT_TRUE(controller->StartRecording());
    emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    sprinter->GetPldState().frameLines = 1;   // 312 lines from the next frame start
    emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    sprinter->GetPldState().frameLines = 0;   // 320 again
    emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    controller->StopRecording();

    const ttd::TTDFrameTable& frames = engine.Frames();
    ASSERT_GE(frames.Count(), 13u);
    const uint64_t units = context->emulatorState.ttd_clock_units ? context->emulatorState.ttd_clock_units : 1;
    const uint64_t full = 320ull * 224 * units, shorter = 312ull * 224 * units;
    std::vector<uint64_t> lengths;
    for (uint64_t f = frames.FirstFrame(); f < frames.LastFrame(); ++f)
    {
        ttd::TTDMachineTime a = 0, b = 0;
        ASSERT_TRUE(frames.Start(f, a));
        ASSERT_TRUE(frames.Start(f + 1, b));
        ASSERT_GT(b, a) << "machine time goes forward, frame " << f;
        lengths.push_back(b - a);
    }
    const auto count = [&](uint64_t length) { return std::count(lengths.begin(), lengths.end(), length); };
    EXPECT_GE(count(shorter), 4) << "the 312-line frames, measured";
    EXPECT_GE(count(full), 6) << "the 320-line ones";
    EXPECT_EQ(count(shorter) + count(full), static_cast<long>(lengths.size())) << "every frame is one or the other";

    size_t changes = 0;
    for (const ttd::TTDEvent& e : engine.Events().Events())
        changes += e.kind == ttd::TTDEventKind::FrameLengthChange ? 1 : 0;
    EXPECT_EQ(changes, 2u) << "to 312 lines and back";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// Phase 3, Step 5: the real-time clocks' emulated microseconds come from the
/// base T-states run since power-on, at the CPU's rate. A Sprinter switching
/// to 312 lines and back gains exactly each frame's duration (20,480 us at 320
/// lines, 19,968 at 312) and never goes back - frame x the current duration did
TEST(TTDEngineFrameTable_Test, EmulatedMicrosecondsFollowEveryFrameLength)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("SPRINTER", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* sprinter = dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder);
    ASSERT_NE(sprinter, nullptr);
    emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    std::vector<uint64_t> steps;
    uint64_t last = sprinter->EmulatedMicroseconds();
    auto frame = [&]() {
        emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        const uint64_t now = sprinter->EmulatedMicroseconds();
        ASSERT_GE(now, last) << "never back";
        steps.push_back(now - last);
        last = now;
    };
    for (int i = 0; i < 3; ++i)
        ASSERT_NO_FATAL_FAILURE(frame());
    sprinter->GetPldState().frameLines = 1;
    for (int i = 0; i < 4; ++i)
        ASSERT_NO_FATAL_FAILURE(frame());
    sprinter->GetPldState().frameLines = 0;
    for (int i = 0; i < 4; ++i)
        ASSERT_NO_FATAL_FAILURE(frame());
    // A frame ends at an instruction boundary, a few T-states past its end:
    // each step is its frame's duration within a few microseconds
    const auto within = [](uint64_t step, uint64_t us) { return step + 8 >= us && step <= us + 8; };
    ASSERT_EQ(steps.size(), 11u);
    for (size_t i = 0; i < steps.size(); ++i)
    {
        // The line count is latched at the next frame start: frames 4-7 are 312 lines
        const uint64_t want = (i >= 4 && i < 8) ? 19968 : 20480;
        EXPECT_TRUE(within(steps[i], want)) << "frame " << i << ": " << steps[i] << " us, want " << want;
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// Phase 3, Step 5: the session's time base is the host's wall time when the
/// recording starts, at the machine's emulated time then. A clock anchoring
/// later (any frame after the start) reads that base advanced by exactly the
/// emulated time since, so every clock of the machine agrees
TEST(TTDEngineFrameTable_Test, SessionTimeBaseIsOneInstantForEveryClock)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("SPRINTER", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    PortDecoder* decoder = context->pPortDecoder;
    EXPECT_EQ(decoder->SessionWallMicros(), Ds12887::kNoSessionWall) << "no session yet";

    emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    ASSERT_TRUE(context->pTimeTravelController->StartRecording());
    const int64_t wall = context->ttdSessionWallMicros;
    ASSERT_NE(wall, Ds12887::kNoSessionWall);
    const uint64_t start = decoder->EmulatedMicroseconds();
    EXPECT_EQ(decoder->SessionWallMicros(), wall) << "at the start: the wall time taken";

    emulator->RunNFrames(7, /*skipBreakpoints=*/true);
    const uint64_t later = decoder->EmulatedMicroseconds();
    EXPECT_GT(later, start + 6 * 20000);
    EXPECT_EQ(decoder->SessionWallMicros(), wall + static_cast<int64_t>(later - start))
        << "later: the same base, moved by the emulated time only";
    context->pTimeTravelController->StopRecording();
    EmulatorTestHelper::CleanupEmulator(emulator);
}
