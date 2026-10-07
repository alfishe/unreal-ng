#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/audiocapture/audiocaptureanalyzer.h"
#include "debugger/debugmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/memory/memory.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/soundmanager.h"

/// The AY clock at run time (SoundChip_TurboSound::SetPsgClock, Profi hi-res phase H2b in
/// docs/inprogress/2026-10-01-profi-v3-v5/design-hires.md): the generators follow the clock from the T-state of
/// the switch, the output carries on without a click, and the clock state survives a TTD save / load.
///
/// The device is driven directly, the way multirate_test.cpp does it: a frame = handleFrameStart at T 0, then one
/// handleStep at the frame end.
class SoundChipTurboSound_Test : public ::testing::Test
{
protected:
    static constexpr uint32_t kFrame = 71680;  // Pentagon
    static constexpr uint32_t kHiresClock = 1'500'000;

    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::AY);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _context->config.frame = kFrame;
        _z80->t = 0;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _z80->t = 0;
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    static SoundChip_TurboSound* Device(SoundManager& sound)
    {
        return dynamic_cast<SoundChip_TurboSound*>(sound.getTurboSound());
    }

    static void Poke(SoundChip_TurboSound* device, uint8_t reg, uint8_t value)
    {
        device->portDeviceOutMethod(0xFFFD, reg);
        device->portDeviceOutMethod(0xBFFD, value);
    }

    /// Channel A: tone of the given period at full volume, or (period 0) the tone off - a constant level
    static void ProgramChannelA(SoundChip_TurboSound* device, uint16_t period)
    {
        Poke(device, 0, static_cast<uint8_t>(period & 0xFF));
        Poke(device, 1, static_cast<uint8_t>(period >> 8));
        Poke(device, 7, period ? 0b00111110 : 0b00111111);
        Poke(device, 8, 15);
    }

    void StartFrame(SoundChip_TurboSound* device)
    {
        _z80->t = 0;
        device->handleFrameStart();
    }

    void StepTo(SoundChip_TurboSound* device, uint32_t t)
    {
        _z80->t = t;
        device->handleStep();
    }

    /// Left channel of chip 0, frame after frame
    std::vector<int16_t> RenderFrames(SoundChip_TurboSound* device, int frames)
    {
        std::vector<int16_t> stream;
        for (int f = 0; f < frames; f++)
        {
            StartFrame(device);
            StepTo(device, kFrame);
            AppendFrame(device, stream);
        }
        return stream;
    }

    static void AppendFrame(SoundChip_TurboSound* device, std::vector<int16_t>& stream)
    {
        const size_t samples = device->getRenderedSamplesThisFrame();
        const int16_t* buffer = device->getChipBuffer(0);
        for (size_t i = 0; i < samples; i++)
            stream.push_back(buffer[i * 2]);
    }

    /// Midline crossings with hysteresis (multirate_test.cpp)
    static uint64_t Crossings(const std::vector<int16_t>& stream)
    {
        const auto [lo, hi] = std::minmax_element(stream.begin(), stream.end());
        const int32_t mid = (int32_t(*lo) + int32_t(*hi)) / 2;
        const int32_t hysteresis = (int32_t(*hi) - int32_t(*lo)) / 8;
        uint64_t crossings = 0;
        int state = 0;
        for (int16_t s : stream)
        {
            if (s > mid + hysteresis && state <= 0)
            {
                crossings += state < 0 ? 1 : 0;
                state = 1;
            }
            else if (s < mid - hysteresis && state >= 0)
            {
                crossings += state > 0 ? 1 : 0;
                state = -1;
            }
        }
        return crossings;
    }

    static std::vector<uint8_t> Save(const SoundChip_TurboSound* device)
    {
        std::vector<uint8_t> blob(device->TTDStateSize());
        device->TTDSaveState(blob.data());
        return blob;
    }
};

