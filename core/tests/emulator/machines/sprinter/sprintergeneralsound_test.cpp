// The General Sound / NeoGS on the Sprinter's ISA ZX-bus adapter, end to end (docs/inprogress/2026-10-02-sprinter-isa/
// tdd.md §11 T-ISA-8, T-ISA-10, T-ISA-12; phase I2):
//   - SprinterGeneralSound_Test: a Z80 program in RAM opens slot 1 the way ProPlay does (#1FFD = #11, window 3 = #D4,
//     #9FBD = 0), sends the GS warm restart #F3 and the command #20 (total RAM) through #C0BB, and reads the three
//     answer bytes through #C0B3: the card's firmware answers through the adapter, on the classic GS and the NeoGS;
//   - SprinterProPlay_Test (env UNREAL_SPRINTER_HDD, the MAME pack's DSS 1.71 system disk): PROPLAY.EXE from the
//     disk plays the generated test MOD (tools/machines/sprinter/test-mod/, open question Q10) on the GS behind the
//     adapter: the three notes come out at the pitch the MOD's periods give and 1.92 s apart, as its speed / tempo
//     give; the session, recorded with TTD from before the command is typed, replays to the same GS and ISA blobs and
//     the same sound.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"
#include "sprinterzxsession.h"

namespace
{
constexpr double kPi = 3.14159265358979323846;

/// ProPlay's open sequence, then #F3 and #20 (total memory: three bytes L, H, C) through the adapter; the answer goes
/// to #A000-#A002, #A003 = 1 when done
const std::vector<uint8_t> kGsProgram = {
    0xF3,                    // DI
    0x01, 0xFD, 0x1F,        // LD BC,#1FFD
    0x3E, 0x11,              // LD A,#11            pages #D0-#DF mean ISA
    0xED, 0x79,              // OUT (C),A
    0x3E, 0xD4,              // LD A,#D4
    0xD3, 0xE2,              // OUT (#E2),A         window 3 = slot 1 I/O
    0x01, 0xBD, 0x9F,        // LD BC,#9FBD
    0xAF,                    // XOR A
    0xED, 0x79,              // OUT (C),A           A19-A14 = 0, AEN = 0, RESET = 0
    0x3E, 0xF3,              // LD A,#F3
    0x32, 0xBB, 0xC0,        // LD (#C0BB),A        GS warm restart
    0x3A, 0xBB, 0xC0,        // W1: LD A,(#C0BB)
    0x0F,                    // RRCA
    0x38, 0xFA,              // JR C,W1             until the command flag clears
    0x3E, 0x20,              // LD A,#20
    0x32, 0xBB, 0xC0,        // LD (#C0BB),A        total memory
    0x3A, 0xBB, 0xC0,        // W2: LD A,(#C0BB)
    0x0F,                    // RRCA
    0x38, 0xFA,              // JR C,W2
    0x21, 0x00, 0xA0,        // LD HL,#A000
    0x06, 0x03,              // LD B,3
    0x3A, 0xBB, 0xC0,        // R: LD A,(#C0BB)
    0x07,                    // RLCA
    0x30, 0xFA,              // JR NC,R             until the data flag is up
    0x3A, 0xB3, 0xC0,        // LD A,(#C0B3)
    0x77,                    // LD (HL),A
    0x23,                    // INC HL
    0x10, 0xF3,              // DJNZ R
    0x01, 0xFD, 0x1F,        // LD BC,#1FFD
    0x3E, 0x01,              // LD A,#01
    0xED, 0x79,              // OUT (C),A           back to RAM
    0x3E, 0x01,              // LD A,1
    0x32, 0x03, 0xA0,        // LD (#A003),A
    0x18, 0xFE,              // JR $
};
}  // namespace

class SprinterGeneralSound_Test : public ::testing::TestWithParam<GSTypeKind>
{
protected:
    SoundCardScope _gsScope{TestSound::GeneralSound};
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        const GSTypeKind kind = GetParam();
        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-gs", "SPRINTER", 4096, LoggerLevel::LogError, nullptr,
                                                            [kind](CONFIG& config) {
                                                                config.sound.gsTypeKind = kind;
                                                                config.sprinter.fast_start = 1;
                                                            });
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
    }
    void TearDown() override
    {
        _emulator.reset();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
    }
};

