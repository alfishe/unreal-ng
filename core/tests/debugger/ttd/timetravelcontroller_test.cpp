/// @file timetravelcontroller_test.cpp
/// @brief Phase 5, item 2: the engine's playback controller against v1. Two
/// machines run the same program with the same key presses; one records with
/// v1 (TimeTravelManager), the other with the controller (its own engine as
/// the store). Seeks to the same points - at frame starts and inside frames,
/// around the key events - must land both machines on the same state: CPU,
/// all RAM and every device.
///
/// Boots two machines and replays tens of frames: slower than the 50 ms
/// guideline, one acceptance check per scenario.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "loaders/snapshot/machinestatetransfer.h"
#include "emulator/ports/models/portdecoder_sprinter.h"

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

template <typename Session>
MachineState CaptureState(EmulatorContext* context, Session& session)
{
    MachineState s;
    Z80* z80 = context->pCore->GetZ80();
    s.cpu = ttd::CaptureCpuState(*z80);
    s.frame = context->emulatorState.frame_counter;
    s.t = z80->t;
    const size_t pages = session.GetModelRamPages();
    s.ram.assign(context->pMemory->RAMPageAddress(0), context->pMemory->RAMPageAddress(0) + pages * 0x4000);
    for (const auto& [id, device] : session.GetPeripheralRegistry().Devices())
        if (device && device->TTDStateSize() != 0)
            device->TTDSaveStateTo(s.devices[id]);
    return s;
}
}  // namespace

class TimeTravelController_Test : public ::testing::Test
{
protected:
    SoundCardScope _soundCards;
    Emulator* _a = nullptr;   ///< records with v1
    Emulator* _b = nullptr;   ///< records with the controller
    ttd::TimeTravelManager* _v1 = nullptr;
    std::unique_ptr<ttd::TimeTravelController> _controller;

    Emulator* StartMachine()
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        if (!emulator)
            return nullptr;
        EmulatorContext* context = emulator->GetContext();
        FeatureManager* features = emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        // The classic GS card: with NeoGS no port journals are kept (its ZX-DMA is not isolated)
        if (!FitGeneralSoundCard(context->pSoundManager, GSTypeKind::Z80))
            return nullptr;
        // A program whose memory follows the keyboard: DI; LD HL,#C000;
        // loop: LD A,#7F; IN A,(#FE); LD (HL),A; INC L; JR loop
        const uint8_t program[] = {0xF3, 0x21, 0x00, 0xC0, 0x3E, 0x7F, 0xDB, 0xFE, 0x77, 0x2C, 0x18, 0xF8};
        for (size_t i = 0; i < sizeof(program); ++i)
            context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        context->pCore->GetZ80()->pc = 0x8000;
        return emulator;
    }

    void SetUp() override
    {
        _a = StartMachine();
        _b = StartMachine();
        ASSERT_NE(_a, nullptr);
        ASSERT_NE(_b, nullptr);
        _v1 = _a->GetContext()->pTimeTravelManager;
        // Power-on RAM is randomized per machine: start both from the same bytes
        Memory* ma = _a->GetContext()->pMemory;
        Memory* mb = _b->GetContext()->pMemory;
        const size_t ramBytes = size_t(MAX_RAM_PAGES) * 0x4000;
        std::memcpy(mb->RAMPageAddress(0), ma->RAMPageAddress(0), ramBytes);
        // Machine B's core calls the controller (its own v1 manager stays idle)
        _controller = std::make_unique<ttd::TimeTravelController>(_b->GetContext());
        _b->GetContext()->pTimeTravelHooks = _controller.get();
        _b->GetContext()->ttdWriteSink = _controller.get();
    }

    void TearDown() override
    {
        if (_b)
        {
            _controller->StopRecording();
            _b->GetContext()->pTimeTravelHooks = _b->GetContext()->pTimeTravelManager;
            _b->GetContext()->ttdWriteSink = _b->GetContext()->pTimeTravelManager;
            _controller.reset();
            EmulatorTestHelper::CleanupEmulator(_b);
        }
        if (_a)
            EmulatorTestHelper::CleanupEmulator(_a);
    }

    /// The same run on both machines: 24 frames, space pressed and released mid-frame
    void RecordBoth(bool journal = false)
    {
        _v1->SetEnableWriteJournal(journal);
        _controller->SetEnableWriteJournal(journal);
        ASSERT_TRUE(_v1->StartRecording());
        ASSERT_TRUE(_controller->StartRecording());
        ttd::TTDInputEvent key;
        key.kind = ttd::TTDInputKind::Key;
        key.key = ZXKEY_SPACE;
        for (int f = 0; f < 24; ++f)
        {
            if (f == 5 || f == 13)
            {
                _a->RunTStates(20000, /*skipBreakpoints=*/true);   // mid-frame
                _b->RunTStates(20000, /*skipBreakpoints=*/true);
                key.pressed = f == 5;
                ASSERT_TRUE(_v1->SubmitLiveInput(key));
                ASSERT_TRUE(_controller->SubmitLiveInput(key));
            }
            _a->RunNFrames(1, /*skipBreakpoints=*/true);
            _b->RunNFrames(1, /*skipBreakpoints=*/true);
        }
        _v1->StopRecording();
        _controller->StopRecording();
    }

    void ExpectSameSeek(const ttd::TTDTimePoint& target)
    {
        SCOPED_TRACE("frame " + std::to_string(target.frame) + " T " + std::to_string(target.tInFrame));
        ttd::TTDSeekResult a, b;
        ASSERT_TRUE(_v1->SeekTo(target, &a));
        ASSERT_TRUE(_controller->SeekTo(target, &b));
        const MachineState fromV1 = CaptureState(_a->GetContext(), *_v1);
        const MachineState fromController = CaptureState(_b->GetContext(), *_controller);

        EXPECT_EQ(a.arrivedAt, b.arrivedAt);
        EXPECT_EQ(std::memcmp(&fromV1.cpu, &fromController.cpu, sizeof(fromV1.cpu)), 0) << "CPU";
        EXPECT_EQ(fromV1.frame, fromController.frame);
        EXPECT_EQ(fromV1.t, fromController.t);
        ASSERT_EQ(fromV1.ram.size(), fromController.ram.size());
        for (size_t i = 0; i < fromV1.ram.size(); ++i)
            if (fromV1.ram[i] != fromController.ram[i])
            {
                ADD_FAILURE() << "RAM differs at " << i << " (page " << i / 0x4000 << ")";
                break;
            }
        ASSERT_EQ(fromV1.devices.size(), fromController.devices.size());
        for (const auto& [id, bytes] : fromV1.devices)
            EXPECT_TRUE(fromController.devices.at(id) == bytes) << "device " << int(id);
    }
};

TEST_F(TimeTravelController_Test, SeeksLandOnV1sMachine)
{
    ASSERT_NO_FATAL_FAILURE(RecordBoth());
    ASSERT_EQ(_controller->GetCheckpointCount(), _v1->GetCheckpointCount());
    ASSERT_EQ(_controller->GetEngine().CheckpointCount(), _v1->GetCheckpointCount()) << "the controller's engine recorded";
    ASSERT_EQ(_controller->GetReplaySource(), &_controller->GetEngine()) << "seeks restore from the controller's engine";

    // Frames spread over the session, at three points inside each
    const size_t count = _v1->GetCheckpointCount();
    const uint32_t span = static_cast<uint32_t>(_v1->FrameSpan());
    for (size_t i : {size_t(1), count / 3, count / 2, count - 2})
    {
        const uint64_t frame = _v1->GetCheckpoint(i)->time.frame;
        for (uint32_t k = 1; k <= 3; ++k)
            ASSERT_NO_FATAL_FAILURE(ExpectSameSeek({frame, span * k / 4}));
    }
    // Around the key events
    for (const ttd::TTDInputEvent& e : _v1->GetInputJournal().Events())
        for (int32_t d : {-500, 1, 900})
            if (int64_t(e.time.tInFrame) + d > 0 && int64_t(e.time.tInFrame) + d < span)
                ASSERT_NO_FATAL_FAILURE(ExpectSameSeek({e.time.frame, static_cast<uint32_t>(int64_t(e.time.tInFrame) + d)}));
}

