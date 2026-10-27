#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/tsfmplayerharness.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"
#include "emulator/sound/chips/tsfm/ym2203pair.h"

/// YM2203 channel decimator quality harness (docs/inprogress/2026-10-03-zx-multisound/ym-decimator-prototype.md).
///
/// Tools, not checks (DISABLED_, run by name with --gtest_also_run_disabled_tests):
///  - CaptureTraces: timed #FFFD / #BFFD writes of real tunes (tech_support.sna on a TSFM Pentagon, the TFM Music
///    Compiler tunes of TSFM-EL.TAP) into $YMQ_DIR/traces/<name>.txt;
///  - RenderTraces: every trace plus synthetic worst cases (constant SSG levels, fast volume writes, noise, a tone
///    sweep) rendered through the per-channel outputs of a Ym2203Pair (the ZX-MultiSound path: 8 float streams,
///    $YMQ_DIR/out/<name>.pair.f32, interleaved FM0 FM1 SSG0A..C SSG1A..C) and through a SoundChip_TurboSoundFM
///    (the board path: stereo mix, chip and FM buffers, $YMQ_DIR/out/<name>.tsfm.s16).
/// The same traces through two builds give the files the analysis compares (tools in the worktree's scratch/).
namespace
{
constexpr uint64_t kFrameTicks = 71680;   // Pentagon

struct TraceWrite
{
    uint32_t frame;
    uint32_t t;
    uint16_t port;    // 0xFFFD or 0xBFFD
    uint8_t value;
};

std::filesystem::path OutDir()
{
    const char* dir = std::getenv("YMQ_DIR");
    return dir ? std::filesystem::path(dir) : TestPathHelper::FindProjectRoot() / "scratch/ym-quality";
}

void SaveTrace(const std::string& name, const std::vector<TraceWrite>& writes, uint32_t frames)
{
    const auto dir = OutDir() / "traces";
    std::filesystem::create_directories(dir);
    std::ofstream f(dir / (name + ".txt"));
    f << "frames " << frames << "\n";
    for (const TraceWrite& w : writes)
        f << w.frame << " " << w.t << " " << w.port << " " << unsigned(w.value) << "\n";
}

bool LoadTrace(const std::filesystem::path& path, std::vector<TraceWrite>& writes, uint32_t& frames)
{
    std::ifstream f(path);
    std::string word;
    if (!(f >> word >> frames))
        return false;
    TraceWrite w{};
    unsigned port = 0;
    unsigned value = 0;
    while (f >> w.frame >> w.t >> port >> value)
    {
        w.port = static_cast<uint16_t>(port);
        w.value = static_cast<uint8_t>(value);
        writes.push_back(w);
    }
    return true;
}

/// A TS port write seen by the Z80 (partial decode as the TSFM: A15 A14 A1)
bool TsPort(uint16_t port, uint16_t& normalized)
{
    if ((port & 0xC002) == 0xC000)
        normalized = 0xFFFD;
    else if ((port & 0xC002) == 0x8000)
        normalized = 0xBFFD;
    else
        return false;
    return true;
}

/// Synthetic worst cases, written with TSFM control words (0xFB: chip 0, FM on; 0xFA: chip 1)
class Synth
{
public:
    void Reg(uint32_t frame, uint32_t& t, uint8_t reg, uint8_t value)
    {
        _w.push_back({frame, t, 0xFFFD, reg});
        _w.push_back({frame, t + 40, 0xBFFD, value});
        t += 80;
    }
    void Control(uint32_t frame, uint32_t& t, uint8_t value)
    {
        _w.push_back({frame, t, 0xFFFD, value});
        t += 40;
    }
    std::vector<TraceWrite> _w;
};

std::vector<TraceWrite> SynthConstant(uint32_t frames)
{
    // Tone and noise off: every channel holds its volume's level; the volumes step every 50 frames
    Synth s;
    for (uint32_t f = 0; f < frames; f++)
    {
        uint32_t t = 100;
        if (f % 50 != 0)
            continue;
        for (uint8_t chip : {uint8_t{0xFB}, uint8_t{0xFA}})
        {
            s.Control(f, t, chip);
            s.Reg(f, t, 7, 0x3F);
            s.Reg(f, t, 8, static_cast<uint8_t>((f / 50 * 3) % 16));
            s.Reg(f, t, 9, static_cast<uint8_t>((f / 50 * 5 + 7) % 16));
            s.Reg(f, t, 10, chip == 0xFB ? 0 : 15);
        }
    }
    return s._w;
}

std::vector<TraceWrite> SynthFastVolume(uint32_t frames)
{
    // 4-bit sample playback through the SSG volume (tone and noise off): channel A rewritten every 224 ticks
    // (15.6 kHz) with a 1 kHz-ish triangle, channel B of chip 1 with a pseudo-random sequence
    Synth s;
    uint32_t lcg = 12345;
    {
        uint32_t t = 100;
        s.Control(0, t, 0xFB);
        s.Reg(0, t, 7, 0x3F);
        s.Control(0, t, 0xFA);
        s.Reg(0, t, 7, 0x3F);
    }
    for (uint32_t f = 1; f < frames; f++)
    {
        uint32_t n = 0;
        for (uint32_t t = 100; t + 224 < kFrameTicks; t += 224, n++)
        {
            uint32_t at = t;
            const uint32_t k = (f * 320 + n) % 16;
            const uint8_t tri = static_cast<uint8_t>(k < 8 ? k * 2 : (15 - k) * 2);
            s.Control(f, at, 0xFB);
            s.Reg(f, at, 8, tri);
            lcg = lcg * 1103515245u + 12345u;
            s.Control(f, at, 0xFA);
            s.Reg(f, at, 9, static_cast<uint8_t>((lcg >> 16) & 15));
        }
    }
    return s._w;
}

std::vector<TraceWrite> SynthNoise(uint32_t frames)
{
    // Noise on all six channels at volume 15, the noise period swept 0..31
    Synth s;
    for (uint32_t f = 0; f < frames; f++)
    {
        uint32_t t = 100;
        for (uint8_t chip : {uint8_t{0xFB}, uint8_t{0xFA}})
        {
            s.Control(f, t, chip);
            if (f == 0)
            {
                s.Reg(f, t, 7, 0x07);    // tone off, noise on
                s.Reg(f, t, 8, 15);
                s.Reg(f, t, 9, 15);
                s.Reg(f, t, 10, 15);
            }
            s.Reg(f, t, 6, static_cast<uint8_t>((f / 8) % 32));
        }
    }
    return s._w;
}

std::vector<TraceWrite> SynthToneSweep(uint32_t frames)
{
    // Square tones swept from period 1 (109 kHz) to 1000 on chip 0 A, a fixed high tone (period 3) on chip 1 C,
    // and an FM note on chip 0 (one carrier, algorithm 7) - the images above 20 kHz the decimator must remove
    Synth s;
    {
        uint32_t t = 100;
        s.Control(0, t, 0xFB);
        s.Reg(0, t, 7, 0x3E);
        s.Reg(0, t, 8, 15);
        s.Reg(0, t, 0xB0, 0x07);
        for (uint8_t op = 0; op < 4; op++)
        {
            s.Reg(0, t, static_cast<uint8_t>(0x30 + op * 4), 0x01);
            s.Reg(0, t, static_cast<uint8_t>(0x40 + op * 4), op == 3 ? 0x00 : 0x7F);
            s.Reg(0, t, static_cast<uint8_t>(0x50 + op * 4), 0x1F);
            s.Reg(0, t, static_cast<uint8_t>(0x80 + op * 4), 0x0F);
        }
        s.Reg(0, t, 0xA4, 0x22);
        s.Reg(0, t, 0xA0, 0x69);
        s.Reg(0, t, 0x28, 0xF0);
        s.Control(0, t, 0xFA);
        s.Reg(0, t, 7, 0x3B);
        s.Reg(0, t, 10, 12);
        s.Reg(0, t, 4, 3);
        s.Reg(0, t, 5, 0);
    }
    for (uint32_t f = 1; f < frames; f++)
    {
        uint32_t t = 100;
        const uint32_t period = 1 + f * 999 / frames;
        s.Control(f, t, 0xFB);
        s.Reg(f, t, 0, static_cast<uint8_t>(period & 0xFF));
        s.Reg(f, t, 1, static_cast<uint8_t>(period >> 8));
    }
    return s._w;
}

/// The ZX-MultiSound path: per-channel outputs of a Ym2203Pair on a continuous card axis (the card's own
/// configuration), the TSFM control word (bit 0 = 0: chip 1; bit 2 set: FM off), one render per frame
void RenderPair(EmulatorContext* context, const std::vector<TraceWrite>& writes, uint32_t frames,
                const std::filesystem::path& out)
{
    Ym2203PairConfig config;
    config.masterClockHz = 3500000;
    config.hostTickRate = 3500000;
    config.continuousHostAxis = true;
    Ym2203Pair pair(context, config);
    pair.configureChannelOutputs(44100);
    pair.syncTo(0);

    std::ofstream f(out, std::ios::binary);
    std::vector<float> streams[8];
    int chip = 0;
    bool fmEnabled = true;
    size_t index = 0;
    uint64_t samplesAcc = 0;
    for (uint32_t frame = 0; frame < frames; frame++)
    {
        const uint64_t base = uint64_t(frame) * kFrameTicks;
        for (; index < writes.size() && writes[index].frame == frame; index++)
        {
            const TraceWrite& w = writes[index];
            pair.syncTo(base + w.t);
            if (w.port == 0xFFFD && w.value >= 0xF8)
            {
                chip = (w.value & 1) ? 0 : 1;
                fmEnabled = (w.value & 4) == 0;
            }
            else if (w.port == 0xFFFD)
                pair.writeAddress(chip, w.value);
            else
                pair.writeData(chip, w.value);
        }
        pair.syncTo(base + kFrameTicks);
        samplesAcc += kFrameTicks * 44100;
        const size_t n = static_cast<size_t>(samplesAcc / 3500000);
        samplesAcc %= 3500000;
        Ym2203ChannelBlock block;
        for (auto& s : streams)
            s.assign(n, 0.0f);
        for (int c = 0; c < 2; c++)
        {
            block.fm[c] = streams[c].data();
            for (int ch = 0; ch < 3; ch++)
                block.ssg[c][ch] = streams[2 + c * 3 + ch].data();
        }
        pair.beginChannelRender(n);
        ASSERT_EQ(pair.renderChannels(n, block, fmEnabled), n);
        std::vector<float> inter(n * 8);
        for (size_t k = 0; k < n; k++)
            for (int s = 0; s < 8; s++)
                inter[k * 8 + s] = streams[s][k];
        f.write(reinterpret_cast<const char*>(inter.data()), static_cast<std::streamsize>(inter.size() * sizeof(float)));
    }
}

/// The board path: a standalone SoundChip_TurboSoundFM at 44.1 kHz HQ; per frame the stereo mix, then per chip
/// the SSG and FM buffers (each stereo int16)
void RenderTsfm(EmulatorContext* context, const std::vector<TraceWrite>& writes, uint32_t frames,
                const std::filesystem::path& out)
{
    auto device = std::make_unique<SoundChip_TurboSoundFM>(context);
    device->setCoreRate(44100);
    device->setHQEnabled(true);
    Z80* z80 = context->pCore->GetZ80();
    auto setT = [&](uint32_t t) { z80->tt = t << 8; };
    setT(0);
    device->reset();

    std::ofstream f(out, std::ios::binary);
    size_t index = 0;
    for (uint32_t frame = 0; frame < frames; frame++)
    {
        setT(0);
        device->handleFrameStart();
        for (; index < writes.size() && writes[index].frame == frame; index++)
        {
            const TraceWrite& w = writes[index];
            setT(w.t);
            device->portDeviceOutMethod(w.port, w.value);
            if ((index & 15) == 0)
                device->handleStep();
        }
        setT(static_cast<uint32_t>(kFrameTicks));
        device->handleStep();
        device->handleFrameEnd();
        const size_t n = device->getRenderedSamplesThisFrame() * AUDIO_CHANNELS;
        f.write(reinterpret_cast<const char*>(device->getAudioBuffer()), static_cast<std::streamsize>(n * 2));
        for (int c = 0; c < 2; c++)
        {
            f.write(reinterpret_cast<const char*>(device->getChipBuffer(c)), static_cast<std::streamsize>(n * 2));
            f.write(reinterpret_cast<const char*>(device->getFmBuffer(c)), static_cast<std::streamsize>(n * 2));
        }
    }
}
}  // namespace

