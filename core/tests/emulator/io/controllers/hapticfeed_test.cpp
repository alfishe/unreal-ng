/// @file hapticfeed_test.cpp
/// @brief The core side of the haptic feed (haptics.md §3, §5, §8, §12) on a real machine: the breakpoint probes
/// (exec, read, write with every write seen, port-in, port-out) never stop the CPU and report each hit, the sound taps
/// give 8 kHz streams, a TTD seek holds the feed in the same call (Haptics_GateOnSeek), and the emulation is the same
/// bit for bit with the feed on and off, with nothing journaled (Haptics_NotJournaled).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/executionarbiter.h"
#include "emulator/io/controllers/controllermanager.h"
#include "emulator/io/controllers/hapticprobes.h"
#include "emulator/io/controllers/ihapticfeed.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/nextaudio.h"
#include "emulator/sound/soundmanager.h"
#include "haptics/hapticmixer.h"
#include "haptics/hapticprofile.h"
#include "haptics/profilehaptics.h"
#include "profiles/json.h"
#include "stdafx.h"

using namespace controllers;

namespace
{
/// One pass per frame (IM 2 with its own vector, so no ROM code runs): read #9000, take one off it, put it back
/// (a change undone within the frame), OUT it to #FE, IN from #FE
const uint8_t kProgram[] = {
    0xF3,              // 8000 DI
    0xED, 0x5E,        // 8001 IM 2
    0x3E, 0x81,        // 8003 LD A,#81
    0xED, 0x47,        // 8005 LD I,A
    0xFB,              // 8007 EI
    0x76,              // 8008 HALT
    0x3A, 0x00, 0x90,  // 8009 LD A,(#9000)
    0x3D,              // 800C DEC A
    0x32, 0x00, 0x90,  // 800D LD (#9000),A
    0x3C,              // 8010 INC A
    0x32, 0x00, 0x90,  // 8011 LD (#9000),A
    0xD3, 0xFE,        // 8014 OUT (#FE),A
    0xDB, 0xFE,        // 8016 IN A,(#FE)
    0xC3, 0x07, 0x80,  // 8018 JP #8007
};
constexpr uint16_t kAfterHalt = 0x8009;
constexpr uint16_t kByte = 0x9000;
constexpr uint8_t kLives = 5;

class RecordingFeed : public IHapticFeed
{
public:
    std::vector<HapticProbe> probes;
    std::vector<HapticStream> streams;
    std::vector<HapticFrame> frames;
    std::vector<bool> holds;

    void OnHapticFrame(const HapticFrame& frame) override
    {
        frames.push_back(frame);
    }
    std::vector<HapticProbe> Probes() const override
    {
        return probes;
    }
    std::vector<HapticStream> Streams() const override
    {
        return streams;
    }
    void OnHapticHold(bool held) override
    {
        holds.push_back(held);
    }
};

size_t ProbeCount(const BreakpointManager* breakpoints)
{
    size_t count = 0;
    for (const auto& [id, descriptor] : breakpoints->GetAllBreakpoints())
        count += descriptor != nullptr && descriptor->owner == HapticProbes::kOwner ? 1 : 0;
    return count;
}

std::vector<HapticProbe> AllProbes()
{
    std::vector<HapticProbe> probes(5);
    probes[0].kind = HapticProbeKind::Exec;
    probes[0].address = probes[0].end = kAfterHalt;
    probes[1].kind = HapticProbeKind::Read;
    probes[1].address = probes[1].end = kByte;
    probes[2].kind = HapticProbeKind::Write;
    probes[2].address = probes[2].end = kByte;
    probes[3].kind = HapticProbeKind::PortOut;
    probes[3].address = probes[3].end = 0x00FE;
    probes[3].portMask = 0x0001;
    probes[4].kind = HapticProbeKind::PortIn;
    probes[4].address = probes[4].end = 0x00FE;
    probes[4].portMask = 0x0001;
    return probes;
}
}  // namespace

