/// @file timetravelcontroller_test.cpp
/// @brief The engine's playback controller against the live machine: a machine
/// runs a program with key presses while the controller records, and the test
/// keeps what the machine was at points along the way - at frame starts and
/// inside frames, around the key events. Seeks to those points must land on
/// the same state: CPU, all RAM and every device. (Until Phase 6 v1's
/// TimeTravelManager, recording a twin machine, was the reference.)
///
/// Boots a machine and replays tens of frames: slower than the 50 ms
/// guideline, one acceptance check per scenario.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
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
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "emulator/video/screen.h"
#include "loaders/snapshot/machinestatetransfer.h"
#include "emulator/coordinator/ports.h"
#include "emulator/media/mediamanager.h"
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
    // Keyed by the device's legacy id: a device without one (D23: the controller manager's keyboard joysticks) is
    // left out
    for (const ttd::TTDRegistryEntry& e : session.GetPeripheralRegistry().Entries())
        if (e.device && e.device->TTDStateSize() != 0 && e.legacyId != ttd::PeripheralId::None)
            e.device->TTDSaveStateTo(s.devices[static_cast<uint8_t>(e.legacyId)]);
    return s;
}

/// One TAP block (flag #FF and 256 bytes) in the tape slot and the ROM's LD-BYTES entry as the fast tape trap
/// recognizes it; the block, or empty when the tape did not load
std::vector<uint8_t> InsertFastTapeBlock(Emulator& emulator, const char* name)
{
    std::vector<uint8_t> block(258);
    block[0] = 0xFF;
    uint8_t checksum = 0xFF;
    for (size_t i = 1; i <= 256; ++i)
    {
        block[i] = static_cast<uint8_t>(i * 7 + 3);
        checksum ^= block[i];
    }
    block[257] = checksum;
    const std::string tap = TestPathHelper::GetUniqueTestScratchPath(name);
    {
        std::ofstream out(tap, std::ios::binary);
        const uint8_t length[2] = {static_cast<uint8_t>(block.size() & 0xFF), static_cast<uint8_t>(block.size() >> 8)};
        out.write(reinterpret_cast<const char*>(length), 2);
        out.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size()));
    }
    if (!emulator.LoadTape(tap))
        return {};

    uint8_t* rom = emulator.GetContext()->pMemory->GetPhysicalAddressForZ80Page(0);
    const uint8_t signature1[8] = {0x14, 0x08, 0x15, 0xF3, 0x3E, 0x0F, 0xD3, 0xFE};
    const uint8_t signature2[3] = {0xDB, 0xFE, 0x1F};
    std::memcpy(rom + 0x0556, signature1, sizeof(signature1));
    std::memcpy(rom + 0x0556 + 12, signature2, sizeof(signature2));
    return block;
}

/// The loop's store: LD (HL),A at #8008
constexpr uint16_t kLoopStorePc = 0x8008;
}  // namespace

class TimeTravelController_Test : public ::testing::Test
{
protected:
    SoundCardScope _soundCards;
    Emulator* _a = nullptr;   ///< a twin of _b (the same program and RAM), recording with its own controller
    Emulator* _b = nullptr;   ///< records with _controller
    std::unique_ptr<ttd::TimeTravelController> _controller;

    /// A position the machine was at live, and what it was there
    struct LivePoint
    {
        ttd::TTDTimePoint at;
        MachineState state;
    };
    std::vector<LivePoint> _live;

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
        ASSERT_NE(_a->GetContext()->pTimeTravelController, nullptr);
        // Power-on RAM is randomized per machine: start both from the same bytes
        Memory* ma = _a->GetContext()->pMemory;
        Memory* mb = _b->GetContext()->pMemory;
        const size_t ramBytes = size_t(MAX_RAM_PAGES) * 0x4000;
        std::memcpy(mb->RAMPageAddress(0), ma->RAMPageAddress(0), ramBytes);
        // Machine B's core calls the test's controller (its own stays idle)
        _controller = std::make_unique<ttd::TimeTravelController>(_b->GetContext());
        _b->GetContext()->pTimeTravelHooks = _controller.get();
        _b->GetContext()->ttdWriteSink = _controller.get();
    }

    void TearDown() override
    {
        if (_b)
        {
            _controller->StopRecording();
            _b->GetContext()->pTimeTravelHooks = _b->GetContext()->pTimeTravelController;
            _b->GetContext()->ttdWriteSink = _b->GetContext()->pTimeTravelController;
            _controller.reset();
            EmulatorTestHelper::CleanupEmulator(_b);
        }
        if (_a)
            EmulatorTestHelper::CleanupEmulator(_a);
    }

    ttd::TimeTravelController* Twin() const { return _a->GetContext()->pTimeTravelController; }
    uint32_t Span() const { return static_cast<uint32_t>(_controller->FrameSpan()); }

    /// Keep what machine B is now
    void KeepLive() { _live.push_back({_controller->CurrentPosition(), CaptureState(_b->GetContext(), *_controller)}); }

    /// Run @p tStates on machine B (and its twin when it records) and keep where it got to
    void RunAndKeep(uint32_t tStates, bool twin = false)
    {
        if (twin)
            _a->RunTStates(tStates, /*skipBreakpoints=*/true);
        _b->RunTStates(tStates, /*skipBreakpoints=*/true);
        KeepLive();
    }

    /// Space pressed or released now, on machine B (and its twin)
    void Key(bool pressed, bool twin = false)
    {
        ttd::TTDInputEvent key;
        key.kind = ttd::TTDInputKind::Key;
        key.key = ZXKEY_SPACE;
        key.pressed = pressed;
        if (twin)
            ASSERT_TRUE(Twin()->SubmitLiveInput(key));
        ASSERT_TRUE(_controller->SubmitLiveInput(key));
    }

    /// 24 frames, space pressed and released mid-frame (frames 5 and 13). Kept along the way: three points inside
    /// frames 1, 8, 12 and 22, and points just before and after each key event. @p twin: machine A records the
    /// same run with its own controller, the write journal on
    void Record(bool journal = false, bool twin = false)
    {
        _controller->SetEnableWriteJournal(journal);
        ASSERT_TRUE(_controller->StartRecording());
        if (twin)
        {
            Twin()->SetEnableWriteJournal(true);   // the twin is the reference: its journal recorded
            ASSERT_TRUE(Twin()->StartRecording());
        }
        const uint32_t quarter = Span() / 4;
        for (int f = 0; f < 24; ++f)
        {
            if (f == 1 || f == 8 || f == 12 || f == 22)
                for (int k = 0; k < 3; ++k)
                    RunAndKeep(quarter, twin);
            if (f == 5 || f == 13)
            {
                RunAndKeep(19500, twin);
                if (twin)
                    _a->RunTStates(500, /*skipBreakpoints=*/true);
                _b->RunTStates(500, /*skipBreakpoints=*/true);
                ASSERT_NO_FATAL_FAILURE(Key(f == 5, twin));
                RunAndKeep(1, twin);
                RunAndKeep(900, twin);
            }
            if (twin)
                _a->RunNFrames(1, /*skipBreakpoints=*/true);
            _b->RunNFrames(1, /*skipBreakpoints=*/true);
        }
        _controller->StopRecording();
        if (twin)
            Twin()->StopRecording();
    }

    /// A seek to @p point lands on what the machine was there live
    void ExpectSeekShowsLive(const LivePoint& point)
    {
        SCOPED_TRACE("frame " + std::to_string(point.at.frame) + " T " + std::to_string(point.at.tInFrame));
        ttd::TTDSeekResult r;
        ASSERT_TRUE(_controller->SeekTo(point.at, &r));
        EXPECT_EQ(r.arrivedAt, point.at);
        ExpectState(point.state);
    }

    /// Machine B is @p expected
    void ExpectState(const MachineState& expected)
    {
        const MachineState seen = CaptureState(_b->GetContext(), *_controller);
        EXPECT_EQ(std::memcmp(&seen.cpu, &expected.cpu, sizeof(seen.cpu)), 0) << "CPU";
        EXPECT_EQ(seen.frame, expected.frame);
        EXPECT_EQ(seen.t, expected.t);
        ASSERT_EQ(seen.ram.size(), expected.ram.size());
        for (size_t i = 0; i < seen.ram.size(); ++i)
            if (seen.ram[i] != expected.ram[i])
            {
                ADD_FAILURE() << "RAM differs at " << i << " (page " << i / 0x4000 << ")";
                break;
            }
        ASSERT_EQ(seen.devices.size(), expected.devices.size());
        for (const auto& [id, bytes] : expected.devices)
            EXPECT_TRUE(seen.devices.at(id) == bytes) << "device " << int(id);
    }

    /// Every kept point, newest first, then the oldest again: the seeks go backwards and forwards
    void ExpectEverySeekShowsLive()
    {
        ASSERT_FALSE(_live.empty());
        for (auto it = _live.rbegin(); it != _live.rend(); ++it)
            ASSERT_NO_FATAL_FAILURE(ExpectSeekShowsLive(*it));
        ASSERT_NO_FATAL_FAILURE(ExpectSeekShowsLive(_live.front()));
    }

    /// Forget the kept points at or after @p from (a resume from there rewrites that future)
    void DropLiveFrom(const ttd::TTDTimePoint& from)
    {
        _live.erase(std::remove_if(_live.begin(), _live.end(), [&](const LivePoint& p) { return !(p.at < from); }),
                    _live.end());
    }

    /// The byte at Z80 address @p addr as the machine stands
    uint8_t ByteAt(uint16_t addr) const { return _b->GetContext()->pMemory->DirectReadFromZ80Memory(addr); }
};

