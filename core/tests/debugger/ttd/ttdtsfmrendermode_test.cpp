/// @file ttdtsfmrendermode_test.cpp
/// @brief The TurboSound FM's TTD state holds its render loop's position (sample and decimator phases, render
/// cursor, frame progress), which advances differently in turbo, with the sound off and in LQ. TTD
/// pins the device's render mode while the machine is bound to a timeline (ITurboSoundDevice::setTtdRenderPinned):
/// a recording changes it only at a frame start, where the checkpoint records it, and a replay keeps the loaded
/// checkpoint's. Whatever the host's sound settings at replay time, a replay from a checkpoint ends at the next
/// checkpoint's state, across a mode change in the recording too. The recording starts right after a reset, before
/// the chips adopted the CPU's clock, so the baseline interval is checked as well.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

namespace
{
/// The host's sound setting a replay runs with
enum class ReplayMode
{
    AsRecorded,
    SoundOff,
    LowQuality,
    Turbo,
    TurboWithAudio,
};

/// The host's sound settings during a recording: from its start, and from frame kSwitchFrame on
enum class RecordMode
{
    HQ,
    LQ,
    SoundOff,
    HQThenLQ,
    SoundOnThenOff,
    SoundOffThenOn,
};

struct RenderModeCase
{
    RecordMode record;
    ReplayMode replay;
};

std::string CaseName(const ::testing::TestParamInfo<RenderModeCase>& info)
{
    static const char* const kRecords[] = {"RecordedHQ", "RecordedLQ", "RecordedSoundOff",
                                           "RecordedHQThenLQ", "RecordedSoundOnThenOff", "RecordedSoundOffThenOn"};
    static const char* const kModes[] = {"AsRecorded", "SoundOff", "LowQuality", "Turbo", "TurboWithAudio"};
    return std::string(kRecords[int(info.param.record)]) + kModes[int(info.param.replay)];
}

/// The machine: CPU, chipset, every device (decoded) and every RAM page
struct MachineState
{
    ttd::TTDCpuState cpu;
    ttd::TTDChipsetState chipset;
    std::unordered_map<uint8_t, std::vector<uint8_t>> devices;
    std::vector<std::vector<uint8_t>> ram;
};
}  // namespace

class TTDTsfmRenderMode_Test : public ::testing::TestWithParam<RenderModeCase>
{
protected:
    static constexpr uint64_t kFrames = 5;
    static constexpr uint64_t kSwitchFrame = 2;

    // The shipped Pentagon config fits the TSFM in the AY socket (the runner leaves it out by default)
    SoundCardScope _soundCards{TestSound::TurboSound};
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void SetSound(const char* feature, bool on)
    {
        _context->pFeatureManager->setFeature(feature, on);
        _context->pSoundManager->UpdateFeatureCache();
    }

