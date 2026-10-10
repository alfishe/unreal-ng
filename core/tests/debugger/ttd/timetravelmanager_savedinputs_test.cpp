#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttddumpformat.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/memory/memory.h"

/// The replay inputs of a session - the input journal (keyboard, Kempston
/// Mouse, General Sound host stimuli) and the external-event markers (replay
/// barriers) - are saved in the .ttd file (header bits 6 and 7) and loaded
/// back. Before this, both lived only in memory and DeserializeSession cleared
/// them: a loaded session re-executed history inside a frame without the
/// recorded input, and seeks crossed former barriers silently
/// (ttd-offline-analysis.md O-1).
///
/// The central check loads a file into a fresh emulator instance and replays
/// a program that polls the keyboard in a tight loop with interrupts off and
/// logs, per key-state change, its iteration counter: a journal time off by a
/// single instruction changes the log. The same file stripped of the sections
/// (an older writer's file) must diverge - so the check is not vacuous.
///
/// Runtime justification: two emulator instances per replay test, a 150-frame
/// boot and about 20 recorded frames (~0.5 s).
namespace
{
using ttd::TTDExternalEvent;
using ttd::TTDExternalEventKind;
using ttd::TTDInputEvent;
using ttd::TTDInputKind;
using SeekHaltReason = ttd::TimeTravelController::TTDSeekHaltReason;
using SeekResult = ttd::TimeTravelController::TTDSeekResult;

constexpr uint16_t kPollerAddress = 0x8000;
constexpr uint16_t kPollerLog = 0x9000;
constexpr int kBootFrames = 150;

struct Point
{
    uint64_t frame = 0;
    uint32_t tInFrame = 0;
    ttd::TTDCpuState cpu;
    uint64_t ramHash = 0;
    uint8_t matrix[8] = {};
    uint8_t mouseX = 0;
    uint8_t mouseY = 0;
};

Point Observe(EmulatorContext* context)
{
    const Z80* z80 = context->pCore->GetZ80();
    Point p;
    p.frame = context->emulatorState.frame_counter;
    p.tInFrame = z80->t;
    p.cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
    const size_t ramBytes = static_cast<size_t>(context->config.ramsize) * 1024u;
    p.ramHash = ttd::HashBytes(context->pMemory->RAMBase(), ramBytes);
    std::memcpy(p.matrix, reinterpret_cast<KeyboardCUT*>(context->pKeyboard)->_keyboardMatrixState, sizeof(p.matrix));
    p.mouseX = context->pMouse->GetX();
    p.mouseY = context->pMouse->GetY();
    return p;
}

bool SamePoint(const Point& a, const Point& b)
{
    return a.frame == b.frame && a.tInFrame == b.tInFrame && std::memcmp(&a.cpu, &b.cpu, sizeof(a.cpu)) == 0 &&
           a.ramHash == b.ramHash && std::memcmp(a.matrix, b.matrix, sizeof(a.matrix)) == 0 && a.mouseX == b.mouseX &&
           a.mouseY == b.mouseY;
}

void ExpectSameInputs(const std::vector<TTDInputEvent>& actual, const std::vector<TTDInputEvent>& expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); i++)
    {
        const TTDInputEvent& a = actual[i];
        const TTDInputEvent& e = expected[i];
        EXPECT_EQ(a.time, e.time) << "event " << i;
        EXPECT_EQ(a.kind, e.kind) << "event " << i;
        EXPECT_EQ(a.key, e.key) << "event " << i;
        EXPECT_EQ(a.pressed, e.pressed) << "event " << i;
        EXPECT_EQ(a.dx, e.dx) << "event " << i;
        EXPECT_EQ(a.dy, e.dy) << "event " << i;
        EXPECT_EQ(a.buttonMask, e.buttonMask) << "event " << i;
        EXPECT_EQ(a.wheelSteps, e.wheelSteps) << "event " << i;
        EXPECT_EQ(a.value, e.value) << "event " << i;
    }
}

void ExpectSameMarkers(const std::vector<TTDExternalEvent>& actual, const std::vector<TTDExternalEvent>& expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); i++)
    {
        EXPECT_EQ(actual[i].time, expected[i].time) << "marker " << i;
        EXPECT_EQ(actual[i].kind, expected[i].kind) << "marker " << i;
        EXPECT_STREQ(actual[i].reason, expected[i].reason) << "marker " << i;
    }
}
}  // namespace

class TimeTravelManager_SavedInputs_Test : public ::testing::Test
{
protected:
    /// One machine instance with TTD on
    struct Machine
    {
        Emulator* emulator = nullptr;
        EmulatorContext* context = nullptr;
        ttd::TimeTravelController* ttd = nullptr;

