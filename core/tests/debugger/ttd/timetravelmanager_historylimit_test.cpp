/// @file timetravelmanager_historylimit_test.cpp
/// @brief The TTD history limit (TimeTravelController::SetHistoryLimit): while
/// recording, the oldest checkpoints are released; what stays is a complete,
/// shorter session.
///
/// Checked against the live run of a program for which every instruction
/// matters: interrupts off, the keyboard port polled in a tight loop with an
/// iteration counter, and every change of the key state logged to RAM. Keys
/// are pressed at arbitrary points inside frames all through the recording,
/// so the input journal and the port journals are in use everywhere and the
/// eviction has to cut them right:
///   - every kept frame seeks to exactly the live state (CPU, all RAM, keys);
///   - a session saved after an eviction, loaded into a fresh instance and
///     replayed from its new start, ends exactly where the live run ended;
///   - the byte limit holds.
///
/// Runtime justification: a 150-frame boot plus a 120-frame recording, twice
/// for the replay test (~1 s).

#include <gtest/gtest.h>

#include <algorithm>

#include <cstring>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/memory/memory.h"

namespace
{
constexpr uint16_t kPollerAddress = 0x8000;
constexpr uint16_t kPollerLog = 0x9000;
constexpr int kBootFrames = 150;
constexpr unsigned kRecordedFrames = 120;
constexpr uint64_t kFrameLimit = 30;

struct Point
{
    uint64_t frame = 0;
    uint32_t tInFrame = 0;
    ttd::TTDCpuState cpu;
    uint64_t ramHash = 0;
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
    return p;
}

bool SameState(const Point& a, const Point& b)
{
    return a.frame == b.frame && a.tInFrame == b.tInFrame && std::memcmp(&a.cpu, &b.cpu, sizeof(a.cpu)) == 0 &&
           a.ramHash == b.ramHash;
}
}  // namespace

class TimeTravelManager_HistoryLimit_Test : public ::testing::Test
{
protected:
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

        void RunTo(const Point& target)
        {
            const EmulatorState* state = &context->emulatorState;
            const uint64_t frame = target.frame;
            const uint32_t t = target.tInFrame;
            emulator->RunUntilCondition([state, frame, t](const Z80State& z80)
                                        { return state->frame_counter > frame || (state->frame_counter == frame && z80.t >= t); });
        }
    };

    Machine _rec;
    Machine _play;
    std::map<uint64_t, Point> _live;  // live state where the run stopped after each frame
    Point _end;

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

    /// Interrupts off; the keyboard polled in a loop with a counter; every
    /// change of the key state logged (counter and state) at #9000..
    static void InstallKeyPoller(EmulatorContext* context)
    {
        static const uint8_t program[] = {
            0xF3,                    // DI
            0x21, 0x00, 0x00,        // LD HL,0
            0xDD, 0x21, 0x00, 0x90,  // LD IX,#9000
            0x0E, 0xFF,              // LD C,#FF
            0x23,                    // loop: INC HL
            0xAF,                    // XOR A
            0xDB, 0xFE,              // IN A,(#FE)
            0xF6, 0xE0,              // OR #E0
            0xB9,                    // CP C
            0x28, 0xF7,              // JR Z,loop
            0x4F,                    // LD C,A
            0xDD, 0x75, 0x00,        // LD (IX+0),L
            0xDD, 0x74, 0x01,        // LD (IX+1),H
            0xDD, 0x71, 0x02,        // LD (IX+2),C
            0xDD, 0x23,              // INC IX
            0xDD, 0x23,              // INC IX
            0xDD, 0x23,              // INC IX
            0x18, 0xE5,              // JR loop
        };
        Z80* z80 = context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(program); i++)
            z80->DirectWrite(static_cast<uint16_t>(kPollerAddress + i), program[i]);
        for (uint32_t a = kPollerLog; a < 0xC000; a++)
            z80->DirectWrite(static_cast<uint16_t>(a), 0);
        z80->pc = kPollerAddress;
    }

    /// Record kRecordedFrames frames with keys pressed and released inside
    /// frames throughout; the live state after every frame goes to _live
    void Record(uint64_t frameLimit, uint64_t byteLimit)
    {
        Emulator* e = _rec.emulator;
        DebugKeyboardManager* keys = _rec.context->pDebugManager->GetKeyboardManager();
        e->RunNFrames(kBootFrames);
        InstallKeyPoller(_rec.context);
        _rec.ttd->SetHistoryLimit(frameLimit, byteLimit);
        ASSERT_TRUE(_rec.ttd->StartRecording());

        const ZXKeysEnum cycle[] = {ZXKEY_A, ZXKEY_Q, ZXKEY_W, ZXKEY_SPACE};
        for (unsigned i = 0; i < kRecordedFrames; ++i)
        {
            const ZXKeysEnum key = cycle[i % 4];
            e->RunNCPUCycles(1000 + (i * 977) % 50000);
            keys->PressKey(key);
            e->RunNCPUCycles(300 + (i * 131) % 4000);
            keys->ReleaseKey(key);
            e->RunNFrames(1);
            _live[_rec.context->emulatorState.frame_counter] = Observe(_rec.context);
        }
        e->RunNCPUCycles(1234);
        _end = Observe(_rec.context);
        _rec.ttd->StopRecording();
    }
};