/// 1.75 MHz unless the machine says otherwise; requests are rounded to 100 Hz and checked against the range a
/// clock marker carries
TEST_F(SoundChipTurboSound_Test, DefaultClockAndRequestValidation)
{
    SoundManager sound(_context);
    SoundChip_TurboSound* device = Device(sound);
    ASSERT_NE(device, nullptr);

    EXPECT_EQ(device->GetPsgClock(), PSG_CLOCK_RATE);
    EXPECT_EQ(sound.GetPsgClock(), PSG_CLOCK_RATE);
    EXPECT_TRUE(device->SetPsgClock(PSG_CLOCK_RATE)) << "the current clock again is accepted";

    EXPECT_FALSE(device->SetPsgClock(0));
    EXPECT_FALSE(device->SetPsgClock(SoundChip_TurboSound::kMaxPsgClock + 100));
    EXPECT_EQ(device->GetRequestedPsgClock(), PSG_CLOCK_RATE) << "a refused request changes nothing";

    EXPECT_TRUE(sound.SetPsgClock(1'500'049));
    EXPECT_EQ(device->GetRequestedPsgClock(), kHiresClock) << "rounded to 100 Hz";
    EXPECT_EQ(device->GetPsgClock(), PSG_CLOCK_RATE) << "queued on the render timeline, not reached yet";
}

/// A tone renders at 6/7 of its pitch at 1.5 MHz, in both output paths (HQ FIR decimation, LQ boxcar)
TEST_F(SoundChipTurboSound_Test, TonePitchFollowsTheClock)
{
    for (const bool hq : {true, false})
    {
        SCOPED_TRACE(hq ? "HQ" : "LQ");
        uint64_t crossings[2] = {};
        for (int i = 0; i < 2; i++)
        {
            SoundManager sound(_context);
            SoundChip_TurboSound* device = Device(sound);
            ASSERT_NE(device, nullptr);
            device->setHQEnabled(hq);
            _z80->t = 0;
            if (i == 1)
                ASSERT_TRUE(device->SetPsgClock(kHiresClock));
            ProgramChannelA(device, 60);  // 1823 Hz at 1.75 MHz, 1563 Hz at 1.5 MHz
            RenderFrames(device, 4);  // the output DC blocker settles from the level step at the start
            const std::vector<int16_t> stream = RenderFrames(device, 12);
            crossings[i] = Crossings(stream);
            EXPECT_EQ(device->GetPsgClock(), i == 1 ? kHiresClock : PSG_CLOCK_RATE);
        }
        ASSERT_GT(crossings[0], 700u) << "the tone must render (~880 crossings in 12 frames)";
        const double ratio = double(crossings[1]) / double(crossings[0]);
        EXPECT_NEAR(ratio, 6.0 / 7.0, 0.006) << crossings[1] << " / " << crossings[0];
    }
}

/// A switch requested at T 30000 reaches the generators there - not before - and the output carries on: a constant
/// level renders the same as on a device that never switched (the FIR history is kept and the DC blocker keeps its
/// cutoff; a cleared history would dip to zero for a filter length)
TEST_F(SoundChipTurboSound_Test, SwitchLandsOnItsTStateWithoutAClick)
{
    SoundManager soundS(_context);
    SoundManager soundR(_context);
    SoundChip_TurboSound* switched = Device(soundS);
    SoundChip_TurboSound* reference = Device(soundR);
    ASSERT_NE(switched, nullptr);
    ASSERT_NE(reference, nullptr);
    std::vector<int16_t> frameS;
    std::vector<int16_t> frameR;
    for (SoundChip_TurboSound* device : {switched, reference})
    {
        device->setHQEnabled(true);
        ProgramChannelA(device, 0);
        RenderFrames(device, 2);

        StartFrame(device);
        _z80->t = 30000;
        if (device == switched)
            ASSERT_TRUE(device->SetPsgClock(kHiresClock));
        StepTo(device, 20000);
        EXPECT_EQ(device->GetPsgClock(), PSG_CLOCK_RATE) << "rendering has not reached T 30000";
        StepTo(device, 30000);
        EXPECT_EQ(device->GetPsgClock(), PSG_CLOCK_RATE) << "the render cursor lags the CPU";
        StepTo(device, kFrame);
        EXPECT_EQ(device->GetPsgClock(), device == switched ? kHiresClock : PSG_CLOCK_RATE);
        AppendFrame(device, device == switched ? frameS : frameR);
    }

    ASSERT_EQ(frameS.size(), frameR.size());
    ASSERT_GT(frameS.size(), 800u);
    ASSERT_GT(frameR.front(), 1000) << "channel A holds a level";
    for (size_t i = 0; i < frameS.size(); i++)
        ASSERT_NEAR(frameS[i], frameR[i], 4) << "sample " << i;
}