// T-ISA-8. Boot-bound: the BIOS sets the machine up (30 frames at fast start) and the card's firmware boots before
// the program runs (the NeoGS loader starts its main ROM from flash)
TEST_P(SprinterGeneralSound_Test, FirmwareAnswersThroughTheAdapter)
{
    _emulator->EnableTurboMode();
    _emulator->Reset();
    _emulator->RunNFrames(30, true);
    GeneralSoundCard* gs = _context->pSoundManager->getGeneralSound();
    ASSERT_NE(gs, nullptr);
    for (int f = 0; f < 200 && !gs->isReadyForCommands(); f++)
        _emulator->RunNFrames(1, true);
    ASSERT_TRUE(gs->isReadyForCommands());

    Memory* memory = _context->pMemory;
    for (size_t i = 0; i < kGsProgram.size(); ++i)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), kGsProgram[i]);
    for (uint16_t a = 0xA000; a < 0xA004; ++a)
        memory->DirectWriteToZ80Memory(a, 0);
    gs->startPortTrace();
    Z80* z80 = _context->pCore->GetZ80();
    z80->pc = 0x8000;
    z80->sp = 0x8FF0;
    z80->halted = 0;
    for (int f = 0; f < 100 && memory->DirectReadFromZ80Memory(0xA003) != 1; f++)
        _emulator->RunNFrames(1, true);
    std::string trace;
    int shown = 0;
    for (const GSTraceEvent& e : gs->getPortTraceEvents())
    {
        if ((e.side == GSTraceSide::Host && !e.isOut() && e.port == 0xBB) || e.side == GSTraceSide::DacFetch || e.side == GSTraceSide::Interrupt || ++shown > 60)
            continue;
        char line[64];
        std::snprintf(line, sizeof line, "%s %s #%02X = #%02X pc #%04X\n", e.side == GSTraceSide::Host ? "host" : "card",
                      e.isOut() ? "out" : "in", e.port, e.value, e.pc);
        trace += line;
    }
    ASSERT_EQ(memory->DirectReadFromZ80Memory(0xA003), 1)
        << "the program did not finish; PC #" << std::hex << z80->pc << ", answer bytes so far "
        << int(memory->DirectReadFromZ80Memory(0xA000)) << " " << int(memory->DirectReadFromZ80Memory(0xA001)) << "\n"
        << trace;

    const uint32_t total = memory->DirectReadFromZ80Memory(0xA000) | memory->DirectReadFromZ80Memory(0xA001) << 8 |
                           static_cast<uint32_t>(memory->DirectReadFromZ80Memory(0xA002)) << 16;
    // The firmware's figure is the RAM its modules may use: the card's RAM less its own system area (gs105a: 128 KB -
    // 16 KB = 114 688; NeoGS v1.11: 2048 KB - 48 KB = 2 048 000)
    const uint32_t ram = static_cast<uint32_t>(gs->getRamSizeKB()) * 1024u;
    EXPECT_EQ(total, GetParam() == GSTypeKind::NGS ? ram - 48u * 1024u : ram - 16u * 1024u)
        << "the firmware's answer through the adapter (card RAM " << gs->getRamSizeKB() << " KB)";
    const SprinterIsaBus::Counters& c = _decoder->GetIsaBus().GetCounters(0);
    EXPECT_GE(c.ioWrites, 2u);
    EXPECT_GE(c.ioReads, 5u);
}

INSTANTIATE_TEST_SUITE_P(Personalities, SprinterGeneralSound_Test, ::testing::Values(GSTypeKind::Z80, GSTypeKind::NGS),
                         [](const ::testing::TestParamInfo<GSTypeKind>& info) {
                             return std::string(info.param == GSTypeKind::NGS ? "NeoGS" : "ClassicGS");
                         });

