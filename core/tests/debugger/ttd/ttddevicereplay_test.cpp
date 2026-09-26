#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "_helpers/soundcardscope.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "emulator/memory/memory.h"

/// Every device restores and replays exactly while it is actually working.
///
/// The corpus fixtures record demos that leave most sound devices idle, so a
/// device whose frame-relative state is lost on restore (a time axis, a frame
/// base, a stale-channel countdown, a sample-phase accumulator) replays
/// "correctly" there by accident. Here a program drives every sound device on
/// the machine from the CPU the whole time - beeper, AY / TurboSound, Covox,
/// MoonSound FM, General Sound mailbox - and the recording is re-executed
/// from several restore points: the session start (a baseline taken
/// mid-frame), a per-frame checkpoint, and a mid-frame seek. Each run must
/// arrive at the recorded end with the same CPU, RAM and device state, byte
/// for byte, as the recording itself.
namespace
{
using DeviceBlobs = std::map<uint8_t, std::vector<uint8_t>>;

struct ReplayPoint
{
    uint64_t frame = 0;
    uint32_t tInFrame = 0;
    ttd::TTDCpuState cpu;
    uint64_t ramHash = 0;
    DeviceBlobs devices;
};

/// Drives every sound device, interrupts off, forever (loop at #8003). Covox
/// is written in bursts only (32 of every 256 iterations, ~2000 T each), so its
/// channels go idle for several frames at a time and the stale-channel decay
/// runs too
constexpr uint16_t kProgramAddress = 0x8000;
const uint8_t kSoundDriver[] = {
    0xF3,              // 8000 DI
    0x1E, 0x00,        // 8001 LD E,0
    0x7B,              // 8003 loop: LD A,E
    0xE6, 0x17,        // 8004 AND #17            beeper (bit 4) + border
    0xD3, 0xFE,        // 8006 OUT (#FE),A
    0x01, 0xFD, 0xFF,  // 8008 LD BC,#FFFD        AY: register E&7 <- E
    0x7B,              // 800B LD A,E
    0xE6, 0x07,        // 800C AND 7
    0xED, 0x79,        // 800E OUT (C),A
    0x06, 0xBF,        // 8010 LD B,#BF
    0xED, 0x59,        // 8012 OUT (C),E
    0x7B,              // 8014 LD A,E             Covox bursts: only while E < #20
    0xE6, 0xE0,        // 8015 AND #E0
    0x20, 0x03,        // 8017 JR NZ,+3
    0x7B,              // 8019 LD A,E
    0xD3, 0xFB,        // 801A OUT (#FB),A
    0x3E, 0xA0,        // 801C LD A,#A0           MoonSound FM: fnum <- E
    0xD3, 0xC4,        // 801E OUT (#C4),A
    0x7B,              // 8020 LD A,E
    0xD3, 0xC5,        // 8021 OUT (#C5),A
    0x3E, 0xB0,        // 8023 LD A,#B0           key on, block 4
    0xD3, 0xC4,        // 8025 OUT (#C4),A
    0x3E, 0x32,        // 8027 LD A,#32
    0xD3, 0xC5,        // 8029 OUT (#C5),A
    0x7B,              // 802B LD A,E
    0xD3, 0xB3,        // 802C OUT (#B3),A        General Sound data
    0xDB, 0xBB,        // 802E IN A,(#BB)         General Sound status
    0x1C,              // 8030 INC E
    0x06, 0x80,        // 8031 LD B,#80
    0x10, 0xFE,        // 8033 DJNZ $
    0x18, 0xCC,        // 8035 JR loop
};
}  // namespace

class TTD_DeviceReplay_Test : public ::testing::TestWithParam<TurboSoundKind>
{
protected:
    // Keep the General Sound / MoonSound cards the configs fit (every sound device on the machine must be driven, cards included):
    // declared first, so it is active before any machine is created
    SoundCardScope _soundCards;

protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        _emulator->SetCustomConfigPath(EmulatorTestHelper::StageTurboSoundKindConfig(GetParam()));
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    void InstallSoundDriver()
    {
        Z80* z80 = _context->pCore->GetZ80();
        for (size_t i = 0; i < sizeof(kSoundDriver); i++)
            z80->DirectWrite(static_cast<uint16_t>(kProgramAddress + i), kSoundDriver[i]);
        z80->pc = kProgramAddress;
    }

    ReplayPoint Observe() const
    {
        const Z80* z80 = _context->pCore->GetZ80();
        ReplayPoint point;
        point.frame = _context->emulatorState.frame_counter;
        point.tInFrame = z80->t;
        point.cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
        const size_t ramBytes = static_cast<size_t>(_context->config.ramsize) * 1024u;
        point.ramHash = ttd::HashBytes(_context->pMemory->RAMBase(), ramBytes);

        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        _ttd->GetPeripheralRegistry().CaptureAll(blobs);
        for (const auto& [id, blob] : blobs)
            point.devices[id] = ttd::TTDPeripheralRegistry::DecodeBlob(id, blob);
        return point;
    }