/// The queries answer from the same history the same way: the write journal and the
/// coverage index reach the controller through the core's sinks (C1d)
TEST_F(TimeTravelController_Test, QueriesAnswerAsV1)
{
    ASSERT_NO_FATAL_FAILURE(RecordBoth(/*journal=*/true));
    ASSERT_NE(_v1->GetWriteJournal(), nullptr);
    ASSERT_NE(_controller->GetWriteJournal(), nullptr);
    EXPECT_EQ(_controller->GetEngine().Writes().Size(), _v1->GetWriteJournal()->Size()) << "the controller got the writes";
    EXPECT_GT(_controller->GetEngine().Writes().Size(), 0u);
    EXPECT_EQ(_controller->GetWriteJournal()->Size(), 0u) << "the live ring was drained into the engine";
    EXPECT_EQ(_controller->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed),
              _v1->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed)) << "the controller got the coverage";

    const ttd::TTDTimePoint end = _v1->SessionEndPosition();
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_v1->SeekTo(end, &r));
    ASSERT_TRUE(_controller->SeekTo(end, &r));

    // Who wrote these addresses last (the program stores the keyboard row at #C000..)
    for (uint16_t addr : {uint16_t(0xC000), uint16_t(0xC010), uint16_t(0xC0FF)})
    {
        SCOPED_TRACE(addr);
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = addr;
        q.access = ttd::TTDAccessType::Write;
        const auto a = _v1->FindLastAccess(q);
        const auto b = _controller->FindLastAccess(q);
        ASSERT_EQ(a.has_value(), b.has_value());
        if (a)
        {
            EXPECT_EQ(a->time, b->time);
            EXPECT_EQ(a->pc, b->pc);
            EXPECT_EQ(a->value, b->value);
        }
    }

    // Which frames ran the loop
    const auto scanA = _v1->QueryCoverageScan(0, end.frame, ttd::TTDCoverageKind::Executed, 0x8000, 0x800B);
    const auto scanB = _controller->QueryCoverageScan(0, end.frame, ttd::TTDCoverageKind::Executed, 0x8000, 0x800B);
    EXPECT_TRUE(scanA.indexAvailable);
    EXPECT_EQ(scanA.indexAvailable, scanB.indexAvailable);
    EXPECT_EQ(scanA.frames, scanB.frames);

    // Back to the IN of the loop, then a few instructions back
    const auto rcA = _v1->ReverseContinue({0x8006});
    const auto rcB = _controller->ReverseContinue({0x8006});
    EXPECT_TRUE(rcA.matched);
    EXPECT_EQ(rcA.matched, rcB.matched);
    EXPECT_EQ(rcA.arrivedAt, rcB.arrivedAt);
    EXPECT_TRUE(_v1->ReverseStepInstructions(5));
    EXPECT_TRUE(_controller->ReverseStepInstructions(5));
    EXPECT_EQ(_v1->CurrentPosition(), _controller->CurrentPosition());
    const MachineState a = CaptureState(_a->GetContext(), *_v1);
    const MachineState b = CaptureState(_b->GetContext(), *_controller);
    EXPECT_EQ(std::memcmp(&a.cpu, &b.cpu, sizeof(a.cpu)), 0) << "CPU after reverse steps";
    EXPECT_TRUE(a.ram == b.ram) << "RAM after reverse steps";
}

/// A recording resumed from the past (C2): both go back into the middle of
/// frame 10, record on with a key pressed, and every seek - before the resume
/// point, at it and after it - lands on the same machine
TEST_F(TimeTravelController_Test, ResumeFromThePastContinuesAsV1)
{
    ASSERT_NO_FATAL_FAILURE(RecordBoth());
    const uint32_t span = static_cast<uint32_t>(_v1->FrameSpan());
    const ttd::TTDTimePoint from{_v1->GetCheckpoint(10)->time.frame, span / 3};
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_v1->SeekTo(from, &r));
    ASSERT_TRUE(_controller->SeekTo(from, &r));
    ASSERT_TRUE(_v1->ResumeRecordingFrom(from));
    ASSERT_TRUE(_controller->ResumeRecordingFrom(from));
    ttd::TTDInputEvent key;
    key.kind = ttd::TTDInputKind::Key;
    key.key = ZXKEY_SPACE;
    for (int f = 0; f < 10; ++f)
    {
        if (f == 4)
        {
            _a->RunTStates(15000, /*skipBreakpoints=*/true);
            _b->RunTStates(15000, /*skipBreakpoints=*/true);
            key.pressed = true;
            ASSERT_TRUE(_v1->SubmitLiveInput(key));
            ASSERT_TRUE(_controller->SubmitLiveInput(key));
        }
        _a->RunNFrames(1, /*skipBreakpoints=*/true);
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    _v1->StopRecording();
    _controller->StopRecording();

    const size_t count = _v1->GetCheckpointCount();
    ASSERT_EQ(_controller->GetCheckpointCount(), count);
    const ttd::TimeTravelEngine& engine = _controller->GetEngine();
    ASSERT_EQ(engine.CheckpointCount() - engine.FirstCheckpoint(), count) << "the engine holds the same history";
    for (size_t i : {size_t(5), size_t(10), size_t(11), size_t(14), count - 2})
    {
        const uint64_t frame = _v1->GetCheckpoint(i)->time.frame;
        for (uint32_t k = 1; k <= 3; ++k)
            ASSERT_NO_FATAL_FAILURE(ExpectSameSeek({frame, span * k / 4}));
    }
}