class HapticFeed_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ControllerManager* _manager = nullptr;

    void SetUp() override
    {
        _emulator = Start();
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _manager = _context->pControllerManager;
        ASSERT_NE(_manager, nullptr);
    }

    void TearDown() override
    {
        if (_manager)
            _manager->SetHapticFeed(nullptr);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// A 48K running kProgram from its first instruction
    static Emulator* Start()
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        if (emulator == nullptr)
            return nullptr;
        EmulatorContext* context = emulator->GetContext();
        Memory* memory = context->pMemory;
        for (size_t i = 0; i < sizeof(kProgram); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), kProgram[i]);
        for (unsigned i = 0; i <= 256; i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8100 + i), 0x83);
        const uint8_t isr[] = {0xFB, 0xED, 0x4D};  // 8383 EI; RETI
        for (size_t i = 0; i < sizeof(isr); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8383 + i), isr[i]);
        memory->DirectWriteToZ80Memory(kByte, kLives);
        Z80* z80 = context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->sp = 0x7F00;
        return emulator;
    }

    static uint64_t HashNow(Emulator* emulator)
    {
        EmulatorContext* context = emulator->GetContext();
        Z80* z80 = context->pCore->GetZ80();
        const uint32_t ramBytes = static_cast<uint32_t>(context->config.ramsize) * 1024u;
        const uint64_t ram = ttd::HashBytes(context->pMemory->RAMBase(), ramBytes);
        return ttd::HashSnapshot(ttd::CaptureSnapshot(*static_cast<Z80State*>(z80), context->emulatorState, ram));
    }
};

TEST_F(HapticFeed_Test, ProbesSeeEveryAccessAndNeverStop)
{
    auto feed = std::make_shared<RecordingFeed>();
    feed->probes = AllProbes();
    feed->streams = {HapticStream::Beeper, HapticStream::Mix, HapticStream::Covox};
    _manager->SetHapticFeed(feed);
    BreakpointManager* breakpoints = _context->pDebugManager->GetBreakpointsManager();
    _emulator->RunNFrames(1, false);  // the first frame attaches the feed: the probes are armed from its end
    EXPECT_EQ(ProbeCount(breakpoints), 5u);
    EXPECT_TRUE(_emulator->GetFeatureManager()->debugModeInEffect()) << "memory probes hold the debug path";

    _emulator->RunNFrames(4, false);
    EXPECT_FALSE(_emulator->IsPaused()) << "a probe never stops the CPU";
    ASSERT_GE(feed->frames.size(), 4u);
    const HapticFrame& frame = feed->frames.back();
    ASSERT_EQ(frame.probeCount, 5u);
    EXPECT_EQ(frame.probeHits[0], 1u) << "exec: once a frame";
    EXPECT_EQ(frame.probeHits[1], 1u) << "read";
    EXPECT_EQ(frame.probeHits[2], 2u) << "write: both writes";
    EXPECT_EQ(frame.probeHits[3], 1u) << "port-out";
    EXPECT_EQ(frame.probeHits[4], 1u) << "port-in";
    ASSERT_EQ(frame.probeEventCount, 3u) << "the two writes and the OUT";
    EXPECT_EQ(frame.probeEvents[0].probe, 2u);
    EXPECT_EQ(frame.probeEvents[0].before, kLives);
    EXPECT_EQ(frame.probeEvents[0].value, kLives - 1) << "the decrease the frame end never sees";
    EXPECT_EQ(frame.probeEvents[1].before, kLives - 1);
    EXPECT_EQ(frame.probeEvents[1].value, kLives);
    EXPECT_EQ(frame.probeEvents[2].probe, 3u);
    EXPECT_EQ(frame.probeEvents[2].value, kLives) << "the byte the OUT wrote";

    ASSERT_EQ(frame.streamCount, 3u);
    // 8 kHz over a 69888-T frame (19.97 ms): 159.7 samples, so 159 or 160, and 799 over five frames
    unsigned five = 0;
    for (size_t i = feed->frames.size() - 4; i < feed->frames.size(); i++)
        five += feed->frames[i].streams[0].count;
    EXPECT_GE(frame.streams[0].count, 159u);
    EXPECT_LE(frame.streams[0].count, 160u);
    EXPECT_EQ(frame.streams[1].count, frame.streams[0].count) << "every tap the same frame";
    EXPECT_GE(five, 4u * 159u);
    EXPECT_EQ(frame.streams[2].count, 0u) << "a 48K has no Covox: its stream is empty";

    _manager->SetHapticFeed(nullptr);
    _emulator->RunNFrames(1, false);
    EXPECT_TRUE(ProbeCount(breakpoints) == 0) << "detached: disarmed";
    EXPECT_FALSE(_emulator->GetFeatureManager()->probeDebugHold());
}