namespace
{
/// The test MOD (tools/machines/sprinter/test-mod/make-test-mod.py): three notes of a 64-byte sine cycle, 16 rows
/// each at speed 6 / 125 BPM (1.92 s), then 16 rows of silence. Pitch = clock / period / 64
std::vector<uint8_t> TestMod()
{
    std::vector<uint8_t> mod;
    auto text = [&](const char* s, size_t n) {
        for (size_t i = 0; i < n; i++)
            mod.push_back(i < std::strlen(s) ? static_cast<uint8_t>(s[i]) : 0);
    };
    auto be16 = [&](unsigned v) {
        mod.push_back(static_cast<uint8_t>(v >> 8));
        mod.push_back(static_cast<uint8_t>(v));
    };
    text("unreal-ng isa test", 20);
    text("sine 64", 22);
    be16(32);
    mod.push_back(0);
    mod.push_back(64);
    be16(0);
    be16(32);
    for (int s = 0; s < 30; s++)
    {
        text("", 22);
        be16(0);
        mod.push_back(0);
        mod.push_back(0);
        be16(0);
        be16(1);
    }
    mod.push_back(1);
    mod.push_back(127);
    for (int i = 0; i < 128; i++)
        mod.push_back(0);
    text("M.K.", 4);
    static const unsigned kPeriods[3] = {214, 170, 143};
    for (int row = 0; row < 64; row++)
    {
        for (int ch = 0; ch < 4; ch++)
        {
            uint8_t cell[4] = {0, 0, 0, 0};
            if (ch == 0 && row % 16 == 0)
            {
                const int note = row / 16;
                if (note < 3)
                {
                    cell[0] = static_cast<uint8_t>(kPeriods[note] >> 8);
                    cell[1] = static_cast<uint8_t>(kPeriods[note]);
                    cell[2] = 0x10;   // sample 1
                }
                else
                    cell[2] = 0x0C;   // C00: volume 0
            }
            mod.insert(mod.end(), cell, cell + 4);
        }
    }
    for (int i = 0; i < 64; i++)
        mod.push_back(static_cast<uint8_t>(static_cast<int>(std::lround(127 * std::sin(2 * kPi * i / 64))) & 0xFF));
    return mod;
}

/// The power of frequency `f` in pcm[from, from + n) (Goertzel), as a share of the window's AC energy: a pure tone at
/// `f` gives 1
double ToneShare(const std::vector<int16_t>& pcm, size_t from, size_t n, double f, double rate)
{
    double mean = 0;
    for (size_t i = 0; i < n; i++)
        mean += pcm[from + i];
    mean /= static_cast<double>(n);
    double var = 0, s1 = 0, s2 = 0;
    const double c = 2 * std::cos(2 * kPi * f / rate);
    for (size_t i = 0; i < n; i++)
    {
        const double v = pcm[from + i] - mean;
        var += v * v;
        const double s0 = v + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    if (var <= 0)
        return 0;
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 2 * power / (static_cast<double>(n) * var);
}

/// The frequency near `want` (+/- 2 %, 0.05 Hz steps) with the most power in pcm[from, from + n)
double PeakHz(const std::vector<int16_t>& pcm, size_t from, size_t n, double want, double rate)
{
    double best = want, bestShare = -1;
    for (double f = want * 0.98; f <= want * 1.02; f += 0.05)
    {
        const double share = ToneShare(pcm, from, n, f, rate);
        if (share > bestShare)
        {
            bestShare = share;
            best = f;
        }
    }
    return best;
}
/// Which note of the test MOD sounds in each 100 ms window, every 20 ms: 0 / 1 / 2 (C-3 / E-3 / G-3) when its tone
/// carries most of the window's AC energy, -1 silence, -2 neither (a window across a change). `text`: every 5th entry
std::vector<int> NoteTrack(const std::vector<int16_t>& pcm, uint32_t rate, std::string* text)
{
    const double kExpected[3] = {3546895.0 / 214 / 64, 3546895.0 / 170 / 64, 3546895.0 / 143 / 64};
    const size_t step = rate / 50, window = rate / 10;
    std::vector<int> track;
    for (size_t at = 0; at + window <= pcm.size(); at += step)
    {
        double var = 0, mean = 0;
        for (size_t i = 0; i < window; i++)
            mean += pcm[at + i];
        mean /= static_cast<double>(window);
        for (size_t i = 0; i < window; i++)
            var += (pcm[at + i] - mean) * (pcm[at + i] - mean);
        int note = var / static_cast<double>(window) < 100.0 ? -1 : -2;
        for (int n = 0; n < 3 && note == -2; n++)
        {
            if (ToneShare(pcm, at, window, kExpected[n], rate) > 0.5)
                note = n;
        }
        track.push_back(note);
        if (text && track.size() % 5 == 1)
            *text += note == -1 ? "." : note == -2 ? "?" : std::to_string(note);
    }
    return track;
}
}  // namespace

class SprinterProPlay_Test : public SprinterZxSession_Test, public ::testing::WithParamInterface<GSTypeKind>
{
protected:
    SoundCardScope _gsScope{TestSound::GeneralSound};
    void ConfigureMachine(CONFIG& config) override
    {
        config.sound.gsTypeKind = GetParam();
        // The waveform comparison with MAME (open question Q4): MAME's NeoGS flash (v1.10 fix2, neogs110_fix2.rom from
        // the owner's MAME ROM set, not in the repo) in our NeoGS
        if (const char* flash = std::getenv("UNREAL_SPRINTER_NGS_FLASH"))
            std::snprintf(config.ngs.flashPath, sizeof(config.ngs.flashPath), "%s", flash);
    }