TEST_F(TimeTravelController_Test, SeeksLandOnTheLiveMachine)
{
    ASSERT_NO_FATAL_FAILURE(Record());
    ASSERT_EQ(_controller->GetEngine().CheckpointCount(), _controller->GetCheckpointCount()) << "the controller's engine recorded";
    ASSERT_EQ(_controller->GetReplaySource(), &_controller->GetEngine()) << "seeks restore from the controller's engine";
    EXPECT_EQ(_live.size(), 4u * 3u + 2u * 3u);
    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());
}

/// The queries answer from the recorded history: the write journal and the coverage index reach the controller
/// through the core's sinks (C1d), and what they answer is what the machine did
TEST_F(TimeTravelController_Test, QueriesAnswerFromTheHistory)
{
    ASSERT_NO_FATAL_FAILURE(Record(/*journal=*/true));
    ASSERT_NE(_controller->GetWriteJournal(), nullptr);
    EXPECT_GT(_controller->GetEngine().Writes().Size(), 0u) << "the controller got the writes";
    EXPECT_EQ(_controller->GetWriteJournal()->Size(), 0u) << "the live ring was drained into the engine";
    EXPECT_GT(_controller->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed), 20u)
        << "the controller got the coverage";

    const ttd::TTDTimePoint end = _controller->SessionEndPosition();
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_controller->SeekTo(end, &r));

    // Who wrote these addresses last (the program stores the keyboard row at #C000..): the loop's store, with the
    // byte the memory holds at the end
    for (uint16_t addr : {uint16_t(0xC000), uint16_t(0xC010), uint16_t(0xC0FF)})
    {
        SCOPED_TRACE(addr);
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = addr;
        q.access = ttd::TTDAccessType::Write;
        const auto b = _controller->FindLastAccess(q);
        ASSERT_TRUE(b.has_value());
        EXPECT_EQ(b->pc, kLoopStorePc);
        EXPECT_EQ(b->value, ByteAt(addr));
        EXPECT_FALSE(end < b->time);
    }

    // Which frames ran the loop: every recorded one
    const auto scan = _controller->QueryCoverageScan(0, end.frame, ttd::TTDCoverageKind::Executed, 0x8000, 0x800B);
    EXPECT_TRUE(scan.indexAvailable);
    EXPECT_GE(scan.frames.size(), 20u);

    // Back to the IN of the loop, then a few instructions back: the same machine as a seek to that point
    const auto rc = _controller->ReverseContinue({0x8006});
    ASSERT_TRUE(rc.matched);
    EXPECT_TRUE(rc.arrivedAt < end);
    EXPECT_EQ(_b->GetContext()->pCore->GetZ80()->pc, 0x8006);
    EXPECT_TRUE(_controller->ReverseStepInstructions(5));
    const ttd::TTDTimePoint back = _controller->CurrentPosition();
    EXPECT_TRUE(back < rc.arrivedAt);
    const MachineState stepped = CaptureState(_b->GetContext(), *_controller);
    ASSERT_TRUE(_controller->SeekTo(_controller->GetCheckpoint(2)->time, &r));
    ASSERT_TRUE(_controller->SeekTo(back, &r));
    ASSERT_NO_FATAL_FAILURE(ExpectState(stepped));
}

/// A recording resumed from the past (C2): back into the middle of frame 10, recorded on with a key pressed; every
/// seek - before the resume point and after it - lands on what the machine was live
TEST_F(TimeTravelController_Test, ResumeFromThePastContinuesAsTheMachineRan)
{
    ASSERT_NO_FATAL_FAILURE(Record());
    const ttd::TTDTimePoint from{_controller->GetCheckpoint(10)->time.frame, Span() / 3};
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_controller->SeekTo(from, &r));
    ASSERT_TRUE(_controller->ResumeRecordingFrom(from));
    DropLiveFrom(from);
    const size_t before = _live.size();
    for (int f = 0; f < 10; ++f)
    {
        if (f == 4)
        {
            RunAndKeep(15000);
            ASSERT_NO_FATAL_FAILURE(Key(true));
            RunAndKeep(1);
        }
        if (f == 2 || f == 8)
            RunAndKeep(Span() / 2);
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    _controller->StopRecording();
    ASSERT_GT(before, 0u);
    ASSERT_EQ(_live.size(), before + 4u);

    const ttd::TimeTravelEngine& engine = _controller->GetEngine();
    ASSERT_EQ(engine.CheckpointCount() - engine.FirstCheckpoint(), _controller->GetCheckpointCount())
        << "the engine holds the history";
    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());
}

