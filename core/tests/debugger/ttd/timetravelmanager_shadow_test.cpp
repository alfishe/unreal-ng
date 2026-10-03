/// @file timetravelmanager_shadow_test.cpp
/// @brief Shadow mode: TimeTravelEngine records the running emulator next to v1
/// (TimeTravelManager::SetShadowEngine) and every frame it records restores
/// exactly as v1's checkpoint of the same frame
/// (docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.5).
///
/// Boots a Pentagon and runs a game for a few hundred frames: slower than the
/// 50 ms guideline, it is the live counterpart of the corpus oracle.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/ttdeventscompare.h"
#include "base/featuremanager.h"
#include "debugger/ttd/bench/ttdv1feeder.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"

class TimeTravelManager_Shadow_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _v1 = nullptr;
    ttd::TimeTravelEngine _engine;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _v1 = _context->pTimeTravelManager;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        const auto sna = TestPathHelper::FindProjectRoot() / "testdata/loaders/sna/action.sna";
        ASSERT_TRUE(_emulator->LoadSnapshot(sna.string())) << sna;
    }

    void TearDown() override
    {
        if (_v1)
            _v1->SetShadowEngine(nullptr);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// v1's checkpoint index of @p frame, or -1
    int64_t V1IndexOf(uint64_t frame) const
    {
        for (size_t i = 0; i < _v1->GetCheckpointCount(); ++i)
            if (_v1->GetCheckpoint(i)->time.frame == frame)
                return static_cast<int64_t>(i);
        return -1;
    }

    /// Every engine checkpoint equals v1's checkpoint of the same frame
    void ExpectEngineMatchesV1()
    {
        ASSERT_GT(_engine.CheckpointCount(), 0u);
        std::vector<uint8_t> v1Ram, v1Present, engineRam, enginePresent;
        std::string err;
        for (size_t i = 0; i < _engine.CheckpointCount(); ++i)
        {
            const ttd::TTDEngineCheckpoint* got = _engine.Checkpoint(i);
            const int64_t v = V1IndexOf(got->position.frame);
            ASSERT_GE(v, 0) << "v1 has no checkpoint of frame " << got->position.frame;
            const ttd::TTDCheckpoint* want = _v1->GetCheckpoint(static_cast<size_t>(v));
            EXPECT_EQ(std::memcmp(&got->cpu, &want->cpu, sizeof(want->cpu)), 0) << "CPU, frame " << got->position.frame;
            for (const auto& [id, blob] : want->peripheralBlobs)
            {
                std::vector<uint8_t> state;
                ASSERT_TRUE(_engine.DeviceState(i, id, state)) << "device " << int(id) << ", frame " << got->position.frame;
                EXPECT_TRUE(state == ttd::TTDPeripheralRegistry::DecodeBlob(id, blob))
                    << "device " << int(id) << ", frame " << got->position.frame;
            }
            ASSERT_TRUE(ttd::bench::DecodeV1Ram(*_v1, static_cast<size_t>(v), v1Ram, v1Present, err)) << err;
            engineRam.assign(v1Ram.size(), 0);
            ASSERT_TRUE(_engine.RestoreRegion(i, 0, engineRam.data(), &enginePresent).Ok());
            ASSERT_TRUE(engineRam == v1Ram) << "RAM differs at frame " << got->position.frame;
        }
    }
};