/// A frame limit (C2): v1 holds exactly the last 16 frames, the controller's
/// engine whole segments covering at least them; inside the window both land
/// on the same machine, and the journals start where each history starts
TEST_F(TimeTravelController_Test, HistoryLimitKeepsTheWindow)
{
    _v1->SetHistoryLimit(16, 0);
    _controller->SetHistoryLimit(16, 0);
    ASSERT_TRUE(_v1->StartRecording());
    ASSERT_TRUE(_controller->StartRecording());
    for (int f = 0; f < 48; ++f)
    {
        _a->RunNFrames(1, /*skipBreakpoints=*/true);
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    _v1->StopRecording();
    _controller->StopRecording();

    const ttd::TimeTravelEngine& engine = _controller->GetEngine();
    const size_t held = _controller->GetCheckpointCount();
    EXPECT_EQ(engine.CheckpointCount() - engine.FirstCheckpoint(), held);
    EXPECT_GE(held, 16u);
    EXPECT_LE(held, 16u + 2u) << "segments of an eighth of the window";
    EXPECT_GT(engine.FirstCheckpoint(), 0u) << "the oldest segments were dropped";
    EXPECT_EQ(_controller->GetCheckpoint(0)->time.frame, engine.Checkpoint(engine.FirstCheckpoint())->position.frame);
    EXPECT_EQ(_controller->SessionEndPosition(), _v1->SessionEndPosition());

    const uint32_t span = static_cast<uint32_t>(_v1->FrameSpan());
    const uint64_t last = _v1->SessionEndPosition().frame;
    for (uint64_t frame : {last - 14, last - 6, last - 1})
        ASSERT_NO_FATAL_FAILURE(ExpectSameSeek({frame, span / 2}));
    // D12: a seek before the earliest kept position names it and moves nothing
    const ttd::TTDTimePoint before = _controller->CurrentPosition();
    ttd::TTDSeekResult r;
    EXPECT_FALSE(_controller->SeekTo({last - 30, 0}, &r)) << "dropped history is not reachable";
    EXPECT_EQ(r.haltReason, ttd::TTDSeekHaltReason::OutOfRange);
    EXPECT_TRUE(r.beforeEarliest);
    EXPECT_EQ(r.earliest, _controller->GetCheckpoint(0)->time);
    EXPECT_EQ(r.arrivedAt, before);
    EXPECT_EQ(_controller->CurrentPosition(), before);
    EXPECT_EQ(_controller->GetSessionInfo().sessionStartFrame, r.earliest.frame);
}

/// The controller's time is the engine's frame table (C3): on a Sprinter that
/// switches to 312-line frames and back, every frame start is where the engine
/// put it and positions convert there and back
TEST(TimeTravelController_TimeBase_Test, MachineTimeFollowsTheFrameTable)
{
    SoundCardScope soundCards;
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("SPRINTER", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    auto* sprinter = dynamic_cast<PortDecoder_Sprinter*>(context->pPortDecoder);
    ASSERT_NE(sprinter, nullptr);
    auto controller = std::make_unique<ttd::TimeTravelController>(context);
    context->pTimeTravelHooks = controller.get();
    context->ttdWriteSink = controller.get();

    ASSERT_TRUE(controller->StartRecording());
    emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    sprinter->GetPldState().frameLines = 1;   // 312 lines from the next frame start
    emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    sprinter->GetPldState().frameLines = 0;
    emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    controller->StopRecording();

    const ttd::TTDFrameTable& frames = controller->GetEngine().Frames();
    ASSERT_GE(frames.Count(), 13u);
    uint64_t previous = 0;
    bool shorter = false;
    for (uint64_t f = frames.FirstFrame(); f <= frames.LastFrame(); ++f)
    {
        ttd::TTDMachineTime start = 0;
        ASSERT_TRUE(frames.Start(f, start));
        EXPECT_EQ(controller->GlobalT({f, 0}), start) << "frame " << f;
        if (f > frames.FirstFrame())
            shorter |= start - previous != controller->FrameSpan();
        previous = start;
        for (uint32_t t : {0u, 1000u, 50000u})
            EXPECT_EQ(controller->TimePointAt(controller->GlobalT({f, t})), (ttd::TTDTimePoint{f, t})) << "frame " << f;
    }
    EXPECT_TRUE(shorter) << "some frames are not FrameSpan() long: frame x span would misplace them";

    context->pTimeTravelHooks = context->pTimeTravelManager;
    context->ttdWriteSink = context->pTimeTravelManager;
    controller.reset();
    EmulatorTestHelper::CleanupEmulator(emulator);
}

namespace
{
/// Record 14 frames on both machines with a marker of @p kind in the middle of frame 8
void RecordWithMarker(Emulator* a, Emulator* b, ttd::TimeTravelManager* v1, ttd::TimeTravelController* controller,
                      ttd::TTDExternalEventKind kind)
{
    ASSERT_TRUE(v1->StartRecording());
    ASSERT_TRUE(controller->StartRecording());
    for (int f = 0; f < 14; ++f)
    {
        if (f == 8)
        {
            a->RunTStates(30000, /*skipBreakpoints=*/true);
            b->RunTStates(30000, /*skipBreakpoints=*/true);
            v1->RecordExternalEvent(kind, "outside");
            controller->RecordExternalEvent(kind, "outside");
        }
        a->RunNFrames(1, /*skipBreakpoints=*/true);
        b->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    v1->StopRecording();
    controller->StopRecording();
}

/// A search for code that never ran: it walks back until something stops it
ttd::TTDSearchQuery NeverExecuted()
{
    ttd::TTDSearchQuery q;
    q.access = ttd::TTDAccessType::Execute;
    q.addrFrom = q.addrTo = 0x9999;
    return q;
}
}  // namespace

/// Barriers in queries come from the engine's event log (C3): a marker the
/// replay cannot reproduce stops a backward search where v1's marker does,
/// named the same way
TEST_F(TimeTravelController_Test, QueriesStopAtTheSameBarrierAsV1)
{
    ASSERT_NO_FATAL_FAILURE(RecordWithMarker(_a, _b, _v1, _controller.get(), ttd::TTDExternalEventKind::Other));
    ttd::TTDExternalEvent markerA, markerB;
    ttd::TTDSearchWindow windowA, windowB;
    EXPECT_FALSE(_v1->FindLastAccess(NeverExecuted(), &markerA, &windowA).has_value());
    EXPECT_FALSE(_controller->FindLastAccess(NeverExecuted(), &markerB, &windowB).has_value());
    EXPECT_EQ(markerA.time, markerB.time);
    EXPECT_EQ(markerA.kind, markerB.kind);
    EXPECT_STREQ(markerA.reason, markerB.reason);
    EXPECT_EQ(windowA.from, windowB.from);
    EXPECT_EQ(windowA.to, windowB.to);
    EXPECT_GT(windowB.from.frame, _controller->GetCheckpoint(0)->time.frame) << "stopped at the marker";
}

/// Tape control is input the engine's replay applies, not a barrier: the
/// controller searches across it to the session start, where v1 stops
TEST_F(TimeTravelController_Test, TapeControlIsNoBarrierForTheController)
{
    ASSERT_NO_FATAL_FAILURE(RecordWithMarker(_a, _b, _v1, _controller.get(), ttd::TTDExternalEventKind::TapeControl));
    ttd::TTDExternalEvent markerA;
    ttd::TTDSearchWindow windowA, windowB;
    EXPECT_FALSE(_v1->FindLastAccess(NeverExecuted(), &markerA, &windowA).has_value());
    EXPECT_FALSE(_controller->FindLastAccess(NeverExecuted(), nullptr, &windowB).has_value());
    EXPECT_GT(windowA.from.frame, _v1->GetCheckpoint(0)->time.frame) << "v1 stops at the tape marker";
    EXPECT_EQ(windowB.from, _controller->GetCheckpoint(0)->time) << "the controller reaches the session start";
}

/// The write journal built afterwards by replay (C3c): recorded without it,
/// both build it for the whole session; the controller's engine index then
/// covers the session and answers find-last as v1's journal does
TEST_F(TimeTravelController_Test, BuiltJournalAnswersAsV1)
{
    ASSERT_NO_FATAL_FAILURE(RecordBoth(/*journal=*/false));
    const ttd::TTDJournalBuildResult a = _v1->BuildWriteJournal(0, UINT64_MAX);
    const ttd::TTDJournalBuildResult b = _controller->BuildWriteJournal(0, UINT64_MAX);
    ASSERT_TRUE(a.ok) << a.error;
    ASSERT_TRUE(b.ok) << b.error;
    EXPECT_EQ(a.framesBuilt, b.framesBuilt);
    EXPECT_EQ(a.records, b.records);
    EXPECT_GT(b.records, 0u);
    const ttd::TTDWriteIndex& writes = _controller->GetEngine().Writes();
    EXPECT_EQ(writes.Size(), b.records) << "the engine's index holds the built journal";
    ASSERT_EQ(writes.Segments().size(), 1u);

    const ttd::TTDTimePoint end = _v1->SessionEndPosition();
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_v1->SeekTo(end, &r));
    ASSERT_TRUE(_controller->SeekTo(end, &r));
    for (uint16_t addr : {uint16_t(0xC000), uint16_t(0xC07F), uint16_t(0xC0FF)})
    {
        SCOPED_TRACE(addr);
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = addr;
        q.access = ttd::TTDAccessType::Write;
        const auto x = _v1->FindLastAccess(q);
        const auto y = _controller->FindLastAccess(q);
        ASSERT_TRUE(x.has_value());
        ASSERT_TRUE(y.has_value());
        EXPECT_EQ(x->time, y->time);
        EXPECT_EQ(x->pc, y->pc);
        EXPECT_EQ(x->value, y->value);
        EXPECT_EQ(_controller->CurrentPosition(), end) << "answered from the index: the machine stays";
    }
}

/// Status from the engine (C4a): the recorded machine described as v1
/// describes it, the store's figures and key frames from the engine
TEST_F(TimeTravelController_Test, StatusDescribesTheSessionAsV1)
{
    ASSERT_NO_FATAL_FAILURE(RecordBoth());
    const ttd::TTDSessionInfo a = _v1->GetSessionInfo();
    const ttd::TTDSessionInfo b = _controller->GetSessionInfo();
    EXPECT_EQ(a.checkpointCount, b.checkpointCount);
    EXPECT_EQ(a.sessionStartFrame, b.sessionStartFrame);
    EXPECT_EQ(a.currentEndFrame, b.currentEndFrame);
    EXPECT_EQ(a.machine.peripheralMask, b.machine.peripheralMask) << "the same devices recorded";
    EXPECT_NE(b.machine.peripheralMask, 0u);
    EXPECT_EQ(a.inputEventCount, b.inputEventCount);

    const ttd::TimeTravelEngine& engine = _controller->GetEngine();
    EXPECT_EQ(b.pageStoreUsedBytes, engine.PieceStore().PayloadBytes());
    EXPECT_EQ(b.baselineFramesCaptured, engine.PieceStore().LiveVersions());
    EXPECT_GT(b.compressionRatio, 1.0);
    EXPECT_EQ(b.keyFrameCount, engine.Segments().size()) << "a key frame starts each segment";
    EXPECT_EQ(b.keyFrameCount + b.deltaFrameCount, b.checkpointCount);
    EXPECT_GE(_controller->GetHeapBreakdown().Total(), engine.HeapBreakdown().Total()) << "the engine's memory is counted";
}

/// A saved session loads back (C4b): the engine's file with the controller's
/// facts, coverage and bookmarks. Seeks into the loaded session land where
/// v1's in-memory session lands, a v1 file is refused with the reason, and
/// the loaded session continues: resumed from frame 12, both record on and
/// seek alike
TEST_F(TimeTravelController_Test, ASavedSessionLoadsAndContinuesAsV1)
{
    ASSERT_NO_FATAL_FAILURE(RecordBoth(/*journal=*/true));
    std::string err;
    ASSERT_TRUE(_controller->AddBookmark(_v1->GetCheckpoint(7)->time, "seven", &err)) << err;
    const size_t coveredFrames = _controller->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed);
    ASSERT_GT(coveredFrames, 0u);
    std::stringstream file;
    ASSERT_TRUE(_controller->SerializeSession(file, err)) << err;

    std::stringstream v1File;
    ASSERT_TRUE(_v1->SerializeSession(v1File, err)) << err;
    EXPECT_FALSE(_controller->DeserializeSession(v1File, err));
    EXPECT_NE(err.find("v1 .ttd file"), std::string::npos) << err;

    _controller->InvalidateSession("test");
    ASSERT_EQ(_controller->GetCheckpointCount(), 0u);
    ASSERT_TRUE(_controller->DeserializeSession(file, err)) << err;
    EXPECT_EQ(_controller->GetCheckpointCount(), _v1->GetCheckpointCount());
    EXPECT_TRUE(_controller->GetSessionInfo().loadedFromFile);
    EXPECT_EQ(_controller->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed), coveredFrames);
    ASSERT_EQ(_controller->GetBookmarks().size(), 1u);
    EXPECT_EQ(_controller->GetBookmarks()[0].label, "seven");
    EXPECT_EQ(_controller->GetInputJournal().Size(), _v1->GetInputJournal().Size()) << "the key events, as v1 counts them";

    const uint32_t span = static_cast<uint32_t>(_v1->FrameSpan());
    for (size_t i : {size_t(3), size_t(14), _v1->GetCheckpointCount() - 2})
        ASSERT_NO_FATAL_FAILURE(ExpectSameSeek({_v1->GetCheckpoint(i)->time.frame, span / 2}));
    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = 0xC010;
    q.access = ttd::TTDAccessType::Write;
    const auto a = _v1->FindLastAccess(q);
    const auto b = _controller->FindLastAccess(q);
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->time, b->time) << "the write journal came back";

    // Continue the loaded session from frame 12
    const ttd::TTDTimePoint from{_v1->GetCheckpoint(12)->time.frame, span / 3};
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_v1->SeekTo(from, &r));
    ASSERT_TRUE(_controller->SeekTo(from, &r));
    ASSERT_TRUE(_v1->ResumeRecordingFrom(from));
    ASSERT_TRUE(_controller->ResumeRecordingFrom(from)) << "a loaded session continues";
    for (int f = 0; f < 6; ++f)
    {
        _a->RunNFrames(1, /*skipBreakpoints=*/true);
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    _v1->StopRecording();
    _controller->StopRecording();
    ASSERT_EQ(_controller->GetCheckpointCount(), _v1->GetCheckpointCount());
    for (size_t i : {size_t(5), size_t(13), _v1->GetCheckpointCount() - 2})
        ASSERT_NO_FATAL_FAILURE(ExpectSameSeek({_v1->GetCheckpoint(i)->time.frame, span / 2}));
    // The write journal lost the old future and holds the new one
    EXPECT_EQ(_controller->GetEngine().Writes().Size(), _v1->GetWriteJournal()->Size());
    for (uint16_t addr : {uint16_t(0xC010), uint16_t(0xC0F0)})
    {
        q.addrFrom = q.addrTo = addr;
        const auto x = _v1->FindLastAccess(q);
        const auto y = _controller->FindLastAccess(q);
        ASSERT_TRUE(x && y);
        EXPECT_EQ(x->time, y->time) << addr;
        EXPECT_EQ(x->value, y->value) << addr;
    }
}