/// A frame limit (C2): the controller's engine holds whole segments covering at least the last 16 frames; inside
/// the window seeks land on the live machine
TEST_F(TimeTravelController_Test, HistoryLimitKeepsTheWindow)
{
    _controller->SetHistoryLimit(16, 0);
    ASSERT_TRUE(_controller->StartRecording());
    for (int f = 0; f < 48; ++f)
    {
        if (f == 34 || f == 42 || f == 47)
            RunAndKeep(Span() / 2);
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    const ttd::TTDTimePoint stoppedAt = _controller->CurrentPosition();
    _controller->StopRecording();

    const ttd::TimeTravelEngine& engine = _controller->GetEngine();
    const size_t held = _controller->GetCheckpointCount();
    EXPECT_EQ(engine.CheckpointCount() - engine.FirstCheckpoint(), held);
    EXPECT_GE(held, 16u);
    EXPECT_LE(held, 16u + 2u) << "segments of an eighth of the window";
    EXPECT_GT(engine.FirstCheckpoint(), 0u) << "the oldest segments were dropped";
    EXPECT_EQ(_controller->GetCheckpoint(0)->time.frame, engine.Checkpoint(engine.FirstCheckpoint())->position.frame);
    // The history reaches to where the recording stopped
    EXPECT_EQ(_controller->SessionEndPosition(), stoppedAt);

    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());
    // D12: a seek before the earliest kept position names it and moves nothing
    const uint64_t last = stoppedAt.frame;
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

/// A session recorded on one model is refused by an instance of another, with the reason
TEST_F(TimeTravelController_Test, ALoadRefusesAnotherModelsSession)
{
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    _controller->StopRecording();
    std::stringstream file;
    std::string err;
    ASSERT_TRUE(_controller->SerializeSession(file, err)) << err;

    // Pretend this instance is a different machine
    EmulatorContext* context = _b->GetContext();
    const MEM_MODEL original = context->config.mem_model;
    context->config.mem_model = original == MM_SPECTRUM48 ? MM_PENTAGON : MM_SPECTRUM48;
    EXPECT_FALSE(_controller->DeserializeSession(file, err)) << "a session from another model was accepted";
    EXPECT_NE(err.find("model"), std::string::npos) << "the refusal does not name the model: " << err;
    context->config.mem_model = original;
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
    ttd::TimeTravelController* controller = context->pTimeTravelController;
    ASSERT_NE(controller, nullptr);

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
    EmulatorTestHelper::CleanupEmulator(emulator);
}

namespace
{
/// Record 14 frames with a marker of @p kind in the middle of frame 8; the marker's position
ttd::TTDTimePoint RecordWithMarker(Emulator* emulator, ttd::TimeTravelController* controller, ttd::TTDExternalEventKind kind)
{
    ttd::TTDTimePoint at;
    if (!controller->StartRecording())
        return at;
    for (int f = 0; f < 14; ++f)
    {
        if (f == 8)
        {
            emulator->RunTStates(30000, /*skipBreakpoints=*/true);
            at = controller->CurrentPosition();
            controller->RecordExternalEvent(kind, "outside");
        }
        emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    controller->StopRecording();
    return at;
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

/// Barriers in queries come from the engine's event log (C3): a marker the replay cannot reproduce stops a
/// backward search there, and the answer names it
TEST_F(TimeTravelController_Test, AMarkerTheReplayCannotReproduceStopsASearch)
{
    const ttd::TTDTimePoint at = RecordWithMarker(_b, _controller.get(), ttd::TTDExternalEventKind::Other);
    ASSERT_GT(at.frame, 0u);
    ttd::TTDExternalEvent marker;
    ttd::TTDSearchWindow window;
    EXPECT_FALSE(_controller->FindLastAccess(NeverExecuted(), &marker, &window).has_value());
    EXPECT_EQ(marker.time, at);
    EXPECT_EQ(marker.kind, ttd::TTDExternalEventKind::Other);
    EXPECT_STREQ(marker.reason, "outside");
    EXPECT_GT(window.from.frame, _controller->GetCheckpoint(0)->time.frame) << "stopped at the marker";
    EXPECT_FALSE(window.from < at) << "the window starts at the marker";
    EXPECT_EQ(window.to, _controller->SessionEndPosition());
}

/// Tape control is input the engine's replay applies, not a barrier: the controller searches across it to the
/// session start
TEST_F(TimeTravelController_Test, TapeControlIsNoBarrierForTheController)
{
    const ttd::TTDTimePoint at = RecordWithMarker(_b, _controller.get(), ttd::TTDExternalEventKind::TapeControl);
    ASSERT_GT(at.frame, 0u);
    ttd::TTDSearchWindow window;
    EXPECT_FALSE(_controller->FindLastAccess(NeverExecuted(), nullptr, &window).has_value());
    EXPECT_EQ(window.from, _controller->GetCheckpoint(0)->time) << "the controller reaches the session start";
}

/// The write journal built afterwards by replay (C3c): a twin records the same run with the journal on, machine B
/// without it and builds it for the whole session afterwards; the built index answers find-last as the recorded one
TEST_F(TimeTravelController_Test, TheBuiltJournalAnswersAsTheRecordedOne)
{
    ASSERT_NO_FATAL_FAILURE(Record(/*journal=*/false, /*twin=*/true));
    ttd::TimeTravelController* twin = Twin();
    ASSERT_EQ(twin->GetCheckpointCount(), _controller->GetCheckpointCount());
    ASSERT_GT(twin->GetEngine().Writes().Size(), 0u) << "the twin recorded its writes";
    const ttd::TTDJournalBuildResult b = _controller->BuildWriteJournal(0, UINT64_MAX);
    ASSERT_TRUE(b.ok) << b.error;
    EXPECT_GT(b.records, 0u);
    const ttd::TTDWriteIndex& writes = _controller->GetEngine().Writes();
    EXPECT_EQ(writes.Size(), b.records) << "the engine's index holds the built journal";
    ASSERT_EQ(writes.Segments().size(), 1u);

    // From the last checkpoint: the built journal covers the frames between checkpoints, not the tail after the last
    ASSERT_EQ(twin->SessionEndPosition(), _controller->SessionEndPosition());
    const ttd::TTDTimePoint end = _controller->GetCheckpoint(_controller->GetCheckpointCount() - 1)->time;
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_controller->SeekTo(end, &r));
    ASSERT_TRUE(twin->SeekTo(end, &r));
    const ttd::TTDTimePoint here = _controller->CurrentPosition();
    for (uint16_t addr : {uint16_t(0xC000), uint16_t(0xC07F), uint16_t(0xC0FF)})
    {
        SCOPED_TRACE(addr);
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = addr;
        q.access = ttd::TTDAccessType::Write;
        const auto x = twin->FindLastAccess(q);
        const auto y = _controller->FindLastAccess(q);
        ASSERT_TRUE(x.has_value());
        ASSERT_TRUE(y.has_value());
        EXPECT_EQ(x->time, y->time);
        EXPECT_EQ(x->pc, y->pc);
        EXPECT_EQ(x->value, y->value);
        EXPECT_EQ(y->pc, kLoopStorePc);
        EXPECT_EQ(_controller->CurrentPosition(), here) << "answered from the index: the machine stays";
    }

    // From the session end, inside the tail after the last checkpoint: the built journal does not cover it (no
    // checkpoint closes the frame), so the search replays it - the recorded answer, the machine positioned at it
    const ttd::TTDTimePoint tail = _controller->SessionEndPosition();
    ASSERT_TRUE(end < tail);
    ASSERT_TRUE(_controller->SeekTo(tail, &r));
    ASSERT_TRUE(twin->SeekTo(tail, &r));
    for (uint16_t addr : {uint16_t(0xC000), uint16_t(0xC07F), uint16_t(0xC0FF)})
    {
        SCOPED_TRACE(addr);
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = addr;
        q.access = ttd::TTDAccessType::Write;
        const auto x = twin->FindLastAccess(q);
        const auto y = _controller->FindLastAccess(q);
        ASSERT_TRUE(x.has_value());
        ASSERT_TRUE(y.has_value());
        EXPECT_EQ(x->time, y->time);
        EXPECT_EQ(x->pc, y->pc);
        EXPECT_EQ(x->value, y->value);
        EXPECT_EQ(_controller->CurrentPosition(), y->time) << "answered by replay: the machine is at the answer";
        ASSERT_TRUE(_controller->SeekTo(tail, &r));
        ASSERT_TRUE(twin->SeekTo(tail, &r));
    }
}

/// Status from the engine (C4a): the recorded machine, the store's figures and key frames from the engine
TEST_F(TimeTravelController_Test, StatusDescribesTheSession)
{
    ASSERT_NO_FATAL_FAILURE(Record());
    const ttd::TTDSessionInfo b = _controller->GetSessionInfo();
    EXPECT_EQ(b.checkpointCount, _controller->GetCheckpointCount());
    EXPECT_EQ(b.sessionStartFrame, _controller->GetCheckpoint(0)->time.frame);
    EXPECT_EQ(b.currentEndFrame, _controller->GetCheckpoint(_controller->GetCheckpointCount() - 1)->time.frame);
    EXPECT_FALSE(b.machine.peripherals.empty());
    EXPECT_EQ(b.inputEventCount, _controller->GetInputJournal().Size());
    EXPECT_GE(b.inputEventCount, 2u) << "the press and the release";

    const ttd::TimeTravelEngine& engine = _controller->GetEngine();
    EXPECT_EQ(b.pageStoreUsedBytes, engine.PieceStore().PayloadBytes());
    EXPECT_EQ(b.baselineFramesCaptured, engine.PieceStore().LiveVersions());
    EXPECT_GT(b.compressionRatio, 1.0);
    EXPECT_EQ(b.keyFrameCount, engine.Segments().size()) << "a key frame starts each segment";
    EXPECT_EQ(b.keyFrameCount + b.deltaFrameCount, b.checkpointCount);
    EXPECT_GE(_controller->GetHeapBreakdown().Total(), engine.HeapBreakdown().Total()) << "the engine's memory is counted";
}

/// A saved session loads back (C4b): the engine's file with the controller's facts, coverage and bookmarks. Seeks
/// into the loaded session land on the live machine, and the loaded session continues: resumed from frame 12, it
/// records on and seeks land on the machine as it ran on
TEST_F(TimeTravelController_Test, ASavedSessionLoadsAndContinues)
{
    ASSERT_NO_FATAL_FAILURE(Record(/*journal=*/true));
    std::string err;
    ASSERT_TRUE(_controller->AddBookmark(_controller->GetCheckpoint(7)->time, "seven", &err)) << err;
    const size_t checkpoints = _controller->GetCheckpointCount();
    const size_t inputs = _controller->GetInputJournal().Size();
    const size_t coveredFrames = _controller->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed);
    ASSERT_GT(coveredFrames, 0u);
    ttd::TTDSearchQuery q;
    q.addrFrom = q.addrTo = 0xC010;
    q.access = ttd::TTDAccessType::Write;
    // Both searches from the last checkpoint (a search runs back from where the machine is)
    const ttd::TTDTimePoint searchFrom = _controller->GetCheckpoint(checkpoints - 1)->time;
    ASSERT_TRUE(_controller->SeekTo(searchFrom));
    const auto recorded = _controller->FindLastAccess(q);
    ASSERT_TRUE(recorded.has_value());
    std::stringstream file;
    ASSERT_TRUE(_controller->SerializeSession(file, err)) << err;

    _controller->InvalidateSession("test");
    ASSERT_EQ(_controller->GetCheckpointCount(), 0u);
    ASSERT_TRUE(_controller->DeserializeSession(file, err)) << err;
    EXPECT_EQ(_controller->GetCheckpointCount(), checkpoints);
    EXPECT_TRUE(_controller->GetSessionInfo().loadedFromFile);
    EXPECT_EQ(_controller->GetCoverageIndex().SealedFrameCount(ttd::TTDCoverageKind::Executed), coveredFrames);
    ASSERT_EQ(_controller->GetBookmarks().size(), 1u);
    EXPECT_EQ(_controller->GetBookmarks()[0].label, "seven");
    EXPECT_EQ(_controller->GetInputJournal().Size(), inputs) << "the key events";

    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());
    ASSERT_TRUE(_controller->SeekTo(searchFrom));
    const auto loaded = _controller->FindLastAccess(q);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->time, recorded->time) << "the write journal came back";
    EXPECT_EQ(loaded->value, recorded->value);

    // Continue the loaded session from frame 12
    const ttd::TTDTimePoint from{_controller->GetCheckpoint(12)->time.frame, Span() / 3};
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_controller->SeekTo(from, &r));
    ASSERT_TRUE(_controller->ResumeRecordingFrom(from)) << "a loaded session continues";
    DropLiveFrom(from);
    for (int f = 0; f < 6; ++f)
    {
        if (f == 1 || f == 4)
            RunAndKeep(Span() / 2);
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    _controller->StopRecording();
    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());
    // The write journal lost the old future and holds the new one
    const ttd::TTDTimePoint end = _controller->SessionEndPosition();
    ASSERT_TRUE(_controller->SeekTo(end, &r));
    for (uint16_t addr : {uint16_t(0xC010), uint16_t(0xC0F0)})
    {
        q.addrFrom = q.addrTo = addr;
        const auto x = _controller->FindLastAccess(q);
        ASSERT_TRUE(x.has_value()) << addr;
        EXPECT_FALSE(x->time < from) << addr << ": a write of the new future";
        EXPECT_EQ(x->value, ByteAt(addr)) << addr;
    }
}