/// Asking for the clock the device already runs at queues nothing: the device stays byte-identical to one never
/// asked, and a capture at the default clock has the plain cursor of earlier builds (sign extension in its upper half)
TEST_F(SoundChipTurboSound_Test, DefaultClockKeepsTheRenderAndTheCaptureLayout)
{
    SoundManager soundA(_context);
    SoundManager soundB(_context);
    SoundChip_TurboSound* a = Device(soundA);
    SoundChip_TurboSound* b = Device(soundB);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ProgramChannelA(a, 37);
    ProgramChannelA(b, 37);
    EXPECT_TRUE(b->SetPsgClock(PSG_CLOCK_RATE));

    const std::vector<int16_t> streamA = RenderFrames(a, 3);
    const std::vector<int16_t> streamB = RenderFrames(b, 3);
    EXPECT_EQ(streamA, streamB);
    const std::vector<uint8_t> blobA = Save(a);
    EXPECT_EQ(blobA, Save(b));

    // The timeline tail starts after the chip selector and both 73-byte chip payloads
    const size_t cursorAt = 1 + 2 * a->getChip(0)->TTDStateSize();
    int64_t cursor = 0;
    std::memcpy(&cursor, blobA.data() + cursorAt, sizeof(cursor));
    EXPECT_LT(cursor, 0) << "the cursor lags the frame end";
    EXPECT_GT(cursor, -4 * kTurboSoundRenderLagT) << "a plain i64 offset: no clock state in its upper half";
}

/// Save at 1.5 MHz with a switch back still queued and the cursor between whole T-states; a fresh device loaded from
/// it runs the same clock, has the same request pending, re-saves the same bytes and renders the same next frame
TEST_F(SoundChipTurboSound_Test, TtdRoundTripKeepsTheClockState)
{
    SoundManager soundA(_context);
    SoundManager soundB(_context);
    SoundChip_TurboSound* a = Device(soundA);
    SoundChip_TurboSound* b = Device(soundB);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    ASSERT_TRUE(a->SetPsgClock(kHiresClock));
    ProgramChannelA(a, 29);
    RenderFrames(a, 2);
    ASSERT_EQ(a->GetPsgClock(), kHiresClock);

    StartFrame(a);
    StepTo(a, kFrame - 1000);
    _z80->t = kFrame - 100;
    ASSERT_TRUE(a->SetPsgClock(PSG_CLOCK_RATE));
    StepTo(a, kFrame);
    ASSERT_EQ(a->GetPsgClock(), kHiresClock) << "the switch back is still queued at the frame end";
    ASSERT_EQ(a->GetRequestedPsgClock(), PSG_CLOCK_RATE);

    const std::vector<uint8_t> blob = Save(a);
    b->TTDLoadState(blob.data());
    EXPECT_EQ(b->GetPsgClock(), kHiresClock);
    EXPECT_EQ(b->GetRequestedPsgClock(), PSG_CLOCK_RATE);
    EXPECT_EQ(Save(b), blob);

    // The next frame from both: the queued switch lands on the same tick
    std::vector<int16_t> nextA;
    std::vector<int16_t> nextB;
    for (SoundChip_TurboSound* device : {a, b})
    {
        StartFrame(device);
        StepTo(device, kFrame);
        AppendFrame(device, device == a ? nextA : nextB);
        EXPECT_EQ(device->GetPsgClock(), PSG_CLOCK_RATE);
    }
    EXPECT_EQ(Save(a), Save(b)) << "generators, cursor and queues in step a frame later";
    // Output content is not TTD state (the load flushes the decimators, the DC blockers are host-side): only the count
    EXPECT_EQ(nextA.size(), nextB.size());
}

/// region <Reset state>