/// A session file on disk (C4b): file-info describes the recorded machine as
/// v1's file does without loading it, and a port search of the file finds
/// what the session itself finds
TEST_F(TimeTravelController_Test, FileInfoAndPortSearchReadTheEngineFile)
{
    ASSERT_NO_FATAL_FAILURE(RecordBoth());
    std::string err;
    ASSERT_TRUE(_controller->AddBookmark(_v1->GetCheckpoint(4)->time, "four", &err)) << err;
    const std::string ours = TestPathHelper::GetUniqueTestScratchPath("controller-session.ttd");
    const std::string theirs = TestPathHelper::GetUniqueTestScratchPath("v1-session.ttd");
    {
        std::ofstream out(FileHelper::ToFsPath(ours), std::ios::binary);
        ASSERT_TRUE(_controller->SerializeSession(out, err)) << err;
        std::ofstream v1Out(FileHelper::ToFsPath(theirs), std::ios::binary);
        ASSERT_TRUE(_v1->SerializeSession(v1Out, err)) << err;
    }
    ttd::TTDFileInfo a, b;
    ASSERT_TRUE(ttd::ReadTTDFileInfo(theirs, a, err)) << err;
    ASSERT_TRUE(ttd::ReadTTDFileInfo(ours, b, err)) << err;
    EXPECT_EQ(b.schemaVersion, 2u);
    EXPECT_EQ(a.machine.modelId, b.machine.modelId);
    EXPECT_EQ(a.machine.model, b.machine.model);
    EXPECT_EQ(a.machine.peripheralMask, b.machine.peripheralMask);
    EXPECT_EQ(a.machine.peripherals, b.machine.peripherals);
    EXPECT_EQ(a.machine.romSignature, b.machine.romSignature);
    EXPECT_EQ(a.machine.generalSound, b.machine.generalSound);
    EXPECT_EQ(a.startFrame, b.startFrame);
    EXPECT_EQ(a.endFrame, b.endFrame);
    EXPECT_EQ(a.checkpointCount, b.checkpointCount);
    EXPECT_EQ(a.hasPortJournals, b.hasPortJournals);
    EXPECT_TRUE(b.hasCoverageIndex);
    EXPECT_TRUE(b.hasBookmarks);
    EXPECT_GT(b.fileBytes, 0u);

    // Every IN of the keyboard row, in the file and in the session
    ttd::TTDPortQuery q;
    q.direction = ttd::TTDPortJournal::Direction::Read;
    q.portMask = 0x00FF;
    q.portValue = 0x00FE;
    const ttd::TTDPortSearchResult live = _controller->SearchPortEvents(q);
    const ttd::TTDPortSearchResult file = _controller->SearchPortEventsInFile(ours, q);
    ASSERT_TRUE(live.ok) << live.error;
    ASSERT_TRUE(file.ok) << file.error;
    EXPECT_GT(live.hits.size(), 0u);
    EXPECT_EQ(live.hits.size(), file.hits.size());
    EXPECT_EQ(live.scanned, file.scanned);
    const ttd::TTDPortSearchResult refused = _controller->SearchPortEventsInFile(theirs, q);
    EXPECT_FALSE(refused.ok);
    EXPECT_NE(refused.error.find("v1 .ttd file"), std::string::npos) << refused.error;
    std::error_code ec;
    std::filesystem::remove(FileHelper::ToFsPath(ours), ec);
    std::filesystem::remove(FileHelper::ToFsPath(theirs), ec);
}