/// A session file on disk (C4b): file-info describes the recorded machine without loading it, and a port search
/// of the file finds what the session itself finds
TEST_F(TimeTravelController_Test, FileInfoAndPortSearchReadTheEngineFile)
{
    ASSERT_NO_FATAL_FAILURE(Record());
    std::string err;
    ASSERT_TRUE(_controller->AddBookmark(_controller->GetCheckpoint(4)->time, "four", &err)) << err;
    const std::string ours = TestPathHelper::GetUniqueTestScratchPath("controller-session.ttd");
    {
        std::ofstream out(FileHelper::ToFsPath(ours), std::ios::binary);
        ASSERT_TRUE(_controller->SerializeSession(out, err)) << err;
    }
    ttd::TTDFileInfo b;
    ASSERT_TRUE(ttd::ReadTTDFileInfo(ours, b, err)) << err;
    const ttd::TTDSessionInfo info = _controller->GetSessionInfo();
    EXPECT_EQ(b.schemaVersion, 2u);
    EXPECT_EQ(b.machine.modelId, info.machine.modelId);
    EXPECT_EQ(b.machine.model, info.machine.model);
    EXPECT_FALSE(b.machine.model.empty());
    EXPECT_EQ(b.machine.peripherals, info.machine.peripherals);
    EXPECT_EQ(b.machine.romSignature, info.machine.romSignature);
    EXPECT_EQ(b.machine.generalSound, info.machine.generalSound);
    EXPECT_EQ(b.startFrame, info.sessionStartFrame);
    EXPECT_EQ(b.endFrame, info.currentEndFrame);
    EXPECT_EQ(b.checkpointCount, info.checkpointCount);
    EXPECT_TRUE(b.hasPortJournals);
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
    std::error_code ec;
    std::filesystem::remove(FileHelper::ToFsPath(ours), ec);
}

/// Clip export walks the session on the engine (C4c): every frame's composed picture is the framebuffer the
/// machine had live at the end of that frame
TEST_F(TimeTravelController_Test, ClipFramesAreTheLivePictures)
{
    // The loop sends the refresh register to the border (LD A,R; OUT (#FE),A
    // in place of the keyboard read): the border stripes differ every frame
    _b->GetFeatureManager()->setFeature(Features::kScreenHQ, true);   // the picture is drawn as the frame runs
    Memory* memory = _b->GetContext()->pMemory;
    const uint8_t patch[] = {0xED, 0x5F, 0xD3, 0xFE};
    for (uint16_t i = 0; i < 4; ++i)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8004 + i), patch[i]);

    // The picture at each frame boundary, by the frame it ends
    std::map<uint64_t, std::vector<uint8_t>> pictures;
    EmulatorContext* context = _b->GetContext();
    ASSERT_TRUE(_controller->StartRecording());
    for (int f = 0; f < 24; ++f)
    {
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
        const FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
        pictures[context->emulatorState.frame_counter - 1].assign(fb.memoryBuffer, fb.memoryBuffer + fb.memoryBufferSize);
    }
    _controller->StopRecording();

    const uint64_t first = _controller->GetCheckpoint(2)->time.frame;
    const uint64_t last = _controller->GetCheckpoint(_controller->GetCheckpointCount() - 2)->time.frame;
    std::vector<std::vector<uint8_t>> clip;
    std::vector<uint64_t> frames;
    EXPECT_EQ(_controller->VisitComposedFrames(first, last,
                                               [&](const ttd::TTDComposedFrame& f) {
                                                   clip.emplace_back(f.rgba, f.rgba + f.rgbaBytes);
                                                   frames.push_back(f.frame);
                                                   return true;
                                               }),
              "");
    ASSERT_EQ(clip.size(), last - first + 1);
    EXPECT_FALSE(clip.front() == clip.back()) << "the pictures change over the clip";
    for (size_t i = 0; i < clip.size(); ++i)
    {
        ASSERT_EQ(pictures.count(frames[i]), 1u) << "frame " << frames[i];
        EXPECT_TRUE(clip[i] == pictures[frames[i]]) << "frame " << frames[i];
    }
    EXPECT_NE(_controller->VisitComposedFrames(last, last + 50, [](const ttd::TTDComposedFrame&) { return true; }), "")
        << "past the session";
}

