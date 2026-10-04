/// @file timetravelmanager_rzxplayback_test.cpp
/// @brief TTD records while an RZX plays (Phase 3 Step 2, RZX requirements
/// RZ-F19): the playback position is a device state in every checkpoint
/// (PeripheralId::RzxPlayback), so a seek back lands inside the playback and
/// it plays on from there; each RZX frame end is an InterruptFrame fact in
/// the engine, and a seek to "RZX frame N" through it lands on the machine
/// the RZX player's own keyframe seek (RzxKeyframeStore) gives.
///
/// Plays real RZX Archive recordings for a few hundred frames: slower than
/// the 50 ms guideline, one acceptance check per scenario.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/rzx/rzxplayer.h"
#include "loaders/rzx/rzxreader.h"

namespace
{
std::string Fixture(const std::string& name)
{
    return (TestPathHelper::FindProjectRoot() / "testdata" / "loaders" / "rzx" / name).string();
}

struct MachineState
{
    ttd::TTDCpuState cpu;
    std::vector<uint8_t> ram;
    uint8_t p7FFD = 0;
    bool playing = false;
    uint64_t rzxFrame = 0;
    uint32_t rzxFetches = 0;
    uint64_t frame = 0;
    uint32_t t = 0;
};

MachineState Capture(Emulator* emulator)
{
    EmulatorContext* context = emulator->GetContext();
    MachineState s;
    s.cpu = ttd::CaptureCpuState(*context->pCore->GetZ80());
    s.frame = context->emulatorState.frame_counter;
    s.t = context->pCore->GetZ80()->t;
    const size_t pages = context->config.ramsize / 16;   // KB
    s.ram.assign(context->pMemory->RAMPageAddress(0), context->pMemory->RAMPageAddress(0) + pages * 0x4000);
    s.p7FFD = context->emulatorState.p7FFD;
    s.playing = context->rzxPlayer != nullptr;
    const rzx::PlayerStatus status = emulator->GetRzxStatus().player;
    s.rzxFrame = status.frame;
    if (context->rzxPlayer)
        s.rzxFetches = context->rzxPlayer->Fetches();
    return s;
}

void ExpectSame(const MachineState& want, const MachineState& got)
{
    EXPECT_EQ(std::memcmp(&want.cpu, &got.cpu, sizeof(want.cpu)), 0)
        << "CPU (PC " << want.cpu.pc << " vs " << got.cpu.pc << ")";
    EXPECT_EQ(want.frame, got.frame) << "video frame";
    EXPECT_EQ(want.t, got.t) << "T in the frame";
    EXPECT_EQ(want.p7FFD, got.p7FFD) << "#7FFD";
    EXPECT_EQ(want.playing, got.playing) << "the playback runs";
    EXPECT_EQ(want.rzxFrame, got.rzxFrame) << "RZX frame";
    EXPECT_EQ(want.rzxFetches, got.rzxFetches) << "fetches in the RZX frame";
    ASSERT_EQ(want.ram.size(), got.ram.size());
    size_t diffs = 0;
    for (size_t i = 0; i < want.ram.size(); ++i)
        diffs += want.ram[i] != got.ram[i];
    EXPECT_EQ(diffs, 0u) << "RAM bytes differ";
}

/// A machine playing an RZX recording, with TTD and a shadow engine
struct Machine
{
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelManager* ttd = nullptr;

    Machine(const std::string& model, const std::string& rzxPath)
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        if (!emulator)
            return;
        context = emulator->GetContext();
        ttd = context->pTimeTravelManager;
        FeatureManager* features = emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        const rzx::PlayResult played = emulator->PlayRzx(rzxPath);
        EXPECT_TRUE(played.Ok()) << played.message;
    }
    ~Machine()
    {
        if (ttd)
        {
            ttd->SetReplaySource(nullptr);
            ttd->SetShadowEngine(nullptr);
        }
        if (emulator)
            EmulatorTestHelper::CleanupEmulator(emulator);
    }
    void Frames(unsigned n) { emulator->RunNFrames(n, /*skipBreakpoints=*/true); }
    uint64_t Frame() const { return context->emulatorState.frame_counter; }
    /// Where the machine stands (RunNFrames under an RZX playback stops a
    /// little past the frame boundary)
    ttd::TTDTimePoint Now() const
    {
        return {context->emulatorState.frame_counter, context->emulatorState.TtdTInFrame(context->pCore->GetZ80()->t)};
    }
};
}  // namespace