TEST_F(TimeTravelManager_Shadow_Test, EveryRecordedFrameMatchesV1_AndTheLastMatchesLiveMemory)
{
    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(300, /*skipBreakpoints=*/true);
    _v1->StopRecording();

    ASSERT_EQ(_engine.CheckpointCount(), _v1->GetCheckpointCount());
    ASSERT_NO_FATAL_FAILURE(ExpectEngineMatchesV1());

    // The delta base invariant, seen from outside: the last checkpoint is the live RAM
    const size_t last = _engine.CheckpointCount() - 1;
    std::vector<uint8_t> ram(_engine.Regions()[0].pieces * size_t(ttd::kTTDPieceSize));
    ASSERT_TRUE(_engine.RestoreRegion(last, 0, ram.data()).Ok());
    for (uint32_t page = 0; page < _engine.Regions()[0].pieces / 4; ++page)
        ASSERT_EQ(std::memcmp(ram.data() + size_t(page) * 4 * ttd::kTTDPieceSize,
                              _context->pMemory->RAMPageAddress(static_cast<uint16_t>(page)), 4 * ttd::kTTDPieceSize),
                  0)
            << "page " << page;

    // The engine stored only real changes: far fewer memory versions than v1's
    // key frames re-store (device states, which v1 keeps as blobs, not counted)
    uint64_t memoryVersions = 0;
    for (uint32_t r = 0; r < _engine.Regions().size(); ++r)
        if (!_engine.IsDeviceStateRegion(r))
            memoryVersions += _engine.RegionVersionCount(r);
    EXPECT_LT(memoryVersions, _v1->GetPageStore().GetUsedSlots());
}

TEST_F(TimeTravelManager_Shadow_Test, ResumeFromThePastStartsANewEngineSession)
{
    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(120, /*skipBreakpoints=*/true);
    const uint64_t first = _v1->GetCheckpoint(0)->time.frame;
    const uint64_t resumeAt = first + 40;

    // v1 cuts its history after the resume point; the engine records the trunk
    // only, so it starts over from there (branches come with Phase 5)
    ASSERT_TRUE(_v1->ResumeRecordingFrom({resumeAt, 0}));
    _emulator->RunNFrames(60, /*skipBreakpoints=*/true);
    _v1->StopRecording();

    ASSERT_GT(_engine.CheckpointCount(), 0u);
    EXPECT_GT(_engine.Checkpoint(0)->position.frame, resumeAt - 1) << "the engine's session starts after the resume";
    ASSERT_NO_FATAL_FAILURE(ExpectEngineMatchesV1());
}