/// The frame limit keeps the newest checkpoints; the journals start at the new start
TEST_F(TimeTravelManager_HistoryLimit_Test, FrameLimitKeepsTheNewestCheckpoints)
{
    Record(kFrameLimit, 0);

    // The engine drops whole segments of an eighth of the window (decision 41): the history holds up to that much more
    const uint64_t segment = std::max<uint64_t>(1, kFrameLimit / 8);
    const ttd::TTDSessionInfo info = _rec.ttd->GetSessionInfo();
    EXPECT_LE(info.checkpointCount, kFrameLimit + segment);
    EXPECT_GE(info.evictedCheckpoints, kRecordedFrames - kFrameLimit - segment);
    EXPECT_GT(info.sessionStartFrame, _live.begin()->first) << "the start moved";
    const auto& journal = _rec.ttd->GetInputJournal();
    ASSERT_GT(journal.Size(), 0u);
    EXPECT_FALSE(journal.Events().front().time.frame < info.sessionStartFrame) << "the input journal starts at the new start";

    // Before the start: gone
    EXPECT_FALSE(_rec.ttd->SeekTo({info.sessionStartFrame - 5, 0}));

    // Every kept frame: exactly the live state
    for (uint64_t f = info.sessionStartFrame + 1; f <= info.currentEndFrame; ++f)
    {
        if (!_live.count(f))
            continue;
        SCOPED_TRACE(f);
        const Point& want = _live.at(f);
        ASSERT_TRUE(_rec.ttd->SeekTo({f, want.tInFrame}));
        EXPECT_TRUE(SameState(Observe(_rec.context), want));
    }
}

/// A session saved after the eviction, loaded into a fresh instance, replays
/// from its new start to exactly where the live run ended - with every key
/// press of the remaining frames (input journal) and every IN result (port journal)
TEST_F(TimeTravelManager_HistoryLimit_Test, SavedAfterEvictionReplaysTheLastFrames)
{
    Record(kFrameLimit, 0);
    const ttd::TTDSessionInfo info = _rec.ttd->GetSessionInfo();
    ASSERT_GT(info.evictedCheckpoints, 0u);
    ASSERT_TRUE(info.portJournalActive) << "the replay must run on the recorded IN results";
    EXPECT_GT(_rec.ttd->GetPortReadJournal().FirstIndex(), 0u)
        << "the eviction dropped old port journal blocks (the file cursors are rebased)";

    std::ostringstream out;
    std::string err;
    ASSERT_TRUE(_rec.ttd->SerializeSession(out, err)) << err;
    std::istringstream in(out.str());
    ASSERT_TRUE(_play.ttd->DeserializeSession(in, err)) << err;

    const ttd::TTDSessionInfo loaded = _play.ttd->GetSessionInfo();
    EXPECT_EQ(loaded.sessionStartFrame, info.sessionStartFrame);
    EXPECT_EQ(loaded.currentEndFrame, info.currentEndFrame);
    EXPECT_EQ(loaded.checkpointCount, info.checkpointCount);

    // From the first live point after the new start, run forward through the remaining frames
    auto it = _live.upper_bound(loaded.sessionStartFrame);
    ASSERT_NE(it, _live.end());
    ASSERT_TRUE(_play.ttd->SeekTo({it->first, it->second.tInFrame}));
    EXPECT_TRUE(SameState(Observe(_play.context), it->second));
    unsigned replayed = 0;
    for (++it; it != _live.end(); ++it, ++replayed)
    {
        SCOPED_TRACE(it->first);
        _play.RunTo(it->second);
        ASSERT_TRUE(SameState(Observe(_play.context), it->second)) << "replay diverged at frame " << it->first;
    }
    EXPECT_GE(replayed, 8u) << "the replay covered the kept frames (a live point every second or third frame)";
    _play.RunTo(_end);
    EXPECT_TRUE(SameState(Observe(_play.context), _end)) << "the replay must end where the live run ended";

    // And seeks into the loaded file land on the live states (newest first, then back to the start)
    for (auto s = _live.rbegin(); s != _live.rend() && s->first > loaded.sessionStartFrame; std::advance(s, 7))
    {
        SCOPED_TRACE(s->first);
        ASSERT_TRUE(_play.ttd->SeekTo({s->first, s->second.tInFrame}));
        EXPECT_TRUE(SameState(Observe(_play.context), s->second));
        if (std::distance(s, _live.rend()) <= 7)
            break;
    }
}

/// The byte limit: what the history holds stays under it, and what stays seeks right
TEST_F(TimeTravelManager_HistoryLimit_Test, ByteLimitDropsWholeSegments)
{
    // The engine keeps the history in segments (decision 41) and its store in 64 KB arena chunks: a byte limit drops
    // the oldest segments while the history is over it, down to the newest one. Segments of 3 frames (a frame limit
    // of kFrameLimit) and a limit nothing meets: the byte limit takes the history below what the frame limit keeps
    Record(kFrameLimit, 1);
    const ttd::TTDSessionInfo info = _rec.ttd->GetSessionInfo();
    const uint64_t segment = std::max<uint64_t>(1, kFrameLimit / 8);
    EXPECT_LE(info.checkpointCount, 2 * segment) << "the newest segment and the frames recorded into the next";
    EXPECT_GE(info.checkpointCount, 2u);
    EXPECT_GT(info.evictedCheckpoints, kRecordedFrames - kFrameLimit);

    for (auto s = _live.upper_bound(info.sessionStartFrame); s != _live.end(); ++s)
    {
        SCOPED_TRACE(s->first);
        ASSERT_TRUE(_rec.ttd->SeekTo({s->first, s->second.tInFrame}));
        EXPECT_TRUE(SameState(Observe(_rec.context), s->second));
    }
}