        bool Create()
        {
            emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
            if (!emulator)
                return false;
            context = emulator->GetContext();
            ttd = context->pTimeTravelController;
            FeatureManager* features = emulator->GetFeatureManager();
            features->setFeature(Features::kDebugMode, true);
            features->setFeature(Features::kTimeTravel, true);
            context->pMemory->UpdateFeatureCache();
            return ttd != nullptr;
        }

        void Destroy()
        {
            if (emulator)
                EmulatorTestHelper::CleanupEmulator(emulator);
            emulator = nullptr;
        }

        /// Run forward synchronously to exactly the recorded position
        void RunTo(const Point& target)
        {
            const EmulatorState* state = &context->emulatorState;
            const uint64_t frame = target.frame;
            const uint32_t t = target.tInFrame;
            emulator->RunUntilCondition([state, frame, t](const Z80State& z80)
                                        { return state->frame_counter > frame || (state->frame_counter == frame && z80.t >= t); });
        }

        std::string Save()
        {
            std::ostringstream out;
            std::string err;
            EXPECT_TRUE(ttd->SerializeSession(out, err)) << err;
            return out.str();
        }

        bool Load(const std::string& file, std::string* errOut = nullptr)
        {
            std::istringstream in(file);
            std::string err;
            const bool ok = ttd->DeserializeSession(in, err);
            if (errOut)
                *errOut = err;
            return ok;
        }
    };

    Machine _rec;    // records
    Machine _play;   // a fresh instance the file is loaded into

    void SetUp() override
    {
        ASSERT_TRUE(_rec.Create());
        ASSERT_TRUE(_play.Create());
    }

    void TearDown() override
    {
        _rec.Destroy();
        _play.Destroy();
    }

    /// A program for which every instruction matters: interrupts off, the
    /// keyboard port polled in a loop with an iteration counter, and on every
    /// change of the key state the counter is logged to RAM (#9000..)
    static void InstallKeyPoller(EmulatorContext* context)
    {
        static const uint8_t program[] = {
            0xF3,                    // 8000 DI
            0x21, 0x00, 0x00,        // 8001 LD HL,0
            0xDD, 0x21, 0x00, 0x90,  // 8004 LD IX,#9000
            0x0E, 0xFF,              // 8008 LD C,#FF        previous key state
            0x23,                    // 800A loop: INC HL
            0xAF,                    // 800B XOR A           all half-rows
            0xDB, 0xFE,              // 800C IN A,(#FE)
            0xF6, 0xE0,              // 800E OR #E0          keys only (bits 0-4)
            0xB9,                    // 8010 CP C
            0x28, 0xF7,              // 8011 JR Z,loop
            0x4F,                    // 8013 LD C,A
            0xDD, 0x75, 0x00,        // 8014 LD (IX+0),L     log: counter, state
            0xDD, 0x74, 0x01,        // 8017 LD (IX+1),H
            0xDD, 0x71, 0x02,        // 801A LD (IX+2),C
            0xDD, 0x23,              // 801D INC IX
            0xDD, 0x23,              // 801F INC IX
            0xDD, 0x23,              // 8021 INC IX
            0x18, 0xE5,              // 8023 JR loop
        };
        Z80* z80 = context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(static_cast<uint16_t>(kPollerAddress + i), program[i]);
        for (uint16_t a = kPollerLog; a < kPollerLog + 0x100; a++)
            z80->DirectWrite(a, 0);
        z80->pc = kPollerAddress;
    }

    static size_t PollerLogEntries(EmulatorContext* context)
    {
        size_t entries = 0;
        for (uint16_t a = kPollerLog + 2; a < kPollerLog + 0x100; a += 3)
        {
            if (context->pMemory->DirectReadFromZ80Memory(a) == 0)
                break;
            entries++;
        }
        return entries;
    }

    /// Record keys and mouse moves at arbitrary points inside frames while the
    /// poller runs; returns the recording's start frame and its end point
    void RecordPollerSession(uint64_t& startFrame, Point& end)
    {
        Emulator* e = _rec.emulator;
        DebugKeyboardManager* keys = _rec.context->pDebugManager->GetKeyboardManager();
        DebugMouseManager* mouse = _rec.context->pDebugManager->GetMouseManager();
        e->RunNFrames(kBootFrames);
        InstallKeyPoller(_rec.context);
        ASSERT_TRUE(_rec.ttd->StartRecording());
        startFrame = _rec.context->emulatorState.frame_counter;

        e->RunNCPUCycles(3001);
        keys->PressKey(ZXKEY_A);
        e->RunNCPUCycles(4567);
        keys->ReleaseKey(ZXKEY_A);
        ASSERT_TRUE(mouse->Move(5, -3).ok());
        e->RunNFrames(2);
        e->RunNCPUCycles(777);
        keys->PressKey(ZXKEY_Q);
        e->RunNCPUCycles(1234);
        keys->PressKey(ZXKEY_W);
        e->RunNFrames(1);
        keys->ReleaseKey(ZXKEY_Q);
        e->RunNCPUCycles(99);
        keys->ReleaseKey(ZXKEY_W);
        ASSERT_TRUE(mouse->Move(-2, 7).ok());
        e->RunNFrames(3);
        e->RunNCPUCycles(500);

        end = Observe(_rec.context);
        _rec.ttd->StopRecording();
        ASSERT_GE(_rec.ttd->GetInputJournal().Size(), 8u);
        ASSERT_GE(PollerLogEntries(_rec.context), 4u) << "the poller never saw the keys - the replay check would be vacuous";
    }
};