/// Record 120 video frames of a playing RZX; seeks back to frame starts land
/// on the machine the straight play had there (the playback position too),
/// and playing on from a seek reaches the straight play's end. Without the
/// playback in the checkpoint the replay consumed the player's values from
/// where it stood and desynced it
TEST(TimeTravelManager_RzxPlayback_Test, SeekBackLandsOnTheStraightPlay)
{
    Machine m("48K", Fixture("archive/ericfloaters.rzx"));
    ASSERT_NE(m.emulator, nullptr);
    m.Frames(20);   // into the recording
    ASSERT_TRUE(m.ttd->StartRecording());
    ASSERT_TRUE(m.ttd->GetPeripheralRegistry().IsRegistered(ttd::PeripheralId::RzxPlayback));

    std::vector<std::pair<ttd::TTDTimePoint, MachineState>> straight;
    for (unsigned i = 0; i < 120; ++i)
    {
        m.Frames(1);
        straight.emplace_back(m.Now(), Capture(m.emulator));
    }
    m.ttd->StopRecording();

    for (size_t i : {size_t(100), size_t(7), size_t(60), size_t(119)})
    {
        const auto& [at, want] = straight[i];
        SCOPED_TRACE("frame " + std::to_string(at.frame) + " T " + std::to_string(at.tInFrame));
        ASSERT_TRUE(m.ttd->SeekTo(at));
        ExpectSame(want, Capture(m.emulator));
    }
    EXPECT_EQ(m.emulator->GetRzxStatus().player.desyncs, 0u);
}

/// The playback stopped during the recording: a seek back into it plays the
/// RZX again (hooks in, the machine's frame interrupt masked), a seek past
/// the stop is live; the engine has the replay source of each stretch
TEST(TimeTravelManager_RzxPlayback_Test, SeekAcrossTheStopOfThePlayback)
{
    Machine m("48K", Fixture("archive/garfield.rzx"));
    ASSERT_NE(m.emulator, nullptr);
    ttd::TimeTravelEngine engine;
    m.ttd->SetShadowEngine(&engine);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(30);
    const ttd::TTDTimePoint inside = m.Now();
    const MachineState atInside = Capture(m.emulator);
    m.Frames(10);
    ASSERT_TRUE(m.emulator->StopRzx());
    m.Frames(15);
    const ttd::TTDTimePoint after = m.Now();
    const MachineState atAfter = Capture(m.emulator);
    m.Frames(5);
    m.ttd->StopRecording();

    ASSERT_TRUE(m.ttd->SeekTo(inside));
    ExpectSame(atInside, Capture(m.emulator));
    EXPECT_TRUE(m.context->pCore->GetZ80()->frameIntMasked) << "the recorded schedule replaces the frame INT";
    ASSERT_TRUE(m.ttd->SeekTo(after));
    ExpectSame(atAfter, Capture(m.emulator));
    EXPECT_FALSE(m.context->pCore->GetZ80()->frameIntMasked);

    ttd::TTDMachineTime insideT = 0, afterT = 0;
    ASSERT_TRUE(engine.Frames().Start(inside.frame, insideT));
    ASSERT_TRUE(engine.Frames().Start(after.frame, afterT));
    EXPECT_EQ(engine.ReplaySourceAt(insideT), ttd::TTDReplaySource::RzxPlayback);
    EXPECT_EQ(engine.ReplaySourceAt(afterT), ttd::TTDReplaySource::LiveInput);
}

/// Every RZX frame the session ended is an InterruptFrame fact: consecutive
/// RZX frame numbers, machine time growing, the interrupt flag as the player
/// counted it
TEST(TimeTravelManager_RzxPlayback_Test, EveryRzxFrameEndIsAFact)
{
    Machine m("48K", Fixture("archive/ericfloaters.rzx"));
    ASSERT_NE(m.emulator, nullptr);
    m.Frames(5);
    ttd::TimeTravelEngine engine;
    m.ttd->SetShadowEngine(&engine);
    const rzx::PlayerStatus before = m.emulator->GetRzxStatus().player;
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(50);
    m.ttd->StopRecording();
    const rzx::PlayerStatus after = m.emulator->GetRzxStatus().player;

    uint64_t facts = 0, interrupts = 0, expected = before.frame + 1;
    ttd::TTDMachineTime last = 0;
    for (const ttd::TTDEvent& ev : engine.Events().Events())
    {
        if (ev.kind != ttd::TTDEventKind::InterruptFrame)
            continue;
        uint64_t frame = 0;
        std::memcpy(&frame, ev.args, sizeof(frame));
        EXPECT_EQ(frame, expected++);
        EXPECT_GT(ev.machineTime, last);
        last = ev.machineTime;
        interrupts += ev.args[ttd::kInterruptFrameInterruptArg];
        ++facts;
    }
    EXPECT_EQ(facts, after.frame - before.frame);
    EXPECT_EQ(interrupts, after.interrupts - before.interrupts);
    ttd::TTDMachineTime at = 0;
    EXPECT_TRUE(engine.RzxFrameTime(after.frame, at));
    EXPECT_FALSE(engine.RzxFrameTime(before.frame, at)) << "ended before the session";
}