/// Clip export walks the session on the engine (C4c): every frame's composed
/// picture and its latches equal v1's
TEST_F(TimeTravelController_Test, ClipFramesEqualV1s)
{
    // The loop sends the refresh register to the border (LD A,R; OUT (#FE),A
    // in place of the keyboard read): the border stripes differ every frame
    for (Emulator* emulator : {_a, _b})
    {
        emulator->GetFeatureManager()->setFeature(Features::kScreenHQ, true);   // the picture is drawn as the frame runs
        Memory* memory = emulator->GetContext()->pMemory;
        const uint8_t patch[] = {0xED, 0x5F, 0xD3, 0xFE};
        for (uint16_t i = 0; i < 4; ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8004 + i), patch[i]);
    }
    ASSERT_NO_FATAL_FAILURE(RecordBoth());
    const uint64_t first = _v1->GetCheckpoint(2)->time.frame;
    const uint64_t last = _v1->GetCheckpoint(_v1->GetCheckpointCount() - 2)->time.frame;
    std::vector<std::vector<uint8_t>> a, b;
    std::vector<uint32_t> latchesA, latchesB;
    auto collect = [](std::vector<std::vector<uint8_t>>& pictures, std::vector<uint32_t>& latches) {
        return [&pictures, &latches](const ttd::TTDComposedFrame& f) {
            pictures.emplace_back(f.rgba, f.rgba + f.rgbaBytes);
            latches.push_back(uint32_t(f.p7FFD) | uint32_t(f.border) << 8 | uint32_t(f.activeScreen) << 16);
            return true;
        };
    };
    EXPECT_EQ(_v1->VisitComposedFrames(first, last, collect(a, latchesA)), "");
    EXPECT_EQ(_controller->VisitComposedFrames(first, last, collect(b, latchesB)), "");
    ASSERT_EQ(a.size(), last - first + 1);
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(latchesA, latchesB);
    EXPECT_FALSE(a.front() == a.back()) << "the pictures change over the clip";
    for (size_t i = 0; i < a.size(); ++i)
        EXPECT_TRUE(a[i] == b[i]) << "frame " << first + i;
    EXPECT_NE(_controller->VisitComposedFrames(last, last + 50, collect(b, latchesB)), "") << "past the session";
}

/// D8: a seek while recording pauses the recording; resumed where it paused it
/// goes on as one recording - the same history as v1 recording straight through
TEST_F(TimeTravelController_Test, APausedRecordingContinuesWhereItPaused)
{
    ASSERT_TRUE(_v1->StartRecording());
    ASSERT_TRUE(_controller->StartRecording());
    ttd::TTDInputEvent key;
    key.kind = ttd::TTDInputKind::Key;
    key.key = ZXKEY_SPACE;
    auto frames = [&](int count, bool pressAt3) {
        for (int f = 0; f < count; ++f)
        {
            if (pressAt3 && f == 3)
            {
                _a->RunTStates(20000, /*skipBreakpoints=*/true);
                _b->RunTStates(20000, /*skipBreakpoints=*/true);
                key.pressed = !key.pressed;
                ASSERT_TRUE(_v1->SubmitLiveInput(key));
                ASSERT_TRUE(_controller->SubmitLiveInput(key));
            }
            _a->RunNFrames(1, /*skipBreakpoints=*/true);
            _b->RunNFrames(1, /*skipBreakpoints=*/true);
        }
    };
    ASSERT_NO_FATAL_FAILURE(frames(10, true));
    // Mid-frame, then a look back on the controller only
    _a->RunTStates(31000, /*skipBreakpoints=*/true);
    _b->RunTStates(31000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint pausedAt = _controller->CurrentPosition();
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(4)->time.frame, 9000}, &r));
    EXPECT_EQ(_controller->GetState(), ttd::TTDSessionState::Detached);
    EXPECT_TRUE(_controller->GetSessionInfo().recordingPaused);
    EXPECT_EQ(_controller->SessionEndPosition(), pausedAt) << "the paused recording reaches to where it paused";
    ASSERT_TRUE(_controller->ResumeRecordingLive()) << "resume goes on where it paused";
    EXPECT_TRUE(_controller->IsRecording());
    EXPECT_FALSE(_controller->GetSessionInfo().recordingPaused);
    EXPECT_EQ(_controller->CurrentPosition(), pausedAt);
    ASSERT_NO_FATAL_FAILURE(frames(10, true));
    _v1->StopRecording();
    _controller->StopRecording();

    ASSERT_EQ(_controller->GetCheckpointCount(), _v1->GetCheckpointCount());
    const uint32_t span = static_cast<uint32_t>(_v1->FrameSpan());
    for (size_t i : {size_t(5), size_t(10), size_t(11), size_t(14), _v1->GetCheckpointCount() - 2})
        ASSERT_NO_FATAL_FAILURE(ExpectSameSeek({_v1->GetCheckpoint(i)->time.frame, span / 2}));
    ASSERT_NO_FATAL_FAILURE(ExpectSameSeek(pausedAt));
}