/// D8: a seek while recording pauses the recording; resumed where it paused it goes on as one recording - every
/// seek lands on what the machine was live, before the pause and after it
TEST_F(TimeTravelController_Test, APausedRecordingContinuesWhereItPaused)
{
    ASSERT_TRUE(_controller->StartRecording());
    bool pressed = false;
    auto frames = [&](int count) {
        for (int f = 0; f < count; ++f)
        {
            if (f == 3)
            {
                RunAndKeep(20000);
                pressed = !pressed;
                ASSERT_NO_FATAL_FAILURE(Key(pressed));
                RunAndKeep(1);
            }
            if (f == 6)
                RunAndKeep(Span() / 2);
            _b->RunNFrames(1, /*skipBreakpoints=*/true);
        }
    };
    ASSERT_NO_FATAL_FAILURE(frames(10));
    // Mid-frame, then a look back
    RunAndKeep(31000);
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
    ASSERT_NO_FATAL_FAILURE(frames(10));
    _controller->StopRecording();
    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());
}

/// D8: running forward through a paused recording, execution reaches where it paused and the recording goes on by
/// itself - one history: seeks land on the machine as it ran, before the pause and in the stretch recorded after it
TEST_F(TimeTravelController_Test, RunningIntoThePausedEndContinuesTheRecording)
{
    ASSERT_TRUE(_controller->StartRecording());
    for (int f = 0; f < 12; ++f)
    {
        if (f == 3 || f == 10)
            RunAndKeep(Span() / 3);
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    RunAndKeep(25000);
    const ttd::TTDTimePoint pausedAt = _controller->CurrentPosition();
    const uint64_t backFrame = _controller->GetCheckpoint(6)->time.frame;
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_controller->SeekTo({backFrame, 0}, &r));
    ASSERT_TRUE(_controller->GetSessionInfo().recordingPaused);
    // From frame 6 the machine runs 10 frames: 6 replayed, then live and recorded
    _b->RunNFrames(10, /*skipBreakpoints=*/true);
    EXPECT_TRUE(_controller->IsRecording()) << "the recording went on at the paused end";
    EXPECT_FALSE(_controller->GetSessionInfo().recordingPaused);
    for (int f = 0; f < 4; ++f)
    {
        RunAndKeep(Span() / 3);
        _b->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    _controller->StopRecording();
    EXPECT_GT(_controller->SessionEndPosition().frame, pausedAt.frame);
    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());
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

/// THE rule (D42, owner 2026-10-06; it replaces D10's "a snapshot load is part of the recording"): a snapshot load ENDS the
/// recording session exactly as a reset does. Nothing is refused, the recording stops cleanly, the history of the machine stays
/// browsable, and no new session starts unless the `ttdrestart` feature is on
TEST_F(TimeTravelController_Test, ASnapshotLoadEndsTheSession)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("d40-snapshot.z80");
    ASSERT_TRUE(_b->SaveSnapshot(path));
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(5, /*skipBreakpoints=*/true);
    const size_t recorded = _controller->GetCheckpointCount();
    ASSERT_GE(recorded, 5u);

    ASSERT_TRUE(_b->LoadSnapshot(path)) << "never refused while recording";
    EXPECT_FALSE(_controller->IsRecording()) << "the load ended the session";
    EXPECT_EQ(_controller->GetSessionInfo().lastStopReason, "snapshot-load");
    EXPECT_EQ(_controller->GetCheckpointCount(), recorded) << "the history of this machine stays";
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    EXPECT_FALSE(_controller->IsRecording()) << "no new session by default";
    EXPECT_EQ(_controller->GetCheckpointCount(), recorded);
    ttd::TTDSeekResult seek;
    EXPECT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(2)->time.frame, 0}, &seek)) << "still browsable";
}

/// A load that is refused is not an operation: the plan decides first, the session ends only when the snapshot really goes in. A
/// refused load (another model's SZX, an SPG on a machine that is not a TS-Conf, a file that is no snapshot, a bank the machine
/// lacks) leaves the recording running and its history growing
TEST_F(TimeTravelController_Test, ARefusedSnapshotLoadLeavesTheRecordingRunning)
{
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    const size_t recorded = _controller->GetCheckpointCount();

    const std::string garbage = TestPathHelper::GetUniqueTestScratchPath("d42-garbage.z80");
    {
        std::ofstream out(garbage, std::ios::binary);
        out << "not a snapshot";
    }
    for (const std::string& file : {TestPathHelper::GetTestDataPath("loaders/szx/libspectrum/synth-48.szx"),
                                    TestPathHelper::GetTestDataPath("machines/tsconf/spg/empty.spg"), garbage})
    {
        EXPECT_FALSE(_b->LoadSnapshot(file)) << file;
        EXPECT_TRUE(_controller->IsRecording()) << file << ": a refused load does not end the session";
        EXPECT_TRUE(_controller->GetSessionInfo().lastStopReason.empty()) << file;
    }
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    EXPECT_TRUE(_controller->IsRecording());
    EXPECT_GT(_controller->GetCheckpointCount(), recorded) << "the recording went on";
    std::remove(garbage.c_str());

    // ...while a load that goes in still ends it
    const std::string good = TestPathHelper::GetUniqueTestScratchPath("d42-good.z80");
    ASSERT_TRUE(_b->SaveSnapshot(good));
    ASSERT_TRUE(_b->LoadSnapshot(good));
    EXPECT_FALSE(_controller->IsRecording());
    EXPECT_EQ(_controller->GetSessionInfo().lastStopReason, "snapshot-load");
    std::remove(good.c_str());
}

/// The same rule for a recording paused for browsing, and for no recording at all (the history stays)
TEST_F(TimeTravelController_Test, ASnapshotLoadEndsAPausedRecordingToo)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("d40-paused.z80");
    ASSERT_TRUE(_b->SaveSnapshot(path));
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(5, /*skipBreakpoints=*/true);
    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(2)->time.frame, 0}, nullptr));
    ASSERT_TRUE(_controller->GetSessionInfo().recordingPaused);
    const size_t kept = _controller->GetCheckpointCount();

    ASSERT_TRUE(_b->LoadSnapshot(path));
    EXPECT_FALSE(_controller->IsRecording());
    EXPECT_FALSE(_controller->GetSessionInfo().recordingPaused);
    EXPECT_EQ(_controller->GetState(), ttd::TTDSessionState::Idle) << "the machine left the history";
    EXPECT_EQ(_controller->GetCheckpointCount(), kept);

    ASSERT_TRUE(_b->LoadSnapshot(path)) << "with no recording the history stays";
    ASSERT_EQ(_controller->GetCheckpointCount(), kept);
    ttd::TTDSeekResult r;
    EXPECT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(3)->time.frame, 0}, &r));
}