    /// A fresh Pentagon (TSFM in the AY socket) records kFrames frames of a program writing the SSG every pass,
    /// from right after its reset, with the host's settings changed between frames as @p mode says
    void Record(RecordMode mode)
    {
        const bool hq = mode != RecordMode::LQ;
        const bool soundOn = mode != RecordMode::SoundOff && mode != RecordMode::SoundOffThenOn;
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kDebugMode, true);
        _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        SetSound(Features::kSoundGeneration, soundOn);
        SetSound(Features::kSoundHQ, hq);
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);

        const std::vector<uint8_t> program = {
            0xF3,              // 8000 di
            0x16, 0x00,        // 8001 ld d,0
            0x01, 0xFD, 0xFF,  // 8003 loop: ld bc,#FFFD
            0x3E, 0x07,        // 8006 ld a,7          mixer
            0xED, 0x79,        // 8008 out (c),a
            0x06, 0xBF,        // 800A ld b,#BF
            0x3E, 0x3E,        // 800C ld a,#3E        tone A on
            0xED, 0x79,        // 800E out (c),a
            0x06, 0xFF,        // 8010 ld b,#FF
            0x3E, 0x08,        // 8012 ld a,8          volume A
            0xED, 0x79,        // 8014 out (c),a
            0x06, 0xBF,        // 8016 ld b,#BF
            0x3E, 0x0F,        // 8018 ld a,15
            0xED, 0x79,        // 801A out (c),a
            0x06, 0xFF,        // 801C ld b,#FF
            0xAF,              // 801E xor a           tone A period, low
            0xED, 0x79,        // 801F out (c),a
            0x06, 0xBF,        // 8021 ld b,#BF
            0x14,              // 8023 inc d           a new period every pass
            0x7A,              // 8024 ld a,d
            0xED, 0x79,        // 8025 out (c),a
            0x06, 0x00,        // 8027 ld b,0
            0x10, 0xFE,        // 8029 djnz $          about 3,300 T
            0x18, 0xD6,        // 802B jr loop
        };
        for (size_t i = 0; i < program.size(); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        _context->pCore->GetZ80()->pc = 0x8000;
        _context->pCore->GetZ80()->sp = 0xBF00;

        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(kSwitchFrame, /*skipBreakpoints=*/true);
        switch (mode)
        {
            case RecordMode::HQThenLQ:
                SetSound(Features::kSoundHQ, false);
                break;
            case RecordMode::SoundOnThenOff:
                SetSound(Features::kSoundGeneration, false);
                break;
            case RecordMode::SoundOffThenOn:
                SetSound(Features::kSoundGeneration, true);
                break;
            default:
                break;
        }
        _emulator->RunNFrames(kFrames - kSwitchFrame, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ASSERT_GE(_ttd->GetCheckpointCount(), kFrames);
    }

    void ApplyReplayMode(ReplayMode mode)
    {
        switch (mode)
        {
            case ReplayMode::AsRecorded:
                break;
            case ReplayMode::SoundOff:
                SetSound(Features::kSoundGeneration, false);
                break;
            case ReplayMode::LowQuality:
                SetSound(Features::kSoundHQ, false);
                break;
            case ReplayMode::Turbo:
                _emulator->EnableTurboMode(false);
                break;
            case ReplayMode::TurboWithAudio:
                _emulator->EnableTurboMode(true);
                break;
        }
    }

    MachineState State() const
    {
        MachineState m;
        const Z80* z80 = _context->pCore->GetZ80();
        m.cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(z80));
        m.chipset = ttd::CaptureChipsetState(_context->emulatorState, static_cast<uint32_t>(z80->t));
        std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
        _ttd->GetPeripheralRegistry().CaptureAll(blobs);
        for (const auto& [id, blob] : blobs)
            m.devices[id] = ttd::TTDPeripheralRegistry::DecodeBlob(id, blob);
        for (uint16_t page = 0; page < _ttd->GetSessionInfo().modelRamPages; ++page)
            if (const uint8_t* bytes = _context->pMemory->RAMPageAddress(page))
                m.ram.emplace_back(bytes, bytes + 0x4000);
        return m;
    }

    /// From frame @p frame's checkpoint the machine runs to the frame's end, where it equals the next checkpoint
    void ExpectReplayReachesNextCheckpoint(uint64_t frame)
    {
        ASSERT_TRUE(_ttd->SeekTo({frame, 0}));
        const uint32_t frameT = _context->GetFrameTStates();
        const uint32_t startT = static_cast<uint32_t>(_context->pCore->GetZ80()->t);
        ASSERT_LT(startT, frameT);
        _emulator->RunTStates(frameT - startT, /*skipBreakpoints=*/true);
        const MachineState replayed = State();

        ASSERT_TRUE(_ttd->SeekTo({frame + 1, 0}));
        const MachineState recorded = State();

        const std::string where = "replay of frame " + std::to_string(frame);
        EXPECT_EQ(std::memcmp(&replayed.cpu, &recorded.cpu, sizeof(replayed.cpu)), 0) << where << ": CPU";
        EXPECT_EQ(std::memcmp(&replayed.chipset, &recorded.chipset, sizeof(replayed.chipset)), 0)
            << where << ": chipset";
        ASSERT_EQ(replayed.devices.size(), recorded.devices.size()) << where;
        for (const auto& [id, expected] : recorded.devices)
        {
            const std::vector<uint8_t>& actual = replayed.devices.at(id);
            size_t first = 0;
            while (first < expected.size() && first < actual.size() && actual[first] == expected[first])
                ++first;
            EXPECT_TRUE(actual == expected) << where << ": device " << int(id) << " differs from byte " << first;
        }
        EXPECT_TRUE(replayed.ram == recorded.ram) << where << ": RAM";
    }
};

TEST_P(TTDTsfmRenderMode_Test, ReplayReachesTheRecordedState)
{
    const RenderModeCase c = GetParam();
    ASSERT_NO_FATAL_FAILURE(Record(c.record));
    ASSERT_EQ(State().devices.count(uint8_t(ttd::PeripheralId::TSFM)), 1u) << "the Pentagon's AY socket holds the TSFM";
    ASSERT_NO_FATAL_FAILURE(ApplyReplayMode(c.replay));

    // The baseline interval (recorded right after the reset) and every one after it, the mode change included
    for (uint64_t frame = 0; frame + 1 < kFrames; ++frame)
    {
        ASSERT_NO_FATAL_FAILURE(ExpectReplayReachesNextCheckpoint(frame));
        if (HasFailure())
            return;   // the first divergence is the useful one
    }
}

INSTANTIATE_TEST_SUITE_P(Modes, TTDTsfmRenderMode_Test,
                         ::testing::Values(RenderModeCase{RecordMode::HQ, ReplayMode::AsRecorded},
                                           RenderModeCase{RecordMode::HQ, ReplayMode::SoundOff},
                                           RenderModeCase{RecordMode::HQ, ReplayMode::LowQuality},
                                           RenderModeCase{RecordMode::HQ, ReplayMode::Turbo},
                                           RenderModeCase{RecordMode::HQ, ReplayMode::TurboWithAudio},
                                           RenderModeCase{RecordMode::LQ, ReplayMode::AsRecorded},
                                           RenderModeCase{RecordMode::LQ, ReplayMode::SoundOff},
                                           RenderModeCase{RecordMode::LQ, ReplayMode::Turbo},
                                           RenderModeCase{RecordMode::SoundOff, ReplayMode::AsRecorded},
                                           RenderModeCase{RecordMode::SoundOff, ReplayMode::TurboWithAudio},
                                           RenderModeCase{RecordMode::HQThenLQ, ReplayMode::AsRecorded},
                                           RenderModeCase{RecordMode::HQThenLQ, ReplayMode::Turbo},
                                           RenderModeCase{RecordMode::SoundOnThenOff, ReplayMode::AsRecorded},
                                           RenderModeCase{RecordMode::SoundOnThenOff, ReplayMode::LowQuality},
                                           RenderModeCase{RecordMode::SoundOffThenOn, ReplayMode::AsRecorded},
                                           RenderModeCase{RecordMode::SoundOffThenOn, ReplayMode::SoundOff}),
                         CaseName);