    /// The GS row's frames, mono (L + R) / 2, at the core rate
    std::vector<int16_t> CaptureGs(int frames)
    {
        std::vector<int16_t> mono;
        for (int f = 0; f < frames; f++)
        {
            _emulator->RunNFrames(1, true);
            GeneralSoundCard* gs = _context->pSoundManager->getGeneralSound();
            const size_t n = _context->pSoundManager->lastFrameSamples();
            const int16_t* b = gs->getBuffer();
            for (size_t i = 0; i < n; i++)
                mono.push_back(static_cast<int16_t>((static_cast<int>(b[2 * i]) + b[2 * i + 1]) / 2));
        }
        return mono;
    }

    static void SaveWav(const std::string& path, const std::vector<int16_t>& mono, uint32_t rate)
    {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f)
            return;
        const uint32_t data = static_cast<uint32_t>(mono.size() * 2);
        auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
        auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
        std::fwrite("RIFF", 1, 4, f);
        u32(36 + data);
        std::fwrite("WAVEfmt ", 1, 8, f);
        u32(16);
        u16(1);
        u16(1);
        u32(rate);
        u32(rate * 2);
        u16(2);
        u16(16);
        std::fwrite("data", 1, 4, f);
        u32(data);
        std::fwrite(mono.data(), 2, mono.size(), f);
        std::fclose(f);
    }
};