/// Power-on (the emulator just created) and a machine reset (Core::Reset) both leave the two AY chips in the
/// datasheet reset state: every register 0, so both I/O ports are inputs and IN #FFFD of R14 / R15 reads the pulled-up
/// pins (#FF), not the zero latch. Before the fix the reset set R7 = #FF: both ports outputs, R14 / R15 read 0.
/// Sources: docs/inprogress/2026-10-04-ay-reset/TODO.md
TEST_F(SoundChipTurboSound_Test, PowerOnAndMachineResetGiveTheDatasheetState)
{
    SoundChip_TurboSound* device = Device(*_context->pSoundManager);
    ASSERT_NE(device, nullptr);

    auto expectResetState = [device](const char* when)
    {
        for (int chip = 0; chip < 2; chip++)
        {
            SCOPED_TRACE(testing::Message() << when << ", chip " << chip);
            device->portDeviceOutMethod(0xFFFD, chip == 0 ? 0xFF : 0xFE);  // TurboSound chip select
            const uint8_t* regs = device->getChip(chip)->getRegisters();
            for (int reg = 0; reg < 16; reg++)
                EXPECT_EQ(regs[reg], 0) << "R" << reg;
            device->portDeviceOutMethod(0xFFFD, AY_PORTA);
            EXPECT_EQ(device->portDeviceInMethod(0xFFFD), 0xFF) << "IN #FFFD of R14";
            device->portDeviceOutMethod(0xFFFD, AY_PORTB);
            EXPECT_EQ(device->portDeviceInMethod(0xFFFD), 0xFF) << "IN #FFFD of R15";
        }
        device->portDeviceOutMethod(0xFFFD, 0xFF);
    };

    expectResetState("power-on");

    // Program both chips away from the reset state: ports outputs with latches, sound on
    for (int chip = 0; chip < 2; chip++)
    {
        device->portDeviceOutMethod(0xFFFD, chip == 0 ? 0xFF : 0xFE);
        Poke(device, AY_MIXER_CONTROL, 0xF8);
        Poke(device, AY_PORTA, 0x12);
        Poke(device, AY_PORTB, 0x34);
        Poke(device, AY_A_VOLUME, 0x0F);
        device->portDeviceOutMethod(0xFFFD, AY_PORTA);
        ASSERT_EQ(device->portDeviceInMethod(0xFFFD), 0x12) << "output port reads its latch";
    }

    _context->pCore->Reset();
    expectResetState("machine reset");
}

/// endregion </Reset state>

/// region <Sample phase and tone across machine events>