TEST_F(HapticFeed_Test, ExecProbesFireOnTheInstructionStartOnly)
{
    // An exec probe is the instruction's first byte: the operand fetches of LD A,(#9000) at #8009 (#800A, #800B) run
    // every frame but are no instruction start
    auto feed = std::make_shared<RecordingFeed>();
    feed->probes.resize(3);
    const uint16_t addresses[] = {kAfterHalt, kAfterHalt + 1, kAfterHalt + 2};
    for (size_t i = 0; i < 3; i++)
    {
        feed->probes[i].kind = HapticProbeKind::Exec;
        feed->probes[i].address = feed->probes[i].end = addresses[i];
    }
    _manager->SetHapticFeed(feed);
    _emulator->RunNFrames(3, false);
    ASSERT_FALSE(feed->frames.empty());
    const HapticFrame& frame = feed->frames.back();
    ASSERT_EQ(frame.probeCount, 3u);
    EXPECT_EQ(frame.probeHits[0], 1u) << "the opcode";
    EXPECT_EQ(frame.probeHits[1], 0u) << "the operand's low byte";
    EXPECT_EQ(frame.probeHits[2], 0u) << "the operand's high byte";
}

TEST_F(HapticFeed_Test, ExecProbesKeepTheFastMemoryPath)
{
    // Exec probes need the instruction start's check only (Z80::execProbeHold): no debug mode, the fast memory
    // interface stays, and the probe still sees its instruction once a frame
    auto feed = std::make_shared<RecordingFeed>();
    feed->probes.resize(1);
    feed->probes[0].kind = HapticProbeKind::Exec;
    feed->probes[0].address = feed->probes[0].end = kAfterHalt;
    _manager->SetHapticFeed(feed);
    Z80* z80 = _context->pCore->GetZ80();
    _emulator->RunNFrames(3, false);
    EXPECT_TRUE(z80->execProbeHold);
    EXPECT_FALSE(z80->isDebugMode);
    EXPECT_FALSE(_emulator->GetFeatureManager()->debugModeInEffect()) << "an exec probe holds no debug path";
    EXPECT_TRUE(z80->MemIf == z80->FastMemIf || z80->MemIf == z80->FastContendedMemIf) << "the fast memory interface";
    ASSERT_FALSE(feed->frames.empty());
    EXPECT_EQ(feed->frames.back().probeHits[0], 1u) << "exec: once a frame";

    _manager->SetHapticFeed(nullptr);
    _emulator->RunNFrames(1, false);
    EXPECT_FALSE(z80->execProbeHold) << "detached: released";
}

TEST_F(HapticFeed_Test, ProbeDebugHoldSwitchesWithTheCpuHeld)
{
    // Arming the probes from another thread (the GUI, a WebAPI request) switches the CPU's debug flag, the memory
    // interface and the tracker's caches, all read by the running loop without locks: the switch holds the CPU, as a
    // feature change does (TSan flagged the bare writes)
    FeatureManager* features = _emulator->GetFeatureManager();
    _emulator->EnableTurboMode();
    _emulator->StartAsync();
    const uint64_t& frame = _context->emulatorState.frame_counter;
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsRunning() && frame > 1; }, std::chrono::seconds(10)));

    int held = 0;
    int ran = 0;
    for (int i = 0; i < 6; i++)
    {
        const uint64_t applied = _emulator->GetArbiter().GetSnapshot().applied;
        const uint64_t before = frame;
        features->setProbeDebugHold(i % 2 == 0);
        held += _emulator->GetArbiter().GetSnapshot().applied > applied ? 1 : 0;
        EXPECT_EQ(_context->pCore->GetZ80()->isDebugMode, features->debugModeInEffect());
        ran += TestWait::For([&] { return frame > before; }) ? 1 : 0;
    }
    _emulator->Stop();
    EXPECT_EQ(held, 6) << "every switch went through the arbiter";
    EXPECT_EQ(ran, 6) << "the machine ran on between the switches";
    EXPECT_FALSE(features->probeDebugHold());
}