// ---------------------------------------------------------------------------
// The file carries every field
// ---------------------------------------------------------------------------

TEST_F(TimeTravelManager_SavedInputs_Test, EveryInputKindAndMarkerFieldSurvivesTheFile)
{
    _rec.emulator->RunNFrames(2);
    ASSERT_TRUE(_rec.ttd->StartRecording());

    // One event of every kind, each with distinctive field values, applied at
    // different points of the machine's time (synchronous mode: applied and
    // journaled at once)
    std::vector<TTDInputEvent> submitted;
    auto submit = [&](TTDInputEvent ev)
    {
        ASSERT_TRUE(_rec.ttd->SubmitLiveInput(ev));
        _rec.emulator->RunNCPUCycles(311);
    };
    TTDInputEvent ev;
    ev.kind = TTDInputKind::Key; ev.key = ZXKEY_Z; ev.pressed = true; submit(ev);
    ev = {}; ev.kind = TTDInputKind::Key; ev.key = ZXKEY_Z; ev.pressed = false; submit(ev);
    ev = {}; ev.kind = TTDInputKind::MouseMove; ev.dx = -32768; ev.dy = 32767; submit(ev);
    ev = {}; ev.kind = TTDInputKind::MouseButtons; ev.buttonMask = 0xFA; submit(ev);
    ev = {}; ev.kind = TTDInputKind::MouseWheel; ev.wheelSteps = -128; submit(ev);
    ev = {}; ev.kind = TTDInputKind::MouseCounters; ev.dx = 0x12; ev.dy = 0xFE; submit(ev);
    ev = {}; ev.kind = TTDInputKind::KeyboardReset; submit(ev);
    ev = {}; ev.kind = TTDInputKind::GSCommand; ev.value = 0x30; submit(ev);
    ev = {}; ev.kind = TTDInputKind::GSData; ev.value = 0xA5; submit(ev);
    ev = {}; ev.kind = TTDInputKind::GSNmi; submit(ev);
    ev = {}; ev.kind = TTDInputKind::GSResetCard; submit(ev);
    ev = {}; ev.kind = TTDInputKind::GSReset; submit(ev);

    // One marker of every kind, including an unclassified one, an empty
    // reason, a reason of exactly 63 characters and an over-long one (stored
    // truncated at 63)
    const std::string reason63(63, 'r');
    const std::string reason80(80, 'x');
    _rec.ttd->RecordExternalEvent(TTDExternalEventKind::TapeControl, "Tape play");
    _rec.emulator->RunNCPUCycles(100);
    _rec.ttd->RecordExternalEvent(TTDExternalEventKind::DiskWrite, reason63.c_str());
    _rec.emulator->RunNCPUCycles(100);
    _rec.ttd->RecordExternalEvent(TTDExternalEventKind::DebuggerEdit, "");
    _rec.emulator->RunNCPUCycles(100);
    _rec.ttd->RecordExternalEvent(TTDExternalEventKind::HardwareReset, reason80.c_str());
    _rec.emulator->RunNCPUCycles(100);
    _rec.ttd->RecordExternalEvent(TTDExternalEventKind::Other, "Other");
    _rec.emulator->RunNFrames(1);
    _rec.ttd->StopRecording();

    const std::vector<TTDInputEvent> inputs = _rec.ttd->GetInputJournal().Events();
    const std::vector<TTDExternalEvent> markers = _rec.ttd->GetExternalEvents().SnapshotEvents();
    ASSERT_EQ(inputs.size(), 12u);
    ASSERT_GE(markers.size(), 5u);

    const std::string file = _rec.Save();

    std::string err;
    ASSERT_TRUE(_play.Load(file, &err)) << err;
    ExpectSameInputs(_play.ttd->GetInputJournal().Events(), inputs);
    ExpectSameMarkers(_play.ttd->GetExternalEvents().SnapshotEvents(), markers);

    const auto info = _play.ttd->GetSessionInfo();
    EXPECT_EQ(info.inputEventCount, inputs.size());
    EXPECT_EQ(info.externalEventCount, markers.size());
    EXPECT_TRUE(info.inputHistoryComplete);

    // The loaded session saves the same inputs and markers again
    const std::string again = _play.Save();
    ASSERT_TRUE(_play.Load(again, &err)) << err;
    ExpectSameInputs(_play.ttd->GetInputJournal().Events(), inputs);
    ExpectSameMarkers(_play.ttd->GetExternalEvents().SnapshotEvents(), markers);
}

