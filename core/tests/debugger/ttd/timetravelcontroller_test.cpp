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
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
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
    EXPECT_EQ(_controller->GetWriteJournal()->Size(), _v1->GetWriteJournal()->Size()) << "the controller got the writes";
    EXPECT_GT(_controller->GetWriteJournal()->Size(), 0u);
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
    ttd::TTDSeekResult r;
    EXPECT_FALSE(_controller->SeekTo({last - 30, 0}, &r)) << "dropped history is not reachable";
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