// The device renders its frame buffers with its own copy of the mixer's sample accumulator (_samplePhase). The mixer
// reads `samplesThisFrame` samples from the buffers every frame; when the device's count for a frame differs, the mixer
// reads a never-rendered zero sample or drops one - a click. The TSFM's copy drifted after a host speed multiplier
// (master cbf7f777b); this suite runs the same events on the single AY (48K, 128K, Pentagon) and the 2 x AY TurboSound
// (Pentagon) in a running machine and checks content: the tone's frequency and its periods, no sample-to-sample step
// beyond the tone's own, and the device's phase equal to the mixer's at every frame end afterwards.
namespace aytone
{

enum class Board
{
    Ay48,
    Ay128,
    AyPentagon,
    TsPentagon,
    TsfmPentagon   ///< the TSFM's SSG (its FM silent): the same render loop, the same rule
};

enum class Event
{
    None,           ///< frame boundaries only
    MachineReset,   ///< Emulator::Reset
    SnapshotLoad,   ///< a .sna load (machine reset + state load)
    CoreRate,       ///< the core sample rate to 48 kHz (at the next frame boundary)
    HostSpeed,      ///< host speed x2 for four frames, back to x1
    HostSpeedX4,    ///< host speed x4 for three frames, back to x1
    HardwareTurbo,  ///< the guest's hardware CPU clock x2 for four frames (hw_turbo_ratio), back to x1
    TurboMode,      ///< turbo (maximum speed) without audio for four frames
    TurboAudio,     ///< turbo with audio for four frames
    SoundOff        ///< the sound feature off for four frames, back on
};

const char* BoardName(Board b)
{
    switch (b)
    {
        case Board::Ay48: return "Ay48";
        case Board::Ay128: return "Ay128";
        case Board::AyPentagon: return "AyPentagon";
        case Board::TsPentagon: return "TsPentagon";
        case Board::TsfmPentagon: return "TsfmPentagon";
    }
    return "?";
}

const char* EventName(Event e)
{
    switch (e)
    {
        case Event::None: return "None";
        case Event::MachineReset: return "MachineReset";
        case Event::SnapshotLoad: return "SnapshotLoad";
        case Event::CoreRate: return "CoreRate";
        case Event::HostSpeed: return "HostSpeed";
        case Event::HostSpeedX4: return "HostSpeedX4";
        case Event::HardwareTurbo: return "HardwareTurbo";
        case Event::TurboMode: return "TurboMode";
        case Event::TurboAudio: return "TurboAudio";
        case Event::SoundOff: return "SoundOff";
    }
    return "?";
}

/// The event's point in a frame: 0 = T 0, 1 = mid-frame, 2 = the frame's last T-state
const char* OffsetName(uint32_t part)
{
    return part == 0 ? "T0" : part == 1 ? "Mid" : "End";
}

constexpr uint16_t kCode = 0x8000;
constexpr uint16_t kPeriod0 = 0x100;   // chip 0 tone A: 427.2 Hz at 1.75 MHz
constexpr uint16_t kPeriod1 = 0x0C0;   // chip 1 tone A: 569.7 Hz

/// The tone as a Z80 program (a reset and a TTD replay re-run it): tone A of chip 0 (and of chip 1 on the TurboSound)
/// at full volume, then DI; HALT. Chip select: #FF / #FE on the TurboSound, #FC / #FD on the TSFM (bit 0 the chip, FM
/// off)
std::vector<uint8_t> ToneProgram(Board board)
{
    std::vector<uint8_t> code{0xF3};   // DI
    auto out = [&](uint16_t port, uint8_t value)
    {
        code.insert(code.end(), {0x01, uint8_t(port), uint8_t(port >> 8), 0x3E, value, 0xED, 0x79});
    };
    auto reg = [&](uint8_t r, uint8_t v)
    {
        out(0xFFFD, r);
        out(0xBFFD, v);
    };
    auto tone = [&](uint16_t period)
    {
        reg(0x00, uint8_t(period & 0xFF));
        reg(0x01, uint8_t(period >> 8));
        reg(0x07, 0x3E);   // tone A only
        reg(0x08, 0x0F);
        reg(0x09, 0x00);
        reg(0x0A, 0x00);
    };
    if (board == Board::TsPentagon)
    {
        out(0xFFFD, 0xFE);
        tone(kPeriod1);
        out(0xFFFD, 0xFF);
    }
    else if (board == Board::TsfmPentagon)
    {
        out(0xFFFD, 0xFD);
        tone(kPeriod1);
        out(0xFFFD, 0xFC);
    }
    tone(kPeriod0);
    code.insert(code.end(), {0xF3, 0x76});   // DI; HALT
    return code;
}

/// A machine with the AY or the TurboSound in its AY socket ([SLOTS] replaced: nothing else fitted), the audio
/// capture analyzer on
class AyMachine
{
public:
    explicit AyMachine(Board board) : _board(board)
    {
        const char* folder = board == Board::Ay48 ? "spectrum48" : board == Board::Ay128 ? "spectrum128" : "pentagon128k";
        const std::filesystem::path source = TestPathHelper::FindProjectRoot() / "data" / "configs" / folder / "unreal.ini";
        std::ifstream in(source, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t shipped = text.find("\n[SLOTS]");
        if (shipped != std::string::npos)
        {
            const size_t next = text.find("\n[", shipped + 1);
            text.erase(shipped, next == std::string::npos ? std::string::npos : next - shipped);
        }
        const char* socket = board == Board::TsPentagon ? "ts" : board == Board::TsfmPentagon ? "tsfm" : "ay";
        text += std::string("\n[SLOTS]\nay-socket = ") + socket + "\n";
        _path = TestPathHelper::GetUniqueTestScratchPath(std::string("aytone-") + folder + ".ini");
        std::ofstream out(_path, std::ios::binary);
        out << text;
        out.close();
        _emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
        _emulator->SetCustomConfigPath(_path.string());
        _ok = _emulator->Init();
    }
    ~AyMachine()
    {
        _emulator->Release();
        std::error_code ignored;
        std::filesystem::remove(_path, ignored);
    }

    bool Init()
    {
        if (!_ok)
            return false;
        AnalyzerManager* analyzers = Context()->pDebugManager->GetAnalyzerManager();
        analyzers->activate("audiocapture");
        _capture = analyzers->getAnalyzer<AudioCaptureAnalyzer>("audiocapture");
        ITurboSoundDevice* device = Device();
        return _capture != nullptr && device != nullptr && device->getChipCount() == (TurboSound() ? 2 : 1);
    }

    Emulator& Machine() { return *_emulator; }
    EmulatorContext* Context() const { return _emulator->GetContext(); }
    bool TurboSound() const { return _board == Board::TsPentagon || _board == Board::TsfmPentagon; }
    ITurboSoundDevice* Device() const { return Context()->pSoundManager->getTurboSound(); }
    double Rate() const { return double(Context()->pSoundManager->getCoreRate()); }
    double ToneHz(int chip) const
    {
        return double(Device()->GetPsgClock()) / 16.0 / double(chip == 0 ? kPeriod0 : kPeriod1);
    }

    void PlayTone()
    {
        const std::vector<uint8_t> code = ToneProgram(_board);
        Z80* z80 = Context()->pCore->GetZ80();
        for (size_t i = 0; i < code.size(); i++)
            z80->DirectWrite(static_cast<uint16_t>(kCode + i), code[i]);
        z80->halted = 0;
        z80->pc = kCode;
    }

    /// One frame; with `check`, the device rendered the mixer's count and ends the frame on the mixer's phase
    void Frame(bool check, const std::string& where)
    {
        Machine().RunFrame(true);
        if (!check)
            return;
        SoundManager* sound = Context()->pSoundManager;
        const uint64_t frame = Context()->emulatorState.frame_counter;
        EXPECT_EQ(sound->lastTurboSoundSamples(), sound->lastFrameSamples())
            << where << ", frame " << frame << ": the device rendered a different count than the mixer read";
        EXPECT_EQ(sound->lastTurboSoundPhase(), sound->samplePhase())
            << where << ", frame " << frame << ": the device's sample phase left the mixer's";
    }
    void Frames(int n, bool check, const std::string& where)
    {
        for (int i = 0; i < n && !::testing::Test::HasFailure(); i++)
            Frame(check, where);
    }

    /// `seconds` of one chip's left channel from the next frame on, mean removed; every frame checked
    std::vector<double> Capture(int chip, double seconds, const std::string& where)
    {
        _capture->startCapture(static_cast<size_t>(seconds * Rate()) * 2,
                               chip == 0 ? AudioSourceType::AY1_All : AudioSourceType::AY2_All);
        for (int guard = 0; guard < 400 && !_capture->isCaptureComplete(); guard++)
            Frame(true, where);
        std::vector<double> left(_capture->getCapturedSamples() / 2);
        double mean = 0.0;
        for (size_t i = 0; i < left.size(); i++)
            mean += left[i] = _capture->getBuffer()[i * 2];
        mean /= double(left.empty() ? 1 : left.size());
        for (double& v : left)
            v -= mean;
        _capture->stopCapture();
        return left;
    }

private:
    SoundCardScope _turboSoundScope{TestSound::TurboSound};
    Board _board;
    std::filesystem::path _path;
    std::unique_ptr<Emulator> _emulator;
    bool _ok = false;
    AudioCaptureAnalyzer* _capture = nullptr;
};

/// The tone in a capture: frequency from the rising zero crossings (interpolated), the largest deviation of one period
/// from their mean, and the largest sample-to-sample step
struct Tone
{
    double hz = 0.0;
    double worstPeriod = 1.0;   // max |period - mean| / mean
    double maxStep = 0.0;
    double peak = 0.0;
    size_t periods = 0;
};

Tone Measure(const std::vector<double>& x, double rate)
{
    Tone tone;
    std::vector<double> at;
    for (size_t i = 1; i < x.size(); i++)
    {
        tone.maxStep = std::max(tone.maxStep, std::abs(x[i] - x[i - 1]));
        tone.peak = std::max(tone.peak, std::abs(x[i]));
        if (x[i - 1] < 0.0 && x[i] >= 0.0)
            at.push_back(double(i - 1) + x[i - 1] / (x[i - 1] - x[i]));
    }
    if (at.size() < 3)
        return tone;
    tone.periods = at.size() - 1;
    const double mean = (at.back() - at.front()) / double(tone.periods);
    tone.hz = rate / mean;
    tone.worstPeriod = 0.0;
    for (size_t i = 1; i < at.size(); i++)
        tone.worstPeriod = std::max(tone.worstPeriod, std::abs((at[i] - at[i - 1]) - mean) / mean);
    return tone;
}

/// Each chip's tone against the reference taken before the event: the frequency within 1 %, no period off by more than
/// 5 %, no step more than 10 % above the tone's own largest step
void ExpectTone(AyMachine& m, const Tone (&reference)[2], const std::string& where)
{
    for (int chip = 0; chip < (m.TurboSound() ? 2 : 1); chip++)
    {
        const std::string what = where + ", chip " + std::to_string(chip);
        const Tone tone = Measure(m.Capture(chip, 0.15, what), m.Rate());
        EXPECT_NEAR(tone.hz, m.ToneHz(chip), m.ToneHz(chip) * 0.01) << what << ": tone frequency";
        EXPECT_LT(tone.worstPeriod, 0.05) << what << ": a period broken (" << tone.periods << " periods)";
        EXPECT_LE(tone.maxStep, reference[chip].maxStep * 1.1)
            << what << ": a step beyond the tone's own (" << reference[chip].maxStep << ")";
        EXPECT_GT(tone.peak, reference[chip].peak * 0.9) << what << ": the tone lost its level";
    }
}

} // namespace aytone

class SoundChipTurboSoundEvents_Test : public ::testing::TestWithParam<std::tuple<aytone::Board, aytone::Event, uint32_t>>
{
};

/// One board, one event at one point of a frame (0, mid, the last T-state): the tone carries on and the device's sample
/// phase is the mixer's at every frame end afterwards. ~60-120 ms each (a machine, ~40 emulated frames with HQ AY
/// rendering, two to four captures)
TEST_P(SoundChipTurboSoundEvents_Test, ToneAndSamplePhaseSurviveEvent)
{
    using namespace aytone;
    const auto [board, event, offsetPart] = GetParam();

    AyMachine m(board);
    ASSERT_TRUE(m.Init());
    const uint32_t frameT = m.Context()->config.frame;
    const uint32_t offset = offsetPart == 0 ? 0 : offsetPart == 1 ? frameT / 2 : frameT - 1;
    const std::string where = std::string(BoardName(board)) + " " + EventName(event) + " at T " + std::to_string(offset);

    m.PlayTone();
    m.Frames(3, true, where + " (before)");
    Tone reference[2];
    for (int chip = 0; chip < (m.TurboSound() ? 2 : 1); chip++)
    {
        reference[chip] = Measure(m.Capture(chip, 0.1, where + " (reference)"), m.Rate());
        ASSERT_NEAR(reference[chip].hz, m.ToneHz(chip), m.ToneHz(chip) * 0.01) << where << ": reference, chip " << chip;
        ASSERT_GT(reference[chip].peak, 500.0) << where << ": reference, chip " << chip;
    }

    if (offset > 0)
        m.Machine().RunTStates(offset);
    switch (event)
    {
        case Event::None:
            break;
        case Event::MachineReset:
            m.Machine().Reset();
            m.PlayTone();
            break;
        case Event::SnapshotLoad:
        {
            const auto sna = TestPathHelper::FindProjectRoot() / "testdata/loaders/sna/Timing_Tests-48k_v1.0.sna";
            ASSERT_TRUE(m.Machine().LoadSnapshot(sna.string())) << where;
            m.PlayTone();
            break;
        }
        case Event::CoreRate:
            m.Context()->pSoundManager->requestCoreRate(48000);
            break;
        case Event::HostSpeed:
            ASSERT_TRUE(m.Machine().SetSpeedMultiplier(2));
            m.Frames(4, false, where);
            ASSERT_TRUE(m.Machine().SetSpeedMultiplier(1));
            break;
        case Event::HostSpeedX4:
            ASSERT_TRUE(m.Machine().SetSpeedMultiplier(4));
            m.Frames(3, false, where);
            ASSERT_TRUE(m.Machine().SetSpeedMultiplier(1));
            break;
        case Event::HardwareTurbo:
            m.Context()->emulatorState.hw_turbo_ratio = 2;
            m.Frames(4, true, where + " (hardware turbo)");
            m.Context()->emulatorState.hw_turbo_ratio = 1;
            break;
        case Event::TurboMode:
            m.Machine().EnableTurboMode(false);
            m.Frames(4, false, where);
            m.Machine().DisableTurboMode();
            break;
        case Event::TurboAudio:
            m.Machine().EnableTurboMode(true);
            m.Frames(4, true, where + " (turbo with audio)");
            m.Machine().DisableTurboMode();
            break;
        case Event::SoundOff:
            m.Machine().GetFeatureManager()->setFeature(Features::kSoundGeneration, false);
            m.Frames(4, false, where);
            m.Machine().GetFeatureManager()->setFeature(Features::kSoundGeneration, true);
            break;
    }
    // The frame the event fell in; then every frame is checked
    m.Frames(1, false, where);
    m.Frames(3, true, where + " (settle)");
    if (event == Event::CoreRate)
        ASSERT_EQ(m.Rate(), 48000.0) << where;

    ExpectTone(m, reference, where);
    m.Frames(10, true, where + " (after)");
}

INSTANTIATE_TEST_SUITE_P(
    BoardsEventsOffsets, SoundChipTurboSoundEvents_Test,
    ::testing::Combine(::testing::Values(aytone::Board::Ay48, aytone::Board::Ay128, aytone::Board::AyPentagon,
                                         aytone::Board::TsPentagon, aytone::Board::TsfmPentagon),
                       ::testing::Values(aytone::Event::None, aytone::Event::MachineReset, aytone::Event::SnapshotLoad,
                                         aytone::Event::CoreRate, aytone::Event::HostSpeed, aytone::Event::HostSpeedX4,
                                         aytone::Event::HardwareTurbo, aytone::Event::TurboMode,
                                         aytone::Event::TurboAudio, aytone::Event::SoundOff),
                       ::testing::Values(0u, 1u, 2u)),
    [](const ::testing::TestParamInfo<SoundChipTurboSoundEvents_Test::ParamType>& info)
    {
        return std::string(aytone::BoardName(std::get<0>(info.param))) + "_" +
               aytone::EventName(std::get<1>(info.param)) + "_" + aytone::OffsetName(std::get<2>(info.param));
    });

/// TTD: a seek at any point of a frame and the replay from there bring the device's sample phase back where it was live
/// (equal to the mixer's at every later frame end) and the same audio: the capture taken at the same frames live and
/// replayed agrees within 1 % of its peak. Single AY and TurboSound on the Pentagon, three offsets (~0.5 s in all: two
/// machines, TTD recording, ~50 frames x 4 runs)
TEST(SoundChipTurboSoundEventsTtd_Test, SeekAndReplayKeepTheSamplePhaseAndTheTone)
{
    using namespace aytone;
    for (const Board board : {Board::AyPentagon, Board::TsPentagon})
    {
        const char* name = BoardName(board);
        AyMachine m(board);
        ASSERT_TRUE(m.Init()) << name;
        FeatureManager* features = m.Machine().GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        m.Context()->pMemory->UpdateFeatureCache();
        ttd::TimeTravelController* ttd = m.Context()->pTimeTravelController;
        ASSERT_NE(ttd, nullptr);
        const uint64_t& frame = m.Context()->emulatorState.frame_counter;
        const uint32_t frameT = m.Context()->config.frame;

        ASSERT_TRUE(ttd->StartRecording()) << name;
        const uint64_t start = frame;
        m.PlayTone();
        std::vector<uint64_t> livePhase;
        for (int i = 0; i < 24; i++)
        {
            m.Frame(true, name);
            livePhase.push_back(m.Context()->pSoundManager->lastTurboSoundPhase());
        }
        const uint64_t captureFrame = frame;
        const int lastChip = m.TurboSound() ? 1 : 0;
        const std::vector<double> live = m.Capture(lastChip, 0.1, name);
        ttd->StopRecording();
        ASSERT_GT(live.size(), 4000u) << name;

        for (const uint32_t offset : {0u, frameT / 2, frameT - 1})
        {
            const std::string where = std::string(name) + " seek at T " + std::to_string(offset);
            ASSERT_TRUE(ttd->SeekTo({10, offset})) << where;
            Z80& z80 = *m.Context()->pCore->GetZ80();
            if (frame < start + 11)
                m.Machine().RunTStates(frameT - z80.t);
            ASSERT_EQ(frame, start + 11) << where;
            for (uint64_t f = 11; f <= livePhase.size(); f++)
            {
                if (f > 11)
                    m.Frame(true, where);
                if (f > 11)
                    EXPECT_EQ(m.Context()->pSoundManager->lastTurboSoundPhase(), livePhase[f - 1]) << where << ", frame " << f;
            }
            ASSERT_EQ(frame, captureFrame) << where;
            const std::vector<double> replay = m.Capture(lastChip, 0.1, where);
            ASSERT_EQ(replay.size(), live.size()) << where;
            double peak = 0.0;
            double worst = 0.0;
            for (size_t i = 0; i < live.size(); i++)
            {
                peak = std::max(peak, std::abs(live[i]));
                worst = std::max(worst, std::abs(live[i] - replay[i]));
            }
            EXPECT_GT(peak, 500.0) << where;
            EXPECT_LT(worst, peak * 0.01) << where << ": replayed AY differs from live";
        }
    }
}

/// endregion </Sample phase and tone across machine events>