/// D8: running forward through a paused recording, execution reaches where it
/// paused and the recording goes on by itself - one history, as v1's straight run
TEST_F(TimeTravelController_Test, RunningIntoThePausedEndContinuesTheRecording)
{
    ASSERT_TRUE(_v1->StartRecording());
    ASSERT_TRUE(_controller->StartRecording());
    for (int f = 0; f < 12; ++f)
    {
        _a->RunNFrames(1, /*skipBreakpoints=*/true);
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    _a->RunTStates(25000, /*skipBreakpoints=*/true);
    _b->RunTStates(25000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint pausedAt = _controller->CurrentPosition();
    const uint64_t backFrame = _controller->GetCheckpoint(6)->time.frame;
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_controller->SeekTo({backFrame, 0}, &r));
    ASSERT_TRUE(_controller->GetSessionInfo().recordingPaused);
    // From frame 6 the controller's machine runs 14 frames: 6 replayed, then live and recorded
    _b->RunNFrames(14, /*skipBreakpoints=*/true);
    EXPECT_TRUE(_controller->IsRecording()) << "the recording went on at the paused end";
    EXPECT_FALSE(_controller->GetSessionInfo().recordingPaused);
    // v1 ran straight to the same frame
    const uint64_t target = _b->GetContext()->emulatorState.frame_counter;
    while (_a->GetContext()->emulatorState.frame_counter < target)
        _a->RunNFrames(1, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    _controller->StopRecording();
    EXPECT_GT(_controller->SessionEndPosition().frame, pausedAt.frame);

    ASSERT_EQ(_controller->GetCheckpointCount(), _v1->GetCheckpointCount());
    const uint32_t span = static_cast<uint32_t>(_v1->FrameSpan());
    for (size_t i : {size_t(3), size_t(12), size_t(13), size_t(16), _v1->GetCheckpointCount() - 2})
        ASSERT_NO_FATAL_FAILURE(ExpectSameSeek({_v1->GetCheckpoint(i)->time.frame, span / 3}));
}

/// D8: a reverse query while recording pauses the recording even when it moves
/// nothing (answered from the write journal): the machine is in the history at
/// its end, and running on goes on recording
TEST_F(TimeTravelController_Test, AQueryWhileRecordingPausesAndRunningGoesOn)
{
    _controller->SetEnableWriteJournal(true);
    ASSERT_TRUE(_controller->StartRecording());
    for (int f = 0; f < 6; ++f)
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint here = _controller->CurrentPosition();
    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = 0xC010;
    q.access = ttd::TTDAccessType::Write;
    ASSERT_TRUE(_controller->FindLastAccess(q).has_value());
    EXPECT_EQ(_controller->GetState(), ttd::TTDSessionState::Detached);
    EXPECT_TRUE(_controller->GetSessionInfo().recordingPaused);
    EXPECT_EQ(_controller->CurrentPosition(), here) << "answered from the journal: the machine stays";
    const size_t before = _controller->GetCheckpointCount();
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    EXPECT_TRUE(_controller->IsRecording()) << "running on from the paused end records";
    EXPECT_EQ(_controller->GetCheckpointCount(), before + 3);
}

/// D9 at the present: a tool's edit while recording (registers, memory, the
/// paging latch) is an event the replay applies. The history before it stays,
/// and a seek past it lands on what the machine did live after it
TEST_F(TimeTravelController_Test, AnEditWhileRecordingIsReplayed)
{
    EmulatorContext* context = _b->GetContext();
    Z80* z80 = context->pCore->GetZ80();
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(6, /*skipBreakpoints=*/true);
    _b->RunTStates(20000, /*skipBreakpoints=*/true);
    const uint64_t firstFrame = _controller->GetCheckpoint(0)->time.frame;
    const size_t before = _controller->GetCheckpointCount();

    // The loop stores through HL: the edit moves it into #C000.., which the
    // paging latch now maps to page 3, and pokes the program's data area
    _b->EditMemoryFromTool("test edit", [&]() {
        z80->hl = 0xC123;
        context->pMemory->DirectWriteToZ80Memory(0x8100, 0x5A);
        context->pMemory->SetRAMPageToBank3(3, true);
    });
    EXPECT_TRUE(_controller->IsRecording()) << "the edit does not restart anything";
    EXPECT_EQ(_controller->GetCheckpointCount(), before);
    EXPECT_EQ(_controller->GetCheckpoint(0)->time.frame, firstFrame) << "the history before the edit stays";

    _b->RunTStates(10000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint afterEdit = _controller->CurrentPosition();
    const MachineState liveAfterEdit = CaptureState(context, *_controller);
    const uint8_t latchAfterEdit = context->emulatorState.p7FFD;
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    _b->RunTStates(15000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint later = _controller->CurrentPosition();
    const MachineState liveLater = CaptureState(context, *_controller);
    _controller->StopRecording();

    auto expectSeekShows = [&](const ttd::TTDTimePoint& at, const MachineState& live, const char* where) {
        ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(2)->time.frame, 0}, nullptr));
        ASSERT_TRUE(_controller->SeekTo(at, nullptr)) << where;
        const MachineState seen = CaptureState(context, *_controller);
        EXPECT_EQ(std::memcmp(&seen.cpu, &live.cpu, sizeof(seen.cpu)), 0)
            << where << ": CPU (HL " << seen.cpu.hl << " vs " << live.cpu.hl << ")";
        EXPECT_TRUE(seen.ram == live.ram) << where << ": RAM";
        EXPECT_TRUE(seen.devices == live.devices) << where << ": devices";
    };
    ASSERT_NO_FATAL_FAILURE(expectSeekShows(afterEdit, liveAfterEdit, "inside the edit's frame"));
    EXPECT_EQ(context->emulatorState.p7FFD, latchAfterEdit) << "the paging latch the edit set";
    ASSERT_NO_FATAL_FAILURE(expectSeekShows(later, liveLater, "frames after the edit"));
}

/// D9 with D8: an edit at the paused end continues the paused recording and is
/// recorded; one before it ends the recording where it paused (branches: Step 2b)
TEST_F(TimeTravelController_Test, AnEditWhilePausedContinuesAtTheEndOrEndsTheRecording)
{
    EmulatorContext* context = _b->GetContext();
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(6, /*skipBreakpoints=*/true);
    _b->RunTStates(20000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint pausedAt = _controller->CurrentPosition();

    // At the paused end
    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(2)->time.frame, 0}, nullptr));
    ASSERT_TRUE(_controller->SeekTo(pausedAt, nullptr));
    ASSERT_TRUE(_controller->GetSessionInfo().recordingPaused);
    _b->EditMemoryFromTool("test edit", [&]() { context->pMemory->DirectWriteToZ80Memory(0x8100, 0x5A); });
    EXPECT_TRUE(_controller->IsRecording()) << "the paused recording goes on with the edit";
    EXPECT_FALSE(_controller->GetSessionInfo().recordingPaused);
    _b->RunTStates(5000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint afterEdit = _controller->CurrentPosition();
    const MachineState live = CaptureState(context, *_controller);

    // Before the paused end
    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(2)->time.frame, 0}, nullptr));
    ASSERT_TRUE(_controller->GetSessionInfo().recordingPaused);
    const size_t kept = _controller->GetCheckpointCount();
    _b->EditMemoryFromTool("test edit", [&]() { context->pMemory->DirectWriteToZ80Memory(0x8101, 0xA5); });
    EXPECT_FALSE(_controller->IsRecording());
    EXPECT_FALSE(_controller->GetSessionInfo().recordingPaused) << "the recording ended where it paused";
    EXPECT_EQ(_controller->GetCheckpointCount(), kept) << "and its history stays";
    const size_t afterStop = _controller->GetCheckpointCount();
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    EXPECT_EQ(_controller->GetCheckpointCount(), afterStop) << "running from the edited past records nothing";

    // The first edit is in the history
    ASSERT_TRUE(_controller->SeekTo(afterEdit, nullptr));
    const MachineState seen = CaptureState(context, *_controller);
    EXPECT_EQ(std::memcmp(&seen.cpu, &live.cpu, sizeof(seen.cpu)), 0);
    EXPECT_TRUE(seen.ram == live.ram);
}