/// One rule, three operations: a reset, a snapshot load and a change of the machine itself each end the recording session; a
/// new one starts at the next frame boundary only with the `ttdrestart` feature on (default off). The black box always starts one
TEST_F(TimeTravelController_Test, EveryEndingOperationFollowsTheOneRule)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("d40-rule.z80");
    ASSERT_TRUE(_b->SaveSnapshot(path));
    FeatureManager* features = _b->GetFeatureManager();
    EXPECT_FALSE(features->isEnabled(Features::kTTDRestart)) << "off by default";

    const std::pair<const char*, std::function<void()>> operations[] = {
        {"reset", [&] { _b->Reset(); }},
        {"snapshot load", [&] { ASSERT_TRUE(_b->LoadSnapshot(path)); }},
        {"machine change", [&] { EXPECT_TRUE(MachineStateTransfer::Transfer(*_a, *_b, {}).ok); }},
    };
    for (const bool restart : {false, true})
    {
        ASSERT_TRUE(features->setFeature(Features::kTTDRestart, restart));
        for (const auto& [name, operation] : operations)
        {
            ASSERT_TRUE(_controller->StartRecording()) << name;
            _b->RunNFrames(3, /*skipBreakpoints=*/true);
            operation();
            EXPECT_FALSE(_controller->IsRecording()) << name << ": the operation ended the session";
            _b->RunNFrames(2, /*skipBreakpoints=*/true);
            EXPECT_EQ(_controller->IsRecording(), restart)
                << name << ": a new session " << (restart ? "starts" : "does not start") << " (ttdrestart " << restart << ")";
            if (_controller->IsRecording())
                _controller->StopRecording();
        }
    }
}

/// A change of the machine itself ends the session (owner rule 2026-10-05): on
/// the engine nothing is refused - a machine state transfer into a recording
/// machine stops the recording and drops its history; an explicit recording
/// stays off, a black box starts a new session at the next frame boundary
TEST_F(TimeTravelController_Test, AMachineChangeEndsTheSession)
{
    for (const ttd::TTDGuardedAction action : {ttd::TTDGuardedAction::LoadRom,
                                               ttd::TTDGuardedAction::SwitchGsCard, ttd::TTDGuardedAction::SwitchModel,
                                               ttd::TTDGuardedAction::ChangeSlots})
    {
        ASSERT_TRUE(_controller->StartRecording());
        EXPECT_TRUE(_b->RecordingGuard(action).empty()) << static_cast<int>(action);
        _controller->StopRecording();
    }

    // An explicit recording: ended cleanly (debug mode, which it switched on, back off), not started again
    FeatureManager* features = _b->GetFeatureManager();
    ASSERT_TRUE(features->setFeature(Features::kDebugMode, false));
    ASSERT_TRUE(_controller->StartRecording());
    ASSERT_TRUE(features->isEnabled(Features::kDebugMode)) << "the recording needs it";
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    MachineStateTransfer::Report report = MachineStateTransfer::Transfer(*_a, *_b, {});
    EXPECT_TRUE(report.ok) << report.reason;
    EXPECT_FALSE(_controller->IsRecording());
    EXPECT_FALSE(features->isEnabled(Features::kDebugMode)) << "the recording's features restored";
    EXPECT_EQ(_controller->GetCheckpointCount(), 0u) << "the session ended";
    EXPECT_EQ(_controller->GetSessionInfo().lastDropReason, "state-transfer");
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    EXPECT_FALSE(_controller->IsRecording()) << "an explicit recording is not started again";

    // A black box: a new session on the changed machine
    _controller->SetBlackBox(true, 5);
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    report = MachineStateTransfer::Transfer(*_a, *_b, {});
    EXPECT_TRUE(report.ok) << report.reason;
    EXPECT_FALSE(_controller->IsRecording());
    _b->RunNFrames(1, /*skipBreakpoints=*/true);
    EXPECT_TRUE(_controller->IsRecording()) << "the black box records again";
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    EXPECT_LE(_controller->GetCheckpointCount(), 3u) << "a new session, not the old one";
}

/// A recording that switched debug mode on gives it back when it stops, but not while a debug feature switched on
/// meanwhile still needs it: an analysis session that started memory tracking during a black box session kept losing
/// its counters when a reset or a snapshot load ended that session (the tracker frees them with debug mode off)
TEST_F(TimeTravelController_Test, StopKeepsDebugModeAnotherFeatureNeeds)
{
    FeatureManager* features = _b->GetFeatureManager();
    MemoryAccessTracker& tracker = _b->GetMemory()->GetAccessTracker();
    ASSERT_TRUE(features->setFeature(Features::kDebugMode, false));
    ASSERT_TRUE(_controller->StartRecording());
    ASSERT_TRUE(features->isEnabled(Features::kDebugMode)) << "the recording needs it";

    ASSERT_TRUE(features->setFeature(Features::kMemoryTracking, true));
    ASSERT_NE(tracker.Z80CountersView().reads, nullptr) << "tracking allocated its counters";
    _controller->StopRecording();
    EXPECT_TRUE(features->isEnabled(Features::kDebugMode)) << "memory tracking still needs debug mode";
    EXPECT_NE(tracker.Z80CountersView().reads, nullptr) << "the counters survive the stop";

    // Nothing else needs it: the next stop gives debug mode back as before
    ASSERT_TRUE(features->setFeature(Features::kMemoryTracking, false));
    ASSERT_TRUE(features->setFeature(Features::kDebugMode, false));
    ASSERT_TRUE(_controller->StartRecording());
    _controller->StopRecording();
    EXPECT_FALSE(features->isEnabled(Features::kDebugMode)) << "the recording's features restored";
}

/// Step 3 (D29): the black box records without holding the machine at real
/// speed. Before an acceleration (turbo mode, host speed) takes effect it
/// stops, its history kept; once nothing accelerates a new session starts. The
/// tape shortcuts do not stop it (the tape trap is a recorded edit; fast disk is
/// held: its FDC timing is no edit). It keeps the
/// last N minutes. An explicit recording still refuses another host speed
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
    // Fast disk is held: its compressed FDC timing is no recorded edit (a DSS floppy boot replayed another WD1793)
    EXPECT_FALSE(features->setFeature(Features::kFastDisk, true));
    EXPECT_FALSE(features->isEnabled(Features::kFastDisk));

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

    // An explicit recording holds the host speed, and records through turbo (D4)
    _controller->StopRecording();
    _controller->SetBlackBox(false);
    ASSERT_TRUE(features->setFeature(Features::kFastTape, false));
    ASSERT_TRUE(_controller->StartRecording());
    EXPECT_FALSE(_b->SetSpeedMultiplier(4)) << "refused while an explicit recording runs";
    EXPECT_TRUE(features->setFeature(Features::kTurboMode, true));
    EXPECT_TRUE(_controller->IsRecording()) << "an explicit recording does not step aside";
    ASSERT_TRUE(features->setFeature(Features::kTurboMode, false));
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

    const std::vector<uint8_t> block = InsertFastTapeBlock(*_b, "blackbox-trap.tap");
    ASSERT_FALSE(block.empty());

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