TEST_F(HapticFeed_Test, Haptics_NotJournaled)
{
    // The same program on two machines, one with every probe and three taps: the same machine state each frame,
    // and the same input journal
    Emulator* plain = Start();
    ASSERT_NE(plain, nullptr);
    auto feed = std::make_shared<RecordingFeed>();
    feed->probes = AllProbes();
    feed->streams = {HapticStream::Beeper, HapticStream::Mix, HapticStream::Ay0};
    _manager->SetHapticFeed(feed);
    for (Emulator* emulator : {_emulator, plain})
    {
        emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        ASSERT_TRUE(emulator->GetContext()->pTimeTravelController->StartRecording());
    }
    for (int frame = 0; frame < 10; frame++)
    {
        _emulator->RunNFrames(1, false);
        plain->RunNFrames(1, false);
        ASSERT_EQ(HashNow(_emulator), HashNow(plain)) << "frame " << frame;
    }
    EXPECT_GE(feed->frames.size(), 9u);
    EXPECT_EQ(_context->pTimeTravelController->GetInputJournal().Events().size(),
              plain->GetContext()->pTimeTravelController->GetInputJournal().Events().size());
    EXPECT_EQ(_context->pTimeTravelController->GetCheckpointCount(),
              plain->GetContext()->pTimeTravelController->GetCheckpointCount());
    _context->pTimeTravelController->StopRecording();
    plain->GetContext()->pTimeTravelController->StopRecording();
    EmulatorTestHelper::CleanupEmulator(plain);
}

TEST_F(HapticFeed_Test, Haptics_GateOnSeek)
{
    std::vector<RumbleLevels> written;
    std::mutex lock;
    HapticMixer mixer(
        [&](const HostDeviceId&, const RumbleLevels& levels) {
            std::lock_guard<std::mutex> guard(lock);
            written.push_back(levels);
            return true;
        },
        false);
    JsonValue section;
    std::string error;
    ASSERT_TRUE(ParseJson(R"({"sources": {"lives": {"probe": "value", "address": "0x9000", "scale": 51}},
                              "rules": [{"on": "lives", "play": "engine"}]})",
                          section, error))
        << error;
    HapticProfile profile;
    ASSERT_TRUE(ParseHapticProfile(section, profile, error)) << error;
    auto feed = std::make_shared<ProfileHaptics>(profile, "game/test", mixer,
                                                 [] { return std::vector<HostDeviceId>{"pad"}; });
    _manager->SetHapticFeed(feed);

    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_TRUE(ttd->StartRecording());
    _emulator->RunNFrames(6, false);
    ASSERT_GT(mixer.Written("pad").low, 0.0f) << "the value probe holds the engine on";
    EXPECT_FALSE(_manager->HapticHeld());

    ASSERT_TRUE(ttd->SeekTo({2, 0}));
    EXPECT_TRUE(_manager->HapticHeld()) << "in the recorded history";
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{}) << "the seek stopped the motors in the same call";
    {
        std::lock_guard<std::mutex> guard(lock);
        ASSERT_FALSE(written.empty());
        EXPECT_EQ(written.back(), RumbleLevels{}) << "the fake driver got the zero";
    }
    const size_t writes = written.size();
    _emulator->RunNFrames(1, false);
    EXPECT_EQ(written.size(), writes) << "replayed history plays nothing";
    EXPECT_EQ(mixer.Written("pad"), RumbleLevels{});
    _manager->SetHapticFeed(nullptr);
    ttd->StopRecording();
}