/// D10: a snapshot load while recording is part of the recording. The machine
/// finishes its frame, the load happens at the boundary and that checkpoint
/// holds the loaded state; machine time goes on. Seeks before and after it
/// restore what ran, and running forward from before it takes the loaded
/// state at the boundary as the recording did
TEST_F(TimeTravelController_Test, ASnapshotLoadWhileRecordingIsPartOfTheRecording)
{
    EmulatorContext* context = _b->GetContext();
    Memory* memory = context->pMemory;
    Z80* z80 = context->pCore->GetZ80();
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    memory->DirectWriteToZ80Memory(0x8100, 0x11);
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("d10-snapshot.z80");
    ASSERT_TRUE(_b->SaveSnapshot(path));
    const uint16_t snapshotPc = z80->pc;
    memory->DirectWriteToZ80Memory(0x8100, 0x22);
    _b->RunNFrames(2, /*skipBreakpoints=*/true);

    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(5, /*skipBreakpoints=*/true);
    _b->RunTStates(20000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint beforeLoad = _controller->CurrentPosition();
    const MachineState liveBeforeLoad = CaptureState(context, *_controller);
    const ttd::TTDTimePoint first = _controller->GetCheckpoint(0)->time;
    const size_t checkpoints = _controller->GetCheckpointCount();

    ASSERT_TRUE(_b->LoadSnapshot(path));
    EXPECT_TRUE(_controller->IsRecording()) << "the load does not end the recording";
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x8100), 0x11) << "the snapshot is loaded";
    const uint64_t cutFrame = beforeLoad.frame + 1;
    EXPECT_EQ(context->emulatorState.frame_counter, cutFrame) << "at the next frame boundary, machine time going on";
    ASSERT_EQ(_controller->GetCheckpointCount(), checkpoints + 1);
    EXPECT_EQ(_controller->GetCheckpoint(checkpoints)->time.frame, cutFrame);
    EXPECT_EQ(_controller->GetCheckpoint(0)->time, first) << "the history before the load stays";

    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    _b->RunTStates(15000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint afterLoad = _controller->CurrentPosition();
    const MachineState liveAfterLoad = CaptureState(context, *_controller);
    _controller->StopRecording();

    auto expectSame = [&](const MachineState& seen, const MachineState& live, const char* where) {
        EXPECT_EQ(std::memcmp(&seen.cpu, &live.cpu, sizeof(seen.cpu)), 0) << where << ": CPU (pc " << seen.cpu.pc
                                                                          << " vs " << live.cpu.pc << ")";
        EXPECT_TRUE(seen.ram == live.ram) << where << ": RAM";
        EXPECT_TRUE(seen.devices == live.devices) << where << ": devices";
    };
    ASSERT_TRUE(_controller->SeekTo(afterLoad, nullptr));
    expectSame(CaptureState(context, *_controller), liveAfterLoad, "a seek after the load");
    ASSERT_TRUE(_controller->SeekTo(beforeLoad, nullptr));
    expectSame(CaptureState(context, *_controller), liveBeforeLoad, "a seek before the load");
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x8100), 0x22);
    ASSERT_TRUE(_controller->SeekTo({cutFrame, 0}, nullptr));
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x8100), 0x11) << "the boundary holds the loaded state";
    EXPECT_EQ(z80->pc, snapshotPc);

    // The machine playing the history forward from before the load crosses it as recorded
    // (the recording ran the rest of the load's frame first)
    ASSERT_TRUE(_controller->SeekTo(beforeLoad, nullptr));
    _b->RunTStates(_controller->FrameSpan() - z80->t, /*skipBreakpoints=*/true);
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    _b->RunTStates(15000, /*skipBreakpoints=*/true);
    EXPECT_EQ(_controller->CurrentPosition(), afterLoad);
    expectSame(CaptureState(context, *_controller), liveAfterLoad, "running across the load");
}

/// D10 outside a running recording: at a paused recording's end the load
/// continues it and is recorded; anywhere else the history stays and the
/// machine leaves it (a paused recording ends where it paused)
TEST_F(TimeTravelController_Test, ASnapshotLoadKeepsTheHistory)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("d10-keep.z80");
    ASSERT_TRUE(_b->SaveSnapshot(path));
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(5, /*skipBreakpoints=*/true);
    _b->RunTStates(20000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint pausedAt = _controller->CurrentPosition();

    // At the paused end: the recording goes on with the load
    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(2)->time.frame, 0}, nullptr));
    ASSERT_TRUE(_controller->SeekTo(pausedAt, nullptr));
    ASSERT_TRUE(_controller->GetSessionInfo().recordingPaused);
    const size_t before = _controller->GetCheckpointCount();
    ASSERT_TRUE(_b->LoadSnapshot(path));
    EXPECT_TRUE(_controller->IsRecording());
    EXPECT_EQ(_controller->GetCheckpointCount(), before + 1) << "the load's boundary";

    // In the past: the recording ends where it paused, the history stays
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(2)->time.frame, 0}, nullptr));
    const size_t kept = _controller->GetCheckpointCount();
    ASSERT_TRUE(_b->LoadSnapshot(path));
    EXPECT_FALSE(_controller->IsRecording());
    EXPECT_FALSE(_controller->GetSessionInfo().recordingPaused);
    EXPECT_EQ(_controller->GetState(), ttd::TTDSessionState::Idle) << "the machine left the history";
    EXPECT_EQ(_controller->GetCheckpointCount(), kept);

    // Idle with history: the history stays
    ASSERT_TRUE(_b->LoadSnapshot(path));
    ASSERT_EQ(_controller->GetCheckpointCount(), kept);
    ttd::TTDSeekResult r;
    EXPECT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(3)->time.frame, 0}, &r)) << "still browsable";
}

/// D10 on the surfaces' guard: on the engine a snapshot load is not refused
/// while recording, a switch of the machine model is (D26: the history belongs
/// to this machine) - a machine state transfer into a recording machine
TEST_F(TimeTravelController_Test, WhileRecordingASnapshotLoadIsAllowedAModelSwitchIsNot)
{
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    EXPECT_TRUE(_b->RecordingGuard(ttd::TTDGuardedAction::LoadSnapshot).empty());
    const std::string refusal = _b->RecordingGuard(ttd::TTDGuardedAction::SwitchModel);
    EXPECT_NE(refusal.find("Stop the recording first"), std::string::npos) << refusal;

    const MachineStateTransfer::Report report = MachineStateTransfer::Transfer(*_a, *_b, {});
    EXPECT_FALSE(report.ok);
    EXPECT_EQ(report.reason, refusal);
    EXPECT_TRUE(_controller->IsRecording());
}

/// Step 3 (D29): the black box records without holding the machine at real
/// speed. Before an acceleration (turbo mode, host speed) takes effect it
/// stops, its history kept; once nothing accelerates a new session starts. The
/// fast loaders do not stop it (their traps are recorded edits). It keeps the
/// last N minutes. An explicit recording still refuses accelerations
TEST_F(TimeTravelController_Test, TheBlackBoxStepsAsideForAccelerations)
{
    FeatureManager* features = _b->GetFeatureManager();
    _controller->SetBlackBox(true, 1);
    ASSERT_TRUE(_controller->StartRecording());
    const unsigned frameMicros = _b->GetContext()->config.frame_duration_us;
    EXPECT_EQ(_controller->GetSessionInfo().historyLimitFrames, uint64_t(60) * 1000000 / frameMicros)
        << "one minute of frames";
    _b->RunNFrames(4, /*skipBreakpoints=*/true);

    // A fast loader: allowed, the black box goes on
    ASSERT_TRUE(features->setFeature(Features::kFastTape, true));
    EXPECT_TRUE(features->isEnabled(Features::kFastTape)) << "not masked by a black box";
    EXPECT_TRUE(_controller->IsRecording());

    // Turbo: allowed; the black box stops first and keeps its history
    ASSERT_TRUE(features->setFeature(Features::kTurboMode, true));
    EXPECT_TRUE(_b->GetContext()->pCore->IsTurboMode());
    EXPECT_FALSE(_controller->IsRecording());
    EXPECT_EQ(_controller->GetSessionInfo().lastStopReason, "acceleration");
    EXPECT_GE(_controller->GetCheckpointCount(), 4u) << "the history before the acceleration stays";
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    EXPECT_GE(_controller->GetCheckpointCount(), 4u);

    // Off again: a new session
    ASSERT_TRUE(features->setFeature(Features::kTurboMode, false));
    EXPECT_TRUE(_controller->IsRecording()) << "records again once nothing accelerates";
    EXPECT_EQ(_controller->GetCheckpointCount(), 1u) << "a new session: its baseline";

    // Host speed the same way
    EXPECT_TRUE(_b->SetSpeedMultiplier(4));
    EXPECT_FALSE(_controller->IsRecording());
    EXPECT_EQ(_b->GetContext()->pCore->GetHostSpeedMultiplier(), 4);
    EXPECT_EQ(_controller->GetCheckpointCount(), 1u) << "kept, not invalidated";
    EXPECT_TRUE(_b->SetSpeedMultiplier(1));
    EXPECT_TRUE(_controller->IsRecording());

    // An explicit recording holds the lock
    _controller->StopRecording();
    _controller->SetBlackBox(false);
    ASSERT_TRUE(features->setFeature(Features::kFastTape, false));
    ASSERT_TRUE(_controller->StartRecording());
    EXPECT_FALSE(features->setFeature(Features::kTurboMode, true)) << "refused while an explicit recording runs";
    EXPECT_FALSE(_b->SetSpeedMultiplier(4));
    EXPECT_TRUE(_controller->IsRecording());
}