// T-ISA-10 + T-ISA-12. Boot-bound and audio-bound: DSS boots from the hard disk, ProPlay uploads the module, then 9 s of
// the GS row are captured with turbo off (the test asserts on sound) and the TTD session replays
TEST_P(SprinterProPlay_Test, PlaysTheTestModAtItsPitchAndTempo_ReplayMatches)
{
    std::vector<uint8_t> file;
    ASSERT_TRUE(Disk().Read("/DOCS/DISP.TXT", file));
    std::vector<uint8_t> mod = TestMod();
    ASSERT_EQ(mod.size(), 2172u);
    ASSERT_LE(mod.size(), file.size());
    mod.resize(file.size(), 0);   // the directory keeps the file's size: the tail after the sample is zeros
    ASSERT_TRUE(Disk().Overwrite("/DOCS/DISP.TXT", mod));

    BootToPrompt();
    GeneralSoundCard* gs = _context->pSoundManager->getGeneralSound();
    ASSERT_NE(gs, nullptr);
    for (int f = 0; f < 300 && !gs->isReadyForCommands(); f++)
        EmulatorTestHelper::RunFramesFast(_emulator.get(), 1);

    FeatureManager* features = _emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    ttd::TimeTravelController* ttd = _context->pTimeTravelController;
    ASSERT_TRUE(ttd->StartRecording());
    const uint64_t startFrame = Frame();

    Dss("PROPLAY.EXE \\DOCS\\DISP.TXT");
    // ProPlay prints "Done." and returns to DSS (whose prompt then shows C:\BIN>, as on MAME) while the card plays
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Done.") && ScreenHas("C:\\BIN>"); }, 600, 2);
    ASSERT_TRUE(ScreenHas("General Sound found at slot: 0")) << ScreenText();
    ASSERT_TRUE(ScreenHas("Done.") && ScreenHas("C:\\BIN>")) << "ProPlay returned to DSS\n" << ScreenText();
    const uint64_t playFrom = Frame();

    _emulator->DisableTurboMode();
    // 14.3 s at 48.83 frames / s: the rest of the first pass, its silent rows, the whole second pass (7.68 s)
    const int kFrames = 700;
    const std::vector<int16_t> pcm = CaptureGs(kFrames);
    const uint32_t rate = static_cast<uint32_t>(_context->pSoundManager->getCoreRate());
    if (const char* dir = std::getenv("UNREAL_SPRINTER_PROPLAY_WAV"))
        SaveWav(std::string(dir) + "/proplay-" + (GetParam() == GSTypeKind::NGS ? "neogs" : "gs") + ".wav", pcm, rate);
    const StateNode isa = DeviceState::Isa(_context);
    const StateNode isaSlot = isa.find("slots")->items[0];
    EXPECT_GE(isaSlot.find("counters")->find("io_writes")->i, 2000) << "the module went through the adapter";

    // Tempo: which note sounds in each 100 ms window, every 20 ms (a note when its tone carries most of the window's
    // AC energy; -1 silence; -2 neither). The capture starts while the first C-3 plays (ProPlay starts the module just
    // before it prints "Done."), so the onsets are taken from the second pass of the pattern, after the silent rows
    const double kExpected[3] = {3546895.0 / 214 / 64, 3546895.0 / 170 / 64, 3546895.0 / 143 / 64};
    const size_t step = rate / 50;
    std::string trackText;
    const std::vector<int> track = NoteTrack(pcm, rate, &trackText);
    // Event times (track index): the silence after the first pass, then C-3, E-3, G-3 and the next silence. A window
    // across a change holds both sounds ("?"): an event is the first window of the new state after the last clear one
    int silence = -1, starts[3] = {-1, -1, -1}, silenceAgain = -1;
    int before = -2;
    for (size_t i = 0; i < track.size(); before = track[i] != -2 ? track[i] : before, i++)
    {
        const int now = track[i];
        if (now == -2)
            continue;
        if (silence < 0)
        {
            if (now == -1 && before == 2)
                silence = static_cast<int>(i);
            continue;
        }
        for (int n = 0; n < 3; n++)
        {
            if (starts[n] < 0 && now == n && before != n && (n == 0 || starts[n - 1] >= 0))
                starts[n] = static_cast<int>(i);
        }
        if (starts[2] >= 0 && silenceAgain < 0 && now == -1 && before == 2)
            silenceAgain = static_cast<int>(i);
    }
    ASSERT_GE(silence, 0) << trackText;
    for (int n = 0; n < 3; n++)
        ASSERT_GE(starts[n], 0) << "note " << n << "\n" << trackText;
    ASSERT_GE(silenceAgain, 0) << trackText;
    // 16 rows of 6 ticks at 125 BPM: 1.92 s per note and for the silent rows (the GS firmware's tick, one window step)
    // (the silence detector sees a note's end ~40 ms late - the same 1.96 s / 1.86 s split on MAME's NeoGS - so the
    // G-3 and the silent rows are checked together; the whole pattern is 64 rows = 7.68 s)
    EXPECT_NEAR((starts[1] - starts[0]) * 0.02, 1.92, 0.04) << "C-3\n" << trackText;
    EXPECT_NEAR((starts[2] - starts[1]) * 0.02, 1.92, 0.04) << "E-3\n" << trackText;
    EXPECT_NEAR((silenceAgain - starts[2]) * 0.02 + (starts[0] - silence) * 0.02, 3.84, 0.04) << "G-3 + silence\n" << trackText;
    EXPECT_NEAR((silenceAgain - silence) * 0.02, 7.68, 0.02) << "the pattern: 64 rows of 120 ms\n" << trackText;
    // Pitch: a 1 s window inside each note of the second pass, within 0.5 % of clock / period / 64
    for (int n = 0; n < 3; n++)
    {
        const size_t from = static_cast<size_t>(starts[n]) * step + rate / 2;
        ASSERT_LE(from + rate, pcm.size());
        const double f = PeakHz(pcm, from, rate, kExpected[n], rate);
        EXPECT_NEAR(f, kExpected[n], kExpected[n] * 0.005) << "note " << n << " (period " << (n == 0 ? 214 : n == 1 ? 170 : 143) << ")";
    }

    // T-ISA-12: replay the session from its first checkpoint; at the end the GS and ISA blobs equal the live ones
    std::unordered_map<uint8_t, std::vector<uint8_t>> recorded;
    ttd->GetPeripheralRegistry().CaptureAll(recorded);
    const uint64_t endFrame = Frame();
    ttd->StopRecording();
    ASSERT_TRUE(ttd->SeekTo({ttd->GetCheckpoint(0)->time.frame, 0}));
    ASSERT_EQ(Frame(), startFrame);
    _emulator->EnableTurboMode();
    while (Frame() < playFrom)
        _emulator->RunNFrames(1, true);
    _emulator->DisableTurboMode();
    const std::vector<int16_t> replayPcm = CaptureGs(static_cast<int>(endFrame - Frame()));
    if (const char* dir = std::getenv("UNREAL_SPRINTER_PROPLAY_WAV"))
        SaveWav(std::string(dir) + "/proplay-" + (GetParam() == GSTypeKind::NGS ? "neogs" : "gs") + "-replay.wav", replayPcm, rate);
    ASSERT_EQ(Frame(), endFrame);
    std::unordered_map<uint8_t, std::vector<uint8_t>> live;
    ttd->GetPeripheralRegistry().CaptureAll(live);
    // The sound of the replay: the same notes at the same moments, and the same waveform. Not compared sample by sample:
    // the mixer's sample-count accumulator (903 / 904 samples a frame at 44.1 kHz) and the GS output stage's resampler
    // are host-side rendering state, not in the checkpoints, so the replayed stream sits a fraction of a sample apart
    // and starts from its own output level (the GS output stage resumes relative to its level at the restore)
    const std::vector<int> replayed = NoteTrack(replayPcm, rate, nullptr);
    ASSERT_EQ(replayed.size(), track.size());
    size_t clearDiffer = 0;
    for (size_t i = 0; i < track.size(); i++)
        clearDiffer += track[i] >= -1 && replayed[i] >= -1 && track[i] != replayed[i];
    EXPECT_EQ(clearDiffer, 0u) << "the replay plays the same notes at the same moments";
    {
        // Pearson correlation over the whole capture, best lag within +/- 2 samples
        double best = -2;
        for (int lag = -2; lag <= 2; lag++)
        {
            double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
            size_t n = 0;
            for (size_t i = 8; i + 8 < pcm.size() && i + 8 < replayPcm.size(); i++, n++)
            {
                const double a = pcm[i + lag], b = replayPcm[i];
                sa += a;
                sb += b;
                saa += a * a;
                sbb += b * b;
                sab += a * b;
            }
            const double cov = sab - sa * sb / n, va = saa - sa * sa / n, vb = sbb - sb * sb / n;
            best = std::max(best, cov / std::sqrt(va * vb));
        }
        EXPECT_GT(best, 0.995) << "the replayed waveform";
    }
    if (GetParam() == GSTypeKind::NGS)
    {
        // The NeoGS blob (id 12) leaves the card's RAM and flash out until the TTD v2 memory regions (neogs-tdd §7.4):
        // a restore keeps the live card RAM - here with the module and the firmware's variables of the end of the
        // recording - so the firmware runs a few T-states differently and the card's registers are not bit-exact
        return;
    }
    // The classic GS carries its RAM in its blob (id 5): the machine replays exactly - the card and the ISA bus
    for (ttd::PeripheralId id : {ttd::PeripheralId::GeneralSound, ttd::PeripheralId::SprinterIsa})
    {
        const uint8_t key = static_cast<uint8_t>(id);
        ASSERT_EQ(live.count(key), 1u) << int(key);
        ASSERT_EQ(recorded.count(key), 1u) << int(key);
        const std::vector<uint8_t> x = ttd::TTDPeripheralRegistry::DecodeBlob(key, live[key]);
        const std::vector<uint8_t> y = ttd::TTDPeripheralRegistry::DecodeBlob(key, recorded[key]);
        ASSERT_EQ(x.size(), y.size()) << int(key);
        std::string diffs;
        size_t count = 0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            if (x[i] != y[i] && count++ < 16)
                diffs += " @" + std::to_string(i) + ":" + std::to_string(y[i]) + "->" + std::to_string(x[i]);
        }
        EXPECT_EQ(count, 0u) << "device " << int(key) << " differs after the replay (recorded->replayed)" << diffs;
    }
}

INSTANTIATE_TEST_SUITE_P(Personalities, SprinterProPlay_Test, ::testing::Values(GSTypeKind::Z80, GSTypeKind::NGS),
                         [](const ::testing::TestParamInfo<GSTypeKind>& info) {
                             return std::string(info.param == GSTypeKind::NGS ? "NeoGS" : "ClassicGS");
                         });