TEST(HapticFeedNext_Test, TheNextDacIsAStreamOfItsOwn)
{
    // The Next's four DAC channels alone, before the part gains: the DAC tap of NextAudio
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    ports->Board().Write(0x08, 0x02 | 0x08);  // turbosound and the DAC on
    auto feed = std::make_shared<RecordingFeed>();
    feed->streams = {HapticStream::NextDac, HapticStream::Covox};
    context->pControllerManager->SetHapticFeed(feed);
    emulator->RunNFrames(2, true);
    ASSERT_FALSE(feed->frames.empty());
    EXPECT_EQ(feed->frames.back().streams[0].samples[10], 0) << "the DAC at rest (#80) is silence";
    ports->Audio().WriteDac(0, 0xFF);
    ports->Audio().WriteDac(2, 0xFF);
    ports->Audio().SetAudioPartGain(NextAudio::kChips, 0.0f);  // the mixer's DAC row at zero: the tap is before it
    emulator->RunNFrames(3, true);
    const HapticFrame& frame = feed->frames.back();
    ASSERT_EQ(frame.streamCount, 2u);
    ASSERT_GE(frame.streams[0].count, 150u);
    EXPECT_EQ(frame.streams[0].samples[10], (127 + 127) * 8) << "A and C at #FF, the mono sum";
    EXPECT_EQ(frame.streams[1].samples[10], frame.streams[0].samples[10]) << "covox on the Next is its DAC";
    context->pControllerManager->SetHapticFeed(nullptr);
    emulator->RunNFrames(1, true);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(HapticFeedNext_Test, EachAyIsAStreamOfItsOwn)
{
    // ay:<n> on the Next: its chips mix inside NextAudio, which renders each tapped chip alone (its DC out)
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    auto* ports = dynamic_cast<PortDecoder_Next*>(context->pPortDecoder);
    ASSERT_NE(ports, nullptr);
    ports->Board().Write(0x08, 0x02 | 0x08);  // turbosound and the DAC on
    SoundChip_AY8910* chip = ports->Audio().Chip(0);
    ASSERT_NE(chip, nullptr);
    chip->writeRegister(0, 0x40);  // tone A: period #040, about 1.7 kHz
    chip->writeRegister(1, 0x00);
    chip->writeRegister(7, 0x3E);  // tone A only
    chip->writeRegister(8, 0x0F);  // full volume
    auto feed = std::make_shared<RecordingFeed>();
    feed->streams = {HapticStream::Ay0, HapticStream::Ay1};
    context->pControllerManager->SetHapticFeed(feed);
    emulator->RunNFrames(4, true);
    ASSERT_FALSE(feed->frames.empty());
    const HapticFrame& frame = feed->frames.back();
    ASSERT_EQ(frame.streamCount, 2u);
    ASSERT_GE(frame.streams[0].count, 150u);
    const auto swing = [&frame](size_t stream) {
        const auto& samples = frame.streams[stream].samples;
        const auto [low, high] = std::minmax_element(samples.begin(), samples.begin() + frame.streams[stream].count);
        return static_cast<int>(*high) - static_cast<int>(*low);
    };
    EXPECT_GT(swing(0), 2000) << "chip 0 plays its tone";
    EXPECT_LT(swing(1), 50) << "chip 1 is silent";
    context->pControllerManager->SetHapticFeed(nullptr);
    emulator->RunNFrames(1, true);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(HapticFeedTaps_Test, SoundriveChannelsAlone)
{
    // soundrive:<n>: one Soundrive channel before its gain, (value - #80) x 128, on a Pentagon with the card
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    Covox* covox = context->pSoundManager->getCovox();
    ASSERT_NE(covox, nullptr) << "the Pentagon's default config fits the Soundrive";
    auto feed = std::make_shared<RecordingFeed>();
    feed->streams = {HapticStream::Soundrive0, HapticStream::Soundrive3, HapticStream::Saa, HapticStream::Moonsound};
    context->pControllerManager->SetHapticFeed(feed);
    emulator->RunNFrames(1, true);
    covox->portDeviceOutMethod(Covox::PORT_LEFT_A, 0xFF);
    emulator->RunNFrames(2, true);
    ASSERT_FALSE(feed->frames.empty());
    const HapticFrame& frame = feed->frames.back();
    ASSERT_EQ(frame.streamCount, 4u);
    ASSERT_GE(frame.streams[0].count, 150u);
    EXPECT_EQ(frame.streams[0].samples[0], 127 * 128) << "left A at #FF";
    EXPECT_EQ(frame.streams[0].samples[100], 127 * 128);
    EXPECT_EQ(frame.streams[1].count, frame.streams[0].count);
    EXPECT_EQ(frame.streams[1].samples[100], 0) << "right B at rest";
    EXPECT_EQ(frame.streams[2].count, 0u) << "no MultiSound card: no SAA";
    EXPECT_EQ(frame.streams[3].count, 0u) << "no Moonsound";
    context->pControllerManager->SetHapticFeed(nullptr);
    emulator->RunNFrames(1, true);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