TEST_F(TimeTravelManager_Shadow_Test, DetachedEngineRecordsNothing)
{
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(20, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    EXPECT_FALSE(_engine.IsSessionOpen());
    EXPECT_EQ(_engine.CheckpointCount(), 0u);
}

TEST_F(TimeTravelManager_Shadow_Test, RestoreToMemoryWritesOnlyWhatDiffers_AndMatchesV1)
{
    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(200, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);
    const size_t count = _engine.CheckpointCount();
    ASSERT_GE(count, 150u);
    const uint32_t pieces = _engine.Regions()[0].pieces;
    uint8_t* live = _context->pMemory->RAMPageAddress(0);

    // Pieces whose version differs between two checkpoints
    auto differing = [&](size_t a, size_t b) {
        uint64_t n = 0;
        for (uint32_t p = 0; p < pieces; ++p)
            n += _engine.VersionAt(a, 0, p) != _engine.VersionAt(b, 0, p) ? 1 : 0;
        return n;
    };
    auto expectLiveIs = [&](size_t index) {
        std::vector<uint8_t> ram, present;
        std::string err;
        const int64_t v = V1IndexOf(_engine.Checkpoint(index)->position.frame);
        ASSERT_GE(v, 0);
        ASSERT_TRUE(ttd::bench::DecodeV1Ram(*_v1, static_cast<size_t>(v), ram, present, err)) << err;
        for (uint32_t p = 0; p < pieces; ++p)
            if (present[p])
                ASSERT_EQ(std::memcmp(live + size_t(p) * ttd::kTTDPieceSize, ram.data() + size_t(p) * ttd::kTTDPieceSize,
                                      ttd::kTTDPieceSize),
                          0)
                    << "piece " << p << " after restoring checkpoint " << index;
    };

    // Live memory holds the last frame: one frame back decodes what that frame changed
    ttd::TTDRestoreStats stats;
    size_t at = count - 1;
    for (const size_t target : {count - 2, count - 3, size_t(17), count / 2, size_t(0), count - 1, size_t(99)})
    {
        ASSERT_TRUE(_engine.RestoreToMemory(target, nullptr, &stats).Ok());
        EXPECT_EQ(stats.piecesDecoded, differing(at, target)) << "seek " << at << " -> " << target;
        ASSERT_NO_FATAL_FAILURE(expectLiveIs(target));
        at = target;
    }

    // The same position again: nothing to write
    ASSERT_TRUE(_engine.RestoreToMemory(at, nullptr, &stats).Ok());
    EXPECT_EQ(stats.piecesDecoded, 0u);

    // A piece written since is restored even though its version matches
    live[5 * ttd::kTTDPieceSize + 7] ^= 0xFF;
    ASSERT_TRUE(_engine.RestoreToMemory(at, [](uint32_t, uint32_t p) { return p == 5; }, &stats).Ok());
    EXPECT_EQ(stats.piecesDecoded, 1u);
    ASSERT_NO_FATAL_FAILURE(expectLiveIs(at));

    // Memory changed behind the engine's back: everything is written
    _engine.ForgetMemory();
    ASSERT_TRUE(_engine.RestoreToMemory(at, nullptr, &stats).Ok());
    EXPECT_GT(stats.piecesDecoded, 0u);
    EXPECT_EQ(stats.piecesSkipped, 0u);
}

/// Phase 3, Step 1: while recording, the shadow engine gets what v1 journals -
/// key presses and releases, a marker with its reason - each at the same time,
/// and every IN and OUT, with the journals' positions at each checkpoint
TEST_F(TimeTravelManager_Shadow_Test, EveryJournaledEventReachesTheEngineAtItsTime)
{
    // A program that reads the keyboard and writes the border all the time:
    // DI; loop: IN A,(#FE); OUT (#FE),A; JR loop
    const uint8_t program[] = {0xF3, 0xDB, 0xFE, 0xD3, 0xFE, 0x18, 0xFA};
    for (size_t i = 0; i < sizeof(program); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;
    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    ttd::TTDInputEvent key;
    key.kind = ttd::TTDInputKind::Key;
    key.key = 12;
    key.pressed = true;
    ASSERT_TRUE(_v1->SubmitLiveInput(key));
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    _v1->RecordExternalEvent(ttd::TTDExternalEventKind::TapeControl, "tape play (test)");
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    key.pressed = false;
    ASSERT_TRUE(_v1->SubmitLiveInput(key));
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    _v1->StopRecording();

    ASSERT_GE(_v1->GetInputJournal().Size(), 2u);
    ASSERT_EQ(_v1->GetExternalEvents().Size(), 1u);
    ASSERT_NO_FATAL_FAILURE(ttdtest::ExpectEventsEqualV1(
        _engine, _v1->GetInputJournal(), _v1->GetExternalEvents(), 0, 0, [this](uint64_t frame) {
            ttd::TTDMachineTime start = 0;
            EXPECT_TRUE(_engine.Frames().Start(frame, start)) << "frame " << frame;
            return start;
        }));
    EXPECT_EQ(_engine.Events().FirstBarrierIn(0, ~ttd::TTDMachineTime(0)), nullptr) << "a tape command is input";
    ASSERT_GT(_v1->GetPortReadJournal().Size(), 1000u) << "the program reads the keyboard";
    ASSERT_NO_FATAL_FAILURE(ttdtest::ExpectBusEqualV1(_engine, *_v1));
}

/// Phase 3, Step 3: machine time comes from the frame table. The Sprinter
/// switches between 320- and 312-line frames at a frame start (#2C / #2D;
/// here through its PLD state, which the screen applies at the next frame
/// start, as after the port write). Each frame starts where the last one
/// started plus its measured length, so machine time never goes back -
/// frame x the current length (v1's GlobalT) would - and each change of
/// length is a fact in the event log
TEST(TimeTravelManager_FrameTable_Test, FrameStartsFollowTheMeasuredFrameLengths)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("SPRINTER", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    auto* sprinter = dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder);
    ASSERT_NE(sprinter, nullptr);
    ttd::TimeTravelManager* v1 = context->pTimeTravelManager;
    ttd::TimeTravelEngine engine;

    v1->SetShadowEngine(&engine);
    ASSERT_TRUE(v1->StartRecording());
    emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    sprinter->GetPldState().frameLines = 1;   // 312 lines from the next frame start
    emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    sprinter->GetPldState().frameLines = 0;   // 320 again
    emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    v1->StopRecording();
    v1->SetShadowEngine(nullptr);

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