/// Phase 2, D4: an explicit recording keeps turbo mode and the tape shortcuts (a tape loaded from zero took minutes
/// at 1x with the ROM trap off). Turbo only takes the host's pacing away and the trap is a recorded edit: with an AY
/// tone playing, a fast tape load in turbo and frames after it replay to what the machine was live. Host speed
/// stays at 1x, fast disk held
TEST_F(TimeTravelController_Test, AnExplicitRecordingKeepsTurboAndTheTapeShortcuts)
{
    EmulatorContext* context = _b->GetContext();
    Memory* memory = context->pMemory;
    Z80* z80 = context->pCore->GetZ80();
    FeatureManager* features = _b->GetFeatureManager();
    const std::vector<uint8_t> block = InsertFastTapeBlock(*_b, "explicit-trap.tap");
    ASSERT_FALSE(block.empty());

    // An AY tone on channel A (mixer #3E, volume 15, period #40): LD BC,#FFFD; LD A,r; OUT (C),A; LD B,#BF;
    // LD A,v; OUT (C),A per register, then the wait of about three frames and the load as in the black box's test
    const uint8_t program[] = {
        0x01, 0xFD, 0xFF, 0x3E, 0x07, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x3E, 0xED, 0x79,  // R7 = #3E
        0x06, 0xFF, 0x3E, 0x08, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x0F, 0xED, 0x79,        // R8 = 15
        0x06, 0xFF, 0x3E, 0x00, 0xED, 0x79, 0x06, 0xBF, 0x3E, 0x40, 0xED, 0x79,        // R0 = #40
        0x01, 0x40, 0x1F, 0x0B, 0x78, 0xB1, 0x20, 0xFB, 0x3E, 0xFF, 0x11, 0x00, 0x01, 0xDD,
        0x21, 0x00, 0x90, 0x37, 0xCD, 0x56, 0x05, 0x21, 0x00, 0x00, 0x23, 0x18, 0xFD};
    for (size_t i = 0; i < sizeof(program); ++i)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8100 + i), program[i]);
    z80->pc = 0x8100;
    z80->sp = 0xBF00;

    ASSERT_TRUE(features->setFeature(Features::kFastTape, true));
    ASSERT_TRUE(_controller->StartRecording());
    ASSERT_FALSE(_controller->IsBlackBox());
    EXPECT_TRUE(features->isEnabled(Features::kFastTape)) << "kept by an explicit recording";
    EXPECT_TRUE(features->setFeature(Features::kTurboTape, true));
    EXPECT_TRUE(features->isEnabled(Features::kTurboTape));
    EXPECT_FALSE(features->setFeature(Features::kFastDisk, true)) << "its compressed FDC timing is no recorded edit";
    EXPECT_FALSE(_b->SetSpeedMultiplier(4)) << "the host speed stays at 1x";
    ASSERT_TRUE(features->setFeature(Features::kTurboMode, true)) << features->refusalReason(Features::kTurboMode, true);
    ASSERT_TRUE(context->pCore->IsTurboMode());
    EXPECT_TRUE(_controller->IsRecording()) << "an explicit recording goes on in turbo";

    const size_t events = _controller->GetSessionInfo().externalEventCount;
    _b->RunNFrames(1, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint inTurbo = _controller->CurrentPosition();
    const MachineState liveInTurbo = CaptureState(context, *_controller);
    _b->RunTStates(2 * 71680 + 30000, /*skipBreakpoints=*/true);
    ASSERT_EQ(memory->DirectReadFromZ80Memory(0x9000), block[1]) << "the trap loaded the block";
    ASSERT_EQ(_controller->GetSessionInfo().externalEventCount, events + 1) << "as one edit";
    const ttd::TTDTimePoint afterTrap = _controller->CurrentPosition();
    const MachineState liveAfterTrap = CaptureState(context, *_controller);
    ASSERT_TRUE(features->setFeature(Features::kTurboMode, false));
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint later = _controller->CurrentPosition();
    const MachineState liveLater = CaptureState(context, *_controller);
    _controller->StopRecording();

    auto expectSame = [&](const MachineState& seen, const MachineState& live, const char* where) {
        EXPECT_EQ(std::memcmp(&seen.cpu, &live.cpu, sizeof(seen.cpu)), 0)
            << where << ": CPU (pc " << seen.cpu.pc << " vs " << live.cpu.pc << ")";
        EXPECT_TRUE(seen.ram == live.ram) << where << ": RAM";
        for (const auto& [id, bytes] : live.devices)
            EXPECT_TRUE(seen.devices.count(id) && seen.devices.at(id) == bytes) << where << ": device " << int(id);
    };
    ASSERT_TRUE(_controller->SeekTo(inTurbo, nullptr));
    expectSame(CaptureState(context, *_controller), liveInTurbo, "a seek into the turbo stretch");
    ASSERT_TRUE(_controller->SeekTo(afterTrap, nullptr));
    expectSame(CaptureState(context, *_controller), liveAfterTrap, "a seek across the trap in turbo");
    ASSERT_TRUE(_controller->SeekTo(later, nullptr));
    expectSame(CaptureState(context, *_controller), liveLater, "a seek after turbo");
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
/// M3 (Q4): a recording paused for browsing is still the user's recording: a media change waits for the next frame
/// boundary the machine runs and becomes an event of the recording there; the paused history stays as it is
TEST_F(TimeTravelController_Test, APausedRecordingTakesAMediaChangeAtItsNextFrameBoundary)
{
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(4, /*skipBreakpoints=*/true);
    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(1)->time.frame, 0}, nullptr));
    ASSERT_TRUE(_controller->GetSessionInfo().recordingPaused);
    const size_t kept = _controller->GetCheckpointCount();

    EXPECT_EQ(_b->MachineOps().Mode(), coordinator::MachineMode::Recording);
    std::string error;
    EXPECT_TRUE(_b->LoadTape(TestPathHelper::GetTestDataPath("contention/halt2int-v3/halt2int.tap"), &error)) << error;
    EXPECT_TRUE(_b->GetContext()->pMediaManager->Info("tape")->pending) << "waits for the next frame boundary";
    EXPECT_EQ(_controller->GetCheckpointCount(), kept) << "the paused recording's history stays";
    EXPECT_TRUE(_controller->GetSessionInfo().recordingPaused);
}

/// BUGS.md 2026-10-05 #3: the history of a stopped recording reaches to where
/// it stopped, inside the frame after its last checkpoint - a bookmark at the
/// present works right after the stop and a seek there shows the machine as it
/// stopped; while recording a bookmark at the present works too
TEST_F(TimeTravelController_Test, TheHistoryReachesWhereTheRecordingStopped)
{
    EmulatorContext* context = _b->GetContext();
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    _b->RunTStates(20000, /*skipBreakpoints=*/true);
    std::string err;
    EXPECT_TRUE(_controller->AddBookmark(_controller->CurrentPosition(), "while-recording", &err)) << err;
    _b->RunTStates(5000, /*skipBreakpoints=*/true);
    _controller->StopRecording();
    const ttd::TTDTimePoint stopped = _controller->CurrentPosition();
    const MachineState live = CaptureState(context, *_controller);
    EXPECT_EQ(_controller->SessionEndPosition(), stopped);

    ASSERT_TRUE(_controller->AddBookmark(stopped, "stopped", &err)) << err;
    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(1)->time.frame, 0}, nullptr));
    ttd::TTDSeekResult r;
    ASSERT_TRUE(_controller->SeekToBookmark("stopped", &r));
    EXPECT_EQ(_controller->CurrentPosition(), stopped);
    const MachineState seen = CaptureState(context, *_controller);
    EXPECT_EQ(std::memcmp(&seen.cpu, &live.cpu, sizeof(seen.cpu)), 0);
    EXPECT_TRUE(seen.ram == live.ram);
    EXPECT_TRUE(seen.devices == live.devices);
}