/// Step 3 (D29) with the fast loaders: in a black box a fast tape load's trap
/// is an edit with the block's bytes, the registers, the tape cursor and the
/// time it spent; a seek across it, and the machine playing the history
/// forward, land where the machine was live
TEST_F(TimeTravelController_Test, AFastTapeTrapInTheBlackBoxIsReplayed)
{
    EmulatorContext* context = _b->GetContext();
    Memory* memory = context->pMemory;
    Z80* z80 = context->pCore->GetZ80();

    // One TAP block: flag #FF and 256 bytes
    std::vector<uint8_t> block(258);
    block[0] = 0xFF;
    uint8_t checksum = 0xFF;
    for (size_t i = 1; i <= 256; ++i)
    {
        block[i] = static_cast<uint8_t>(i * 7 + 3);
        checksum ^= block[i];
    }
    block[257] = checksum;
    const std::string tap = TestPathHelper::GetUniqueTestScratchPath("blackbox-trap.tap");
    {
        std::ofstream out(tap, std::ios::binary);
        const uint8_t length[2] = {static_cast<uint8_t>(block.size() & 0xFF), static_cast<uint8_t>(block.size() >> 8)};
        out.write(reinterpret_cast<const char*>(length), 2);
        out.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size()));
    }
    ASSERT_TRUE(_b->LoadTape(tap));

    // The ROM's LD-BYTES entry as the trap recognizes it
    uint8_t* rom = memory->GetPhysicalAddressForZ80Page(0);
    const uint8_t signature1[8] = {0x14, 0x08, 0x15, 0xF3, 0x3E, 0x0F, 0xD3, 0xFE};
    const uint8_t signature2[3] = {0xDB, 0xFE, 0x1F};
    std::memcpy(rom + 0x0556, signature1, sizeof(signature1));
    std::memcpy(rom + 0x0556 + 12, signature2, sizeof(signature2));

    // A wait of about three frames (LD BC,8000; w: DEC BC; LD A,B; OR C; JR NZ,w), then
    // LD A,#FF; LD DE,256; LD IX,#9000; SCF; CALL #0556; LD HL,0; loop: INC HL; JR loop
    const uint8_t program[] = {0x01, 0x40, 0x1F, 0x0B, 0x78, 0xB1, 0x20, 0xFB, 0x3E, 0xFF, 0x11, 0x00, 0x01, 0xDD,
                               0x21, 0x00, 0x90, 0x37, 0xCD, 0x56, 0x05, 0x21, 0x00, 0x00, 0x23, 0x18, 0xFD};
    for (size_t i = 0; i < sizeof(program); ++i)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8100 + i), program[i]);
    z80->pc = 0x8100;
    z80->sp = 0xBF00;

    ASSERT_TRUE(_b->GetFeatureManager()->setFeature(Features::kFastTape, true));
    _controller->SetBlackBox(true, 5);
    ASSERT_TRUE(_controller->StartRecording());
    const size_t events = _controller->GetSessionInfo().externalEventCount;
    _b->RunTStates(3 * 71680 + 30000, /*skipBreakpoints=*/true);
    ASSERT_EQ(memory->DirectReadFromZ80Memory(0x9000), block[1]) << "the trap loaded the block";
    ASSERT_EQ(_controller->GetSessionInfo().externalEventCount, events + 1) << "as one edit";
    const ttd::TTDTimePoint afterTrap = _controller->CurrentPosition();
    const MachineState liveAfterTrap = CaptureState(context, *_controller);
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint later = _controller->CurrentPosition();
    const MachineState liveLater = CaptureState(context, *_controller);
    _controller->StopRecording();

    auto expectSame = [&](const MachineState& seen, const MachineState& live, const char* where) {
        EXPECT_EQ(std::memcmp(&seen.cpu, &live.cpu, sizeof(seen.cpu)), 0)
            << where << ": CPU (pc " << seen.cpu.pc << " vs " << live.cpu.pc << ", hl " << seen.cpu.hl << " vs "
            << live.cpu.hl << ")";
        EXPECT_TRUE(seen.ram == live.ram) << where << ": RAM";
        EXPECT_TRUE(seen.devices == live.devices) << where << ": devices";
    };
    ASSERT_TRUE(_controller->SeekTo(afterTrap, nullptr));
    expectSame(CaptureState(context, *_controller), liveAfterTrap, "a seek across the trap");
    ASSERT_TRUE(_controller->SeekTo(later, nullptr));
    expectSame(CaptureState(context, *_controller), liveLater, "a seek frames later");

    // The machine playing the history forward from before the trap
    ASSERT_TRUE(_controller->SeekTo(_controller->GetCheckpoint(1)->time, nullptr));
    while (_controller->CurrentPosition() < later)
        _b->RunSingleCPUCycle(true);
    EXPECT_EQ(_controller->CurrentPosition(), later);
    expectSame(CaptureState(context, *_controller), liveLater, "running across the trap");
}

/// A debugger-forced RAM window (DeZog SET_SLOT, windows 1/2) follows no port
/// latch: the controller keeps it beside each checkpoint and a seek brings it back
TEST_F(TimeTravelController_Test, ADebuggerForcedWindowComesBackWithASeek)
{
    Memory* memory = _b->GetContext()->pMemory;
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    memory->SetDebuggerRAMPageToBank(1, 6);
    ASSERT_EQ(memory->GetDebuggerBankOverride(1), 6);
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint forced = _controller->GetCheckpoint(3)->time;
    memory->RevertDebuggerBankOverride(1);
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    _controller->StopRecording();

    ASSERT_TRUE(_controller->SeekTo(forced, nullptr));
    EXPECT_EQ(memory->GetDebuggerBankOverride(1), 6) << "the forced window at that checkpoint";
    EXPECT_EQ(memory->GetRAMPageForBank1(), 6);
    ASSERT_TRUE(_controller->SeekTo(_controller->GetCheckpoint(1)->time, nullptr));
    EXPECT_EQ(memory->GetDebuggerBankOverride(1), MEMORY_UNMAPPABLE) << "none before it";
}

/// D8 with the guards: a recording paused for browsing is still protected - a
/// tape load is refused and the history stays (it goes on from where it paused)
TEST_F(TimeTravelController_Test, APausedRecordingIsStillGuarded)
{
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(4, /*skipBreakpoints=*/true);
    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(1)->time.frame, 0}, nullptr));
    ASSERT_TRUE(_controller->GetSessionInfo().recordingPaused);
    const size_t kept = _controller->GetCheckpointCount();

    EXPECT_FALSE(_b->RecordingGuard(ttd::TTDGuardedAction::LoadTape).empty());
    std::string error;
    EXPECT_FALSE(_b->LoadTape(TestPathHelper::GetTestDataPath("contention/halt2int-v3/halt2int.tap"), &error));
    EXPECT_NE(error.find("stop the recording first"), std::string::npos) << error;
    EXPECT_EQ(_controller->GetCheckpointCount(), kept) << "the paused recording's history stays";
    EXPECT_TRUE(_controller->GetSessionInfo().recordingPaused);
    EXPECT_TRUE(_b->RecordingGuard(ttd::TTDGuardedAction::LoadSnapshot).empty()) << "a snapshot load is an event";
}