    void RunTo(const ReplayPoint& target)
    {
        const EmulatorState* state = &_context->emulatorState;
        const uint64_t frame = target.frame;
        const uint32_t t = target.tInFrame;
        _emulator->RunUntilCondition([state, frame, t](const Z80State& z80)
                                     { return state->frame_counter > frame || (state->frame_counter == frame && z80.t >= t); });
    }

    static void ExpectSame(const ReplayPoint& actual, const ReplayPoint& expected, const std::string& from)
    {
        EXPECT_EQ(actual.frame, expected.frame) << from;
        EXPECT_EQ(actual.tInFrame, expected.tInFrame) << from;
        EXPECT_EQ(std::memcmp(&actual.cpu, &expected.cpu, sizeof(actual.cpu)), 0) << from << ": CPU state";
        EXPECT_EQ(actual.ramHash, expected.ramHash) << from << ": RAM";
        EXPECT_EQ(actual.devices.size(), expected.devices.size()) << from << ": device set";
        for (const auto& [id, blob] : expected.devices)
        {
            const auto it = actual.devices.find(id);
            ASSERT_NE(it, actual.devices.end()) << from << ": device " << int(id) << " missing";
            ASSERT_EQ(it->second.size(), blob.size()) << from << ": device " << int(id) << " blob size";
            size_t first = 0;
            while (first < blob.size() && it->second[first] == blob[first])
                first++;
            EXPECT_EQ(first, blob.size()) << from << ": device " << int(id) << " differs from byte " << first;
        }
    }
};

TEST_P(TTD_DeviceReplay_Test, BusyDevicesReplayExactlyFromEveryKindOfRestorePoint)
{
    _emulator->RunNFrames(5);
    _emulator->RunNCPUCycles(2345);  // the baseline lands mid-frame
    InstallSoundDriver();
    _emulator->RunNFrames(3);

    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _ttd->GetCheckpoint(0)->time.frame;
    _emulator->RunNFrames(40);
    _emulator->RunNCPUCycles(777);
    const ReplayPoint recorded = Observe();
    _ttd->StopRecording();
    ASSERT_GE(recorded.devices.size(), 2u) << "the machine must carry several sound devices";
    ASSERT_TRUE(recorded.devices.count(static_cast<uint8_t>(ttd::PeripheralId::GeneralSound)))
        << "the config fits a General Sound card - it must be part of the replay check";
#ifdef UNREALNG_HAVE_OPL4
    ASSERT_TRUE(recorded.devices.count(static_cast<uint8_t>(ttd::PeripheralId::MoonSound)))
        << "the config fits a MoonSound card - it must be part of the replay check";
#endif

    struct Start
    {
        std::string name;
        ttd::TTDTimePoint at;
    };
    const Start starts[] = {
        {"session start (mid-frame baseline)", {startFrame, 0}},
        {"per-frame checkpoint", {startFrame + 17, 0}},
        {"mid-frame seek", {startFrame + 25, 23456}},
    };
    for (const Start& start : starts)
    {
        SCOPED_TRACE(start.name);
        ASSERT_TRUE(_ttd->SeekTo(start.at));
        RunTo(recorded);
        ExpectSame(Observe(), recorded, start.name);
    }
}

INSTANTIATE_TEST_SUITE_P(TurboSoundSlot, TTD_DeviceReplay_Test,
                         ::testing::Values(TurboSoundKind::FM, TurboSoundKind::AY),
                         [](const ::testing::TestParamInfo<TurboSoundKind>& info)
                         { return info.param == TurboSoundKind::FM ? std::string("TSFM") : std::string("LegacyAY"); });

/// Covox re-centers a channel that stayed idle for STALE_CHANNEL_FRAMES whole
/// frames (the decay writes the DAC latch, which is TTD state). The idle
/// countdown must therefore restore with the latch: resumed from a checkpoint
/// taken before the decay, the channel decays on the same frame as recorded -
/// not early (the live countdown carried over from later in the timeline) and
/// not late (a countdown restarted from zero)
TEST_P(TTD_DeviceReplay_Test, CovoxIdleDecayReplaysOnTheRecordedFrame)
{
    if (GetParam() != TurboSoundKind::FM)
        GTEST_SKIP() << "slot-independent: one instantiation is enough";

    // Write one value to the Covox, then stand still (JR $)
    static const uint8_t writeOnce[] = {0xF3, 0x3E, 0x5A, 0xD3, 0xFB, 0x18, 0xFE};
    Z80* z80 = _context->pCore->GetZ80();
    for (size_t i = 0; i < sizeof(writeOnce); i++)
        z80->DirectWrite(static_cast<uint16_t>(kProgramAddress + i), writeOnce[i]);
    z80->pc = kProgramAddress;

    _emulator->RunNFrames(1);  // the write lands; the next frames are idle
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(8);  // decay happens in here
    _ttd->StopRecording();

    constexpr uint8_t kCovox = static_cast<uint8_t>(ttd::PeripheralId::Covox);
    const auto covoxAt = [&](size_t idx)
    {
        const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(idx);
        return ttd::TTDPeripheralRegistry::DecodeBlob(kCovox, cp->peripheralBlobs.at(kCovox));
    };

    // Find the recorded decay: the first checkpoint whose channel-3 latch is centered
    size_t decayIdx = 0;
    for (size_t i = 1; i < _ttd->GetCheckpointCount(); i++)
    {
        if (covoxAt(i)[3] == 0x80 && covoxAt(i - 1)[3] == 0x5A)
        {
            decayIdx = i;
            break;
        }
    }
    ASSERT_GE(decayIdx, 2u) << "the recording must contain the idle decay with two checkpoints before it";

    // Resume two checkpoints before the decay (the decay runs in a frame
    // start, before that frame's capture); the live countdown is far past the
    // threshold (8 idle frames), so a countdown not restored decays a frame early
    const ttd::TTDCheckpoint* before = _ttd->GetCheckpoint(decayIdx - 2);
    ASSERT_TRUE(_ttd->SeekTo({before->time.frame, 0}));
    for (size_t i = decayIdx - 2; i + 1 < _ttd->GetCheckpointCount() && i <= decayIdx + 1; i++)
    {
        const ttd::TTDCheckpoint* next = _ttd->GetCheckpoint(i + 1);
        ReplayPoint target;
        target.frame = next->time.frame;
        target.tInFrame = ttd::GetChipsetCpuTInFrame(next->chipset);
        RunTo(target);
        EXPECT_EQ(Observe().devices.at(kCovox), covoxAt(i + 1)) << "Covox at checkpoint " << (i + 1);
    }
}