/// BUGS.md 2026-10-05 #2 (guard): a recording started inside a frame takes its
/// first checkpoint there; a seek replaying from it inside its frame reproduces
/// every device
TEST_F(TimeTravelController_Test, ASeekFromAMidFrameFirstCheckpointIsExact)
{
    EmulatorContext* context = _b->GetContext();
    _b->RunTStates(12345, /*skipBreakpoints=*/true);
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunTStates(20000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint at = _controller->CurrentPosition();
    const MachineState live = CaptureState(context, *_controller);
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    _controller->StopRecording();

    ASSERT_TRUE(_controller->SeekTo({_controller->GetCheckpoint(2)->time.frame, 0}, nullptr));
    ASSERT_TRUE(_controller->SeekTo(at, nullptr));   // replays from the first checkpoint
    const MachineState seen = CaptureState(context, *_controller);
    EXPECT_EQ(std::memcmp(&seen.cpu, &live.cpu, sizeof(seen.cpu)), 0);
    EXPECT_TRUE(seen.ram == live.ram);
    for (const auto& [id, state] : live.devices)
        EXPECT_TRUE(seen.devices.at(id) == state) << "device " << int(id);
}

/// BUGS.md 2026-10-05 #2: running forward from a mid-frame first checkpoint
/// across frame boundaries - by frames and instruction by instruction -
/// reproduces every device of the live run. TurboSound FM's render cursor held
/// a re-anchor pending for the next frame start (the sound configuration set
/// just before the recording) that the blob did not carry
TEST_F(TimeTravelController_Test, RunningFromAMidFrameFirstCheckpointIsExact)
{
    EmulatorContext* context = _b->GetContext();
    _b->RunTStates(12345, /*skipBreakpoints=*/true);
    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    _b->RunTStates(20000, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint at = _controller->CurrentPosition();
    const MachineState live = CaptureState(context, *_controller);
    _controller->StopRecording();

    auto expectLive = [&](const char* how) {
        const MachineState seen = CaptureState(context, *_controller);
        EXPECT_EQ(std::memcmp(&seen.cpu, &live.cpu, sizeof(seen.cpu)), 0) << how;
        EXPECT_TRUE(seen.ram == live.ram) << how;
        for (const auto& [id, state] : live.devices)
            EXPECT_TRUE(seen.devices.at(id) == state) << how << ": device " << int(id);
    };
    ASSERT_TRUE(_controller->SeekTo(_controller->GetCheckpoint(0)->time, nullptr));
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    _b->RunTStates(20000, /*skipBreakpoints=*/true);
    ASSERT_EQ(_controller->CurrentPosition(), at);
    expectLive("by frames");

    ASSERT_TRUE(_controller->SeekTo(_controller->GetCheckpoint(0)->time, nullptr));
    while (_controller->CurrentPosition() < at)
        _b->RunSingleCPUCycle(true);
    ASSERT_EQ(_controller->CurrentPosition(), at);
    expectLive("instruction by instruction");
}

/// Step 3c: a recording is written as it records into a folder of its own under
/// the recording root. The folder lives as long as its session: a new session,
/// a discard and a clean close of the instance delete it (a crash would leave it
/// to the startup cleanup). A black box keeps only its window's segment files
/// ~150 ms: the fixture boots two machines (~100 ms in every test here)
TEST_F(TimeTravelController_Test, RecordingFoldersLiveAsLongAsTheirSession)
{
    const std::string root = TestPathHelper::GetUniqueTestScratchPath("ttd-recordings");
    _controller->SetShadowRecordingRoot(root);

    ASSERT_TRUE(_controller->StartRecording());
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    const std::string first = _controller->ShadowRecordingFolder();
    ASSERT_FALSE(first.empty());
    EXPECT_TRUE(FileHelper::IsFolder(first));
    _controller->StopRecording();
    EXPECT_TRUE(FileHelper::IsFolder(first)) << "a stopped session keeps its folder";
    // The next folder may take the same name (a second's resolution): a marker tells them apart
    const std::string marker = first + "/marker";
    std::ofstream(FileHelper::ToFsPath(marker)) << "first";

    ASSERT_TRUE(_controller->StartRecording());   // a new session
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    const std::string second = _controller->ShadowRecordingFolder();
    EXPECT_FALSE(std::filesystem::exists(FileHelper::ToFsPath(marker))) << "the previous session's folder went with it";
    EXPECT_TRUE(FileHelper::IsFolder(second));
    _controller->StopRecording();
    _controller->InvalidateSession("test discard");
    EXPECT_FALSE(FileHelper::IsFolder(second)) << "a discarded session's folder goes";

    // A black box with a small window: its oldest segment files go as it moves on
    _controller->SetBlackBox(true, 1);
    ASSERT_TRUE(_controller->StartRecording());
    _controller->SetHistoryLimit(8, 0);   // segments of a frame
    _b->RunNFrames(30, /*skipBreakpoints=*/true);
    _controller->StopRecording();
    const std::string third = _controller->ShadowRecordingFolder();
    size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(FileHelper::ToFsPath(third)))
        files += entry.path().extension() == ".ttd";
    EXPECT_GT(files, 0u);
    EXPECT_LE(files, _controller->GetEngine().Segments().size() + 1) << "only the window's segments stay on disk";

    // A clean close of the instance
    _controller.reset();
    EXPECT_FALSE(FileHelper::IsFolder(third)) << "an unsaved recording goes with its instance";
    _controller = std::make_unique<ttd::TimeTravelController>(_b->GetContext());
    _b->GetContext()->pTimeTravelHooks = _controller.get();
    _b->GetContext()->ttdWriteSink = _controller.get();
}

/// Owner decision 2026-10-07: a background recording (the black box, a
/// debugger's history) refuses nothing. A tape load is allowed; it ends the
/// session - the recording up to it stays in its folder - and the next session
/// starts at the next frame
TEST_F(TimeTravelController_Test, ABackgroundRecordingRefusesNothing)
{
    _controller->SetShadowRecordingRoot(TestPathHelper::GetUniqueTestScratchPath("ttd-background"));
    _controller->SetBlackBox(true, 1);
    ASSERT_TRUE(_controller->StartRecording());
    EXPECT_TRUE(_controller->IsBackgroundSession());
    _b->RunNFrames(3, /*skipBreakpoints=*/true);
    const std::string before = _controller->ShadowRecordingFolder();
    ASSERT_FALSE(before.empty());

    EXPECT_EQ(_b->RecordingGuard(ttd::TTDGuardedAction::LoadTape), "");
    std::string error;
    ASSERT_TRUE(_b->LoadTape(TestPathHelper::GetTestDataPath("contention/halt2int-v3/halt2int.tap"), &error)) << error;
    EXPECT_FALSE(_controller->IsRecording()) << "the tape ended the session";
    EXPECT_EQ(_controller->GetSessionInfo().lastStopReason, "tape-load");
    EXPECT_NE(ttd::TTDRecordingFolder::Open(before), nullptr) << "the recording up to the tape stays on disk";

    _b->RunNFrames(1, /*skipBreakpoints=*/true);
    EXPECT_TRUE(_controller->IsRecording()) << "the next session started at the frame boundary";
    EXPECT_NE(_controller->ShadowRecordingFolder(), before);
    _controller->SetBlackBox(false);
}

/// Owner decision 2026-10-07: a debugger (DeZog) uses the recording that runs -
/// an explicit one stays explicit and protected and outlives the debugger -
/// and with none starts a background one, which stops when the debugger leaves
TEST_F(TimeTravelController_Test, ADebuggerUsesTheRecordingThatRuns)
{
    ASSERT_TRUE(_controller->StartRecording());
    EXPECT_FALSE(_controller->IsBackgroundSession());
    ASSERT_TRUE(_controller->HoldBackgroundRecording());
    EXPECT_FALSE(_controller->IsBackgroundSession()) << "the user's recording stays explicit";
    EXPECT_FALSE(_b->RecordingGuard(ttd::TTDGuardedAction::LoadTape).empty()) << "and protected";
    _controller->ReleaseBackgroundRecording();
    EXPECT_TRUE(_controller->IsRecording()) << "the user's recording outlives the debugger";
    _controller->StopRecording();
    _controller->InvalidateSession("test: the next case");

    ASSERT_TRUE(_controller->HoldBackgroundRecording());
    EXPECT_TRUE(_controller->IsRecording());
    EXPECT_TRUE(_controller->IsBackgroundSession());
    EXPECT_EQ(_b->RecordingGuard(ttd::TTDGuardedAction::LoadTape), "") << "a background recording refuses nothing";
    _b->RunNFrames(2, /*skipBreakpoints=*/true);
    _controller->ReleaseBackgroundRecording();
    EXPECT_FALSE(_controller->IsRecording()) << "the debugger's recording stops with it";
    EXPECT_GT(_controller->GetCheckpointCount(), 1u) << "its history kept";
}