/// region <Parity with the RZX player's keyframe seek>

namespace
{
/// Record @p frames video frames of @p rzx from its start; for each target
/// RZX frame, a TTD seek to the engine's fact (from v1's data and from the
/// engine's) equals SeekRzx on a second machine
void ExpectParity(const std::string& model, const std::string& rzx, unsigned frames)
{
    SCOPED_TRACE(rzx);
    Machine m(model, rzx);
    ASSERT_NE(m.emulator, nullptr);
    ttd::TimeTravelEngine engine;
    m.ttd->SetShadowEngine(&engine);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(frames);
    m.ttd->StopRecording();
    const uint64_t ended = m.emulator->GetRzxStatus().player.frame;
    ASSERT_GT(ended, 10u);

    Machine reference(model, rzx);
    ASSERT_NE(reference.emulator, nullptr);
    for (uint64_t target : {ended / 2, uint64_t(1), ended - 1, ended / 3})
    {
        SCOPED_TRACE("RZX frame " + std::to_string(target));
        ttd::TTDMachineTime at = 0;
        ttd::TTDPosition pos;
        ASSERT_TRUE(engine.RzxFrameTime(target, at));
        ASSERT_TRUE(engine.PositionOf(at, pos));
        const ttd::TTDTimePoint point{pos.frame, static_cast<uint32_t>(pos.tInFrame)};

        std::string error;
        ASSERT_TRUE(reference.emulator->SeekRzx(target, &error)) << error;
        const MachineState want = Capture(reference.emulator);

        m.ttd->SetReplaySource(nullptr);
        ASSERT_TRUE(m.ttd->SeekTo(point));
        ExpectSame(want, Capture(m.emulator));
        m.ttd->SetReplaySource(&engine);
        ASSERT_TRUE(m.ttd->SeekTo(point));
        m.ttd->SetReplaySource(nullptr);
        ExpectSame(want, Capture(m.emulator));
    }
}
}  // namespace

/// The recordings in testdata (48K, 128K, +2, Pentagon; Spectaculator and SPIN)
TEST(TimeTravelManager_RzxPlayback_Test, SeekToAnRzxFrameEqualsTheKeyframeSeek)
{
    ExpectParity("48K", Fixture("archive/ericfloaters.rzx"), 150);
    ExpectParity("48K", Fixture("archive/garfield.rzx"), 150);
    ExpectParity("128K", Fixture("archive/greenberet.rzx"), 150);
    ExpectParity("128K", Fixture("archive/thundercats.rzx"), 150);
    ExpectParity("PLUS2", Fixture("archive/dargonscrypt.rzx"), 150);
    ExpectParity("PENTAGON", Fixture("archive/darkwingduck.rzx"), 150);
}

/// Every .rzx in $UNREAL_RZX_CORPUS (the 16 RZX Archive recordings), on the
/// model its start snapshot names
TEST(TimeTravelManager_RzxPlayback_Test, CorpusSeeksEqualTheKeyframeSeek)
{
    const char* folder = std::getenv("UNREAL_RZX_CORPUS");
    if (folder == nullptr)
        GTEST_SKIP() << "set UNREAL_RZX_CORPUS=<folder of .rzx files>";
    for (const auto& entry : std::filesystem::directory_iterator(folder))
    {
        if (entry.path().extension() != ".rzx")
            continue;
        auto file = std::make_shared<rzx::File>();
        std::string error;
        ASSERT_TRUE(RzxReader::ParseFile(entry.path().string(), *file, error)) << error;
        rzx::RzxSession::StartSnapshot start;
        rzx::PlayResult resolve;
        ASSERT_TRUE(rzx::RzxSession::ResolveStartSnapshot(*file, entry.path().string(), {}, start, resolve))
            << resolve.message;
        const TMemModel* model = Config::FindModelByEnum(start.machine.model);
        ASSERT_NE(model, nullptr);
        ExpectParity(model->ShortName, entry.path().string(), 600);
    }
}

/// endregion </Parity>