/// A playing tape replays exactly: the EAR level the CPU reads, the edge
/// timing and the loader watchdogs (read gap, EAR-poll resume) are tape state,
/// not derivable from the head position. A program polls EAR (port #FE bit 6)
/// in a tight loop with an iteration counter and logs the counter at every
/// edge (#9000..): any difference in level or edge instant after a restore
/// changes the log
TEST_P(TTD_DeviceReplay_Test, PlayingTapeReplaysExactly)
{
    if (GetParam() != TurboSoundKind::FM)
        GTEST_SKIP() << "slot-independent: one instantiation is enough";

    static const uint8_t earPoller[] = {
        0xF3,                    // 8000 DI
        0x21, 0x00, 0x00,        // 8001 LD HL,0
        0xDD, 0x21, 0x00, 0x90,  // 8004 LD IX,#9000
        0x0E, 0xFF,              // 8008 LD C,#FF
        0x23,                    // 800A loop: INC HL
        0x3E, 0xFF,              // 800B LD A,#FF        no keyboard row selected
        0xDB, 0xFE,              // 800D IN A,(#FE)
        0xE6, 0x40,              // 800F AND #40         EAR
        0xB9,                    // 8011 CP C
        0x28, 0xF6,              // 8012 JR Z,loop
        0x4F,                    // 8014 LD C,A
        0xDD, 0x75, 0x00,        // 8015 LD (IX+0),L
        0xDD, 0x74, 0x01,        // 8018 LD (IX+1),H
        0xDD, 0x23,              // 801B INC IX
        0xDD, 0x23,              // 801D INC IX
        0x18, 0xE9,              // 801F JR loop
    };
    // Automatic turbo loading off: it toggles the host turbo mode during the
    // load, and turbo suppresses sound synthesis - a host decision TTD does
    // not journal yet (v2 input ownership: tape / disk / turbo), so the AY /
    // TSFM synthesis state would follow whichever turbo stretch the replay
    // happened to get. This test is about the tape's own state
    _emulator->GetFeatureManager()->setFeature(Features::kTurboTape, false);

    // Inserted, not started: the poller's sustained EAR polling starts
    // playback by itself (Tape's EAR-poll resume) - watchdog state in action
    ASSERT_TRUE(_emulator->LoadTape((TestPathHelper::FindProjectRoot() / "testdata/memory/UMT23X.tap").string()));

    Z80* z80 = _context->pCore->GetZ80();
    for (size_t i = 0; i < sizeof(earPoller); i++)
        z80->DirectWrite(static_cast<uint16_t>(kProgramAddress + i), earPoller[i]);
    for (uint16_t a = 0x9000; a < 0xC000; a++)
        z80->DirectWrite(a, 0);
    z80->pc = kProgramAddress;

    _emulator->RunNFrames(3);
    _emulator->RunNCPUCycles(4321);  // the baseline lands mid-frame
    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _ttd->GetCheckpoint(0)->time.frame;
    _emulator->RunNFrames(30);
    _emulator->RunNCPUCycles(999);
    const ReplayPoint recorded = Observe();
    _ttd->StopRecording();
    ASSERT_NE(_context->pMemory->DirectReadFromZ80Memory(0x9010), 0) << "the poller saw no tape edges - vacuous";

    const ttd::TTDTimePoint starts[] = {{startFrame, 0}, {startFrame + 11, 0}, {startFrame + 19, 30000}};
    for (const ttd::TTDTimePoint& start : starts)
    {
        const std::string name = "from frame " + std::to_string(start.frame) + " t " + std::to_string(start.tInFrame);
        SCOPED_TRACE(name);
        ASSERT_TRUE(_ttd->SeekTo(start));
        RunTo(recorded);
        ExpectSame(Observe(), recorded, name);
    }
}