TEST_F(TimeTravelManager_SavedInputs_Test, EmptyJournalsAreWrittenSoAQuietSessionLoadsComplete)
{
    ASSERT_TRUE(_rec.ttd->StartRecording());
    _rec.emulator->RunNFrames(3);
    _rec.ttd->StopRecording();
    ASSERT_EQ(_rec.ttd->GetInputJournal().Size(), 0u);

    const std::string file = _rec.Save();

    std::string err;
    ASSERT_TRUE(_play.Load(file, &err)) << err;
    const auto info = _play.ttd->GetSessionInfo();
    EXPECT_EQ(info.inputEventCount, 0u);
    EXPECT_EQ(info.externalEventCount, 0u);
    EXPECT_TRUE(info.inputHistoryComplete) << "no input happened - the history is complete, not unknown";
}

// ---------------------------------------------------------------------------
// The point of it: a loaded session replays exactly
// ---------------------------------------------------------------------------

/// Keys and mouse moves applied inside frames while recording are replayed,
/// from the file, in a different emulator instance: the poller logs the same
/// iteration counters, so the machine arrives at the recorded end point in the
/// same state - CPU, RAM, keyboard matrix, mouse
TEST_F(TimeTravelManager_SavedInputs_Test, LoadedSessionReplaysTheRecordedInputExactlyInAFreshInstance)
{
    uint64_t startFrame = 0;
    Point recorded;
    RecordPollerSession(startFrame, recorded);
    if (HasFatalFailure())
        return;

    const std::string file = _rec.Save();
    std::string err;
    ASSERT_TRUE(_play.Load(file, &err)) << err;
    ASSERT_TRUE(_play.ttd->SeekTo({startFrame, 0}));
    _play.RunTo(recorded);

    const Point replayed = Observe(_play.context);
    EXPECT_TRUE(SamePoint(replayed, recorded)) << "the replay from the file diverged from the recording";
    EXPECT_EQ(replayed.ramHash, recorded.ramHash) << "RAM differs: the poller saw different input";
    EXPECT_EQ(PollerLogEntries(_play.context), PollerLogEntries(_rec.context));
}

/// Replay barriers keep stopping seeks after a save and a load: the seek halts
/// at the marker, with its kind, reason and time, exactly as in the recording
/// instance
TEST_F(TimeTravelManager_SavedInputs_Test, LoadedSessionKeepsItsReplayBarriers)
{
    _rec.emulator->RunNFrames(2);
    ASSERT_TRUE(_rec.ttd->StartRecording());
    _rec.emulator->RunNFrames(2);
    _rec.emulator->RunNCPUCycles(2000);
    _rec.ttd->RecordExternalEvent(TTDExternalEventKind::DebuggerEdit, "test poke");
    const ttd::TTDTimePoint markerAt = _rec.ttd->GetExternalEvents().SnapshotEvents().back().time;
    _rec.emulator->RunNFrames(3);
    _rec.ttd->StopRecording();

    const ttd::TTDTimePoint behind{markerAt.frame, markerAt.tInFrame + 500};
    SeekResult live;
    EXPECT_FALSE(_rec.ttd->SeekTo(behind, &live));
    ASSERT_EQ(live.haltReason, SeekHaltReason::ExternalEvent);
    EXPECT_EQ(live.blockingMarker.time, markerAt) << "the seek names the marker it stopped at";
    EXPECT_EQ(live.blockingMarker.kind, TTDExternalEventKind::DebuggerEdit);
    EXPECT_STREQ(live.blockingMarker.reason, "test poke");

    const std::string file = _rec.Save();
    std::string err;
    ASSERT_TRUE(_play.Load(file, &err)) << err;
    SeekResult loaded;
    EXPECT_FALSE(_play.ttd->SeekTo(behind, &loaded));
    EXPECT_EQ(loaded.haltReason, SeekHaltReason::ExternalEvent) << "the loaded session crossed the barrier";
    EXPECT_EQ(loaded.blockingMarker.time, live.blockingMarker.time);
    EXPECT_EQ(loaded.blockingMarker.kind, TTDExternalEventKind::DebuggerEdit);
    EXPECT_STREQ(loaded.blockingMarker.reason, "test poke");
    EXPECT_EQ(loaded.arrivedAt, live.arrivedAt);
}