class Ym2203PairQuality_Test : public ::testing::Test
{
};

TEST_F(Ym2203PairQuality_Test, DISABLED_CaptureTraces)
{
    constexpr uint32_t kFrames = 1500;   // 30 s
    std::vector<TraceWrite> writes;
    uint32_t frame = 0;

    // tech_support.sna on a Pentagon with the TSFM in the AY socket
    {
        SoundCardScope turboSound(TestSound::TurboSound);
        Emulator emulator(LoggerLevel::LogError);
        emulator.SetCustomConfigPath(EmulatorTestHelper::StageTurboSoundKindConfig(TurboSoundKind::FM));
        ASSERT_TRUE(emulator.Init());
        const auto sna = TestPathHelper::FindProjectRoot() / "testdata/sound/tsfm/tech_support.sna";
        ASSERT_TRUE(emulator.LoadSnapshot(sna.string()));
        Z80* z80 = emulator.GetContext()->pCore->GetZ80();
        z80->busTraceHook = [&](char type, uint16_t port, uint8_t value)
        {
            uint16_t p = 0;
            if (type == 'O' && TsPort(port, p))
                writes.push_back({frame, static_cast<uint32_t>(z80->t), p, value});
        };
        for (frame = 0; frame < kFrames; frame++)
            EmulatorTestHelper::RunFramesFast(&emulator, 1);
        z80->busTraceHook = nullptr;
        emulator.Stop();
        emulator.Release();
        EXPECT_GT(writes.size(), 10000u);
        SaveTrace("tech-support", writes, kFrames);
    }

    // TFM Music Compiler tunes (TSFM-EL.TAP), 20 s each
    for (size_t tune = 0; tune < 3; tune++)
    {
        constexpr uint32_t kTuneFrames = 1000;
        Emulator* emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::FM);
        ASSERT_NE(emulator, nullptr);
        TsfmPlayerHarness harness;
        std::string error;
        ASSERT_TRUE(harness.Setup(emulator, tune, &error)) << error;
        Z80* z80 = emulator->GetContext()->pCore->GetZ80();
        writes.clear();
        auto previous = z80->busTraceHook;
        z80->busTraceHook = [&, previous](char type, uint16_t port, uint8_t value)
        {
            if (previous)
                previous(type, port, value);
            uint16_t p = 0;
            if (type == 'O' && TsPort(port, p))
                writes.push_back({frame, static_cast<uint32_t>(z80->t), p, value});
        };
        for (frame = 0; frame < kTuneFrames; frame++)
            harness.RunFrames(1);
        z80->busTraceHook = previous;
        harness.Detach();
        EmulatorTestHelper::CleanupEmulator(emulator);
        EXPECT_GT(writes.size(), 1000u);
        SaveTrace("tfm-el-" + std::to_string(tune), writes, kTuneFrames);
    }
}

TEST_F(Ym2203PairQuality_Test, DISABLED_RenderTraces)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    struct Case
    {
        std::string name;
        std::vector<TraceWrite> writes;
        uint32_t frames;
    };
    std::vector<Case> cases;
    for (const auto& entry : std::filesystem::directory_iterator(OutDir() / "traces"))
    {
        Case c;
        c.name = entry.path().stem().string();
        ASSERT_TRUE(LoadTrace(entry.path(), c.writes, c.frames));
        cases.push_back(std::move(c));
    }
    cases.push_back({"synth-constant", SynthConstant(500), 500});
    cases.push_back({"synth-fastvolume", SynthFastVolume(250), 250});
    cases.push_back({"synth-noise", SynthNoise(250), 250});
    cases.push_back({"synth-tonesweep", SynthToneSweep(250), 250});

    const auto out = OutDir() / "out";
    std::filesystem::create_directories(out);
    for (const Case& c : cases)
    {
        RenderPair(context, c.writes, c.frames, out / (c.name + ".pair.f32"));
        RenderTsfm(context, c.writes, c.frames, out / (c.name + ".tsfm.s16"));
    }
    EmulatorTestHelper::CleanupEmulator(emulator);
}
