#ifdef UNREALNG_HAVE_OPL4

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_moonsound.h"
#include "emulator/sound/soundmanager.h"

/// Guest-level per-melody re-verification of the MFM Music sample disk
/// (testdata/sound/moonsound/mfm_sample.trd, "MFM Music sample 1" per
/// SOURCES.md: 2 tunes, Space or auto-advance).
///
/// Symptom report (2026-09-15): FM plays at very low amplitude compared to
/// PCM, hisses, and drops notes in some modes. Measured verdict: on the
/// pre-adoption in-tree FM map both tunes rendered as white noise at
/// 1/7-1/23 of the classic-map amplitude (melody1 fmRms 66-352 vs 2635,
/// HF ratio 1.40 vs 0.19 - white noise is sqrt(2) - and melody2 likewise),
/// while the guest side was byte-perfect: identical card traffic, identical
/// MBPlayer position advancement on both backends. The melodies are "MB FOR
/// MOONSOUND FM" tunes - FM-only, PCM legitimately silent - so the
/// low-amplitude-vs-PCM symptom was this FM rendering, not a mix imbalance.
/// Root cause: the three classic-map divergences
/// (opl4-ymfm-verification-findings.md section 2.1) - the player (author's
/// mfm_player.asm, MBPlayer) writes the classic YMF262 operator map
/// register stream (0x20/0x23, 0x28/0x2B ... 0xC0-0xC5), which the in-tree
/// linear map misrouted (envelope/TL writes land on the wrong operators,
/// inverted 0xC0 routing, never-attacking unconfigured carriers).
/// RESOLVED the same day: the in-tree engine adopted the classic
/// YMF262/silicon semantics (slot-indexed operator storage, CHA/CHB
/// include routing, unconfigured-carrier attack, ymfm KSL consumption),
/// verified accommodation-free against ymfm by co-simulation - both
/// backends now render both tunes healthy (flip recorded in the findings
/// doc, section 5).
///
/// Each melody gets a dedicated test that plays it through the real guest
/// player code (harness-staged launch, see StagePlayer) and measures the
/// rendered MoonSound FM/PCM device buffers at 1x speed (turbo is used only
/// for launch/settle windows; audio buffers are invalid under turbo's muted
/// low-quality path):
///   - FM/PCM RMS balance (the "very low amplitude" symptom),
///   - FM high-frequency character: first-difference RMS / signal RMS and
///     zero-crossing density (the "hissing" symptom),
///   - per-channel FM activity (the "missing notes" symptom).
///
/// Following the section-2.1 convention ("the finding is the report, not
/// the failure"): the advancement/mechanics fences hold on every backend,
/// and since the classic-map adoption the symptom fences hold at the
/// healthy reference values on every backend too (previously they held
/// only on the ymfm build while the in-tree build pinned the broken
/// state).
///
/// The fixtures are persisted for format-level work at
/// testdata/sound/moonsound/mfm-sample/ (bcareful.mfm, melodies.mfm and the
/// demo binary moonsound-demo.bin - a copy of the corpus moonsound.bin).
class MoonSoundMfmGuest_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();

        // Same staging as the demo-guest suite (MoonSound=1, the idle TSFM
        // pair dropped to AY). The demo targets DEVICE ZXSPECTRUM128; the
        // Pentagon is the suite's canonical 128K machine. The launch is
        // harness-staged (StagePlayer), so the TR-DOS loader - which stalls
        // section-12.8-style at the double-sided track boundary on every
        // model the harness can build - is bypassed entirely. Sound
        // generation stays ON - these tests assert on the rendered device
        // buffers.
        _emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::AY, LoggerLevel::LogError);
        if (_emulator)
        {
            _context = _emulator->GetContext();

            _context->config.reset_rom = RM_SOS;
            _emulator->Reset();
            if (FeatureManager* features = _context->pFeatureManager)
            {
                features->setFeature(Features::kScreenHQ, false);
                features->setFeature(Features::kSoundHQ, false);
            }

            // Boot-bound only: the measurement pass disables turbo before
            // touching the audio buffers
            _emulator->EnableTurboMode();
        }
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            _emulator->Release();
            delete _emulator;
            _emulator = nullptr;
        }
        MessageCenter::DisposeDefaultMessageCenter();
    }

    std::string ocrCurrentScreen() const
    {
        Memory* memory = _emulator->GetMemory();
        std::string screen;
        screen.reserve(24 * (32 + 1));
        for (int row = 0; row < 24; row++)
        {
            for (int col = 0; col < 32; col++)
            {
                screen += ScreenOCR::ocrCell(memory, row, col);
            }
            screen += '\n';
        }
        return screen;
    }

    MainLoop_CUT* MainLoop()
    {
        return reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);
    }

    /// Card bus traffic counters, fed from the CPU trace hook
    struct CardStats
    {
        int fmOuts = 0;     // per frame, OUTs with low byte #C4..#C7
        int waveOuts = 0;   // per frame, OUTs with low byte #7E/#7F
        int fmTotal = 0;
        int waveTotal = 0;

        void ResetFrame()
        {
            fmOuts = 0;
            waveOuts = 0;
        }
    };

    CardStats _stats;
    int _frame = 0;

    /// Reads a guest byte (fixed $8xxx window - always RAM page 2)
    uint8_t GuestPeek(uint16_t address) const
    {
        return _context->pMemory->DirectReadFromZ80Memory(address);
    }

    /// MBPlayer_play's position variable (mfm_player.asm): the tune's
    /// position index, advanced by the IM2 handler every 16 steps
    static constexpr uint16_t kPlayerPosAddress = 0x8A67;

    /// Rendered-audio measurements over a playback window at 1x speed
    struct PlaybackMetrics
    {
        int frames = 0;
        double fmRms = 0.0;    // RMS of the FM source buffer across the window
        int fmPeak = 0;
        double pcmRms = 0.0;   // RMS of the PCM source buffer
        int pcmPeak = 0;
        double fmDc = 0.0;     // mean DC offset of the FM buffer
        double fmHfRatio = 0.0; // first-difference RMS / signal RMS: ~0.1-0.3 for tonal music, ~1.4 for white hiss
        double fmZcRate = 0.0; // mean zero crossings per sample
        int fmActiveFrames = 0;   // frames with FM register traffic
        int waveActiveFrames = 0; // frames with wave/PCM register traffic
        int fmPortWrites = 0;   // FM register writes issued inside the window
        int wavePortWrites = 0; // wave/PCM register writes inside the window
        int startPos = 0;       // MBPlayer position at window start/end - the
        int endPos = 0;         // tune must advance (or wrap) through it
        float fmChannelPeak[18] = {};
        float pcmChannelPeak[24] = {};
        int fmChannelsAudible = 0;  // FM channels over the audibility floor
        int pcmChannelsAudible = 0; // PCM slots over the audibility floor

        double BalanceDb() const
        {
            return 20.0 * std::log10((fmRms + 1e-30) / (pcmRms + 1e-30));
        }
    };

    static constexpr float kChannelAudibleFloor = 0.02f; // same floor the PoC tap tests use

    /// Runs `frames` frames at 1x speed (turbo off - audio buffers are only
    /// valid unmuted) and accumulates the rendered-audio metrics. Register
    /// traffic is event-sparse (MBPlayer only writes on note triggers, every
    /// `speed` frames), so the activity fences below count writes, not
    /// active frames.
    PlaybackMetrics MeasurePlayback(int frames, SoundChip_Moonsound* moonsound)
    {
        _emulator->DisableTurboMode();
        PlaybackMetrics m;
        m.frames = frames;
        const int fmTotalStart = _stats.fmTotal;
        const int waveTotalStart = _stats.waveTotal;
        m.startPos = GuestPeek(kPlayerPosAddress);
        double fmEnergy = 0.0;
        double pcmEnergy = 0.0;
        double fmSum = 0.0;
        double fmDiffEnergy = 0.0;
        long long fmSamples = 0;
        long long fmZeroCrossings = 0;
        int prev = 0;

        for (int i = 0; i < frames; i++)
        {
            _stats.ResetFrame();
            MainLoop()->RunFrame();
            _frame++;

            if (_stats.fmOuts > 0)
            {
                m.fmActiveFrames++;
            }
            if (_stats.waveOuts > 0)
            {
                m.waveActiveFrames++;
            }

            const int16_t* fm = moonsound->getFmBuffer();
            const int16_t* pcm = moonsound->getPcmBuffer();
            for (int s = 0; s < SAMPLES_PER_FRAME; s++)
            {
                const double v = fm[s];
                fmEnergy += v * v;
                fmSum += v;
                const double d = (s == 0) ? (v - prev) : (v - fm[s - 1]);
                fmDiffEnergy += d * d;
                const int absV = v < 0 ? -static_cast<int>(v) : static_cast<int>(v);
                if (absV > m.fmPeak)
                {
                    m.fmPeak = absV;
                }
                const double p = pcm[s];
                pcmEnergy += p * p;
                const int absP = p < 0 ? -static_cast<int>(p) : static_cast<int>(p);
                if (absP > m.pcmPeak)
                {
                    m.pcmPeak = absP;
                }
                if (s > 0 && ((fm[s - 1] < 0) != (v < 0)))
                {
                    fmZeroCrossings++;
                }
            }
            prev = fm[SAMPLES_PER_FRAME - 1];
            fmSamples += SAMPLES_PER_FRAME;

            for (int ch = 0; ch < 18; ch++)
            {
                const float peak = moonsound->channelPeak(opl4::ChannelGroup::Fm, static_cast<size_t>(ch));
                if (peak > m.fmChannelPeak[ch])
                {
                    m.fmChannelPeak[ch] = peak;
                }
            }
            for (int ch = 0; ch < 24; ch++)
            {
                const float peak = moonsound->channelPeak(opl4::ChannelGroup::Pcm, static_cast<size_t>(ch));
                if (peak > m.pcmChannelPeak[ch])
                {
                    m.pcmChannelPeak[ch] = peak;
                }
            }
        }

        m.fmRms = std::sqrt(fmEnergy / static_cast<double>(fmSamples));
        m.pcmRms = std::sqrt(pcmEnergy / static_cast<double>(fmSamples));
        m.fmDc = fmSum / static_cast<double>(fmSamples);
        m.fmHfRatio = std::sqrt(fmDiffEnergy / (fmEnergy + 1e-30));
        m.fmZcRate = static_cast<double>(fmZeroCrossings) / static_cast<double>(fmSamples);
        m.fmPortWrites = _stats.fmTotal - fmTotalStart;
        m.wavePortWrites = _stats.waveTotal - waveTotalStart;
        m.endPos = GuestPeek(kPlayerPosAddress);
        for (int ch = 0; ch < 18; ch++)
        {
            if (m.fmChannelPeak[ch] > kChannelAudibleFloor)
            {
                m.fmChannelsAudible++;
            }
        }
        for (int ch = 0; ch < 24; ch++)
        {
            if (m.pcmChannelPeak[ch] > kChannelAudibleFloor)
            {
                m.pcmChannelsAudible++;
            }
        }
        return m;
    }

    void PrintMetrics(const char* melody, const PlaybackMetrics& m) const
    {
        std::cout << "[metrics:" << melody << "] frames=" << m.frames
                  << " fmRms=" << m.fmRms << " fmPeak=" << m.fmPeak
                  << " pcmRms=" << m.pcmRms << " pcmPeak=" << m.pcmPeak
                  << " balance=" << m.BalanceDb() << " dB (FM vs PCM)\n"
                  << "[metrics:" << melody << "] fmDc=" << m.fmDc
                  << " fmHfRatio=" << m.fmHfRatio << " fmZcRate=" << m.fmZcRate
                  << " fmPortWrites=" << m.fmPortWrites << " wavePortWrites=" << m.wavePortWrites
                  << " fmActiveFrames=" << m.fmActiveFrames << "/" << m.frames
                  << " waveActiveFrames=" << m.waveActiveFrames << "/" << m.frames
                  << " playerPos=" << m.startPos << "->" << m.endPos << "\n"
                  << "[metrics:" << melody << "] fmChannelsAudible=" << m.fmChannelsAudible
                  << " peaks:";
        for (int ch = 0; ch < 18; ch++)
        {
            char buf[24];
            snprintf(buf, sizeof(buf), " %.2f", m.fmChannelPeak[ch]);
            std::cout << buf;
        }
        std::cout << "\n[metrics:" << melody << "] pcmChannelsAudible=" << m.pcmChannelsAudible
                  << " peaks:";
        for (int ch = 0; ch < 24; ch++)
        {
            char buf[24];
            snprintf(buf, sizeof(buf), " %.2f", m.pcmChannelPeak[ch]);
            std::cout << buf;
        }
        std::cout << "\n";
    }

    /// Plain turbo frames (settle/wait windows)
    void RunTurboFrames(int frames)
    {
        for (int i = 0; i < frames; i++)
        {
            _stats.ResetFrame();
            MainLoop()->RunFrame();
            _frame++;
        }
    }

    /// The demo's melody-advance key: port #7FFE bit 0 (moonsound_demo.asm
    /// MoonSound_loop reads it with `in a,(#FE)` / A=#7F). Caps Shift
    /// (#FEFE bit 0) exits to TR-DOS - never pressed here.
    void PressMelodyKey(int frames)
    {
        Keyboard* keyboard = _context->pKeyboard;
        ASSERT_NE(keyboard, nullptr);
        keyboard->PressKey(ZXKEY_SPACE);
        RunTurboFrames(frames);
        keyboard->ReleaseKey(ZXKEY_SPACE);
    }

    /// Loads a binary fixture from testdata (the suite-wide GetFileSize /
    /// ReadFileToBuffer pattern). The expected size doubles as an integrity
    /// check: these are fixed corpus builds. Returns false (with an
    /// ADD_FAILURE) on any miss.
    bool LoadFixture(const std::string& relativePath, size_t expectedSize, const char* what, std::vector<uint8_t>& out)
    {
        const std::string path = TestPathHelper::GetTestDataPath(relativePath);
        if (!FileHelper::FileExists(path))
        {
            ADD_FAILURE() << "missing fixture: " << path;
            return false;
        }
        const size_t size = FileHelper::GetFileSize(path);
        if (size != expectedSize)
        {
            ADD_FAILURE() << what << ": expected " << expectedSize << " bytes, got " << size << " (" << path << ")";
            return false;
        }
        out.assign(size, 0);
        if (size > 0 && FileHelper::ReadFileToBuffer(path, out.data(), size) != size)
        {
            ADD_FAILURE() << what << ": short read: " << path;
            return false;
        }
        return true;
    }

    /// Host-side memcpy into guest Z80 space, split at 16K window boundaries
    /// (every window maps exactly one physical page contiguously)
    void HostCopyToZ80(uint16_t address, const uint8_t* src, size_t length)
    {
        Memory* memory = _context->pMemory;
        while (length > 0)
        {
            uint8_t* host = memory->MapZ80AddressToPhysicalAddress(address);
            const size_t windowLeft = 0x4000 - (address & 0x3FFF);
            const size_t chunk = length < windowLeft ? length : windowLeft;
            memcpy(host, src, chunk);
            address = static_cast<uint16_t>(address + chunk);
            src += chunk;
            length -= chunk;
        }
    }

    /// Stages the MFM sample demo's post-load state and launches the player
    /// (melody 0 starts by itself: the entry falls into
    /// MoonSound_next_music, which copies the tune, runs mwmload +
    /// MBPlayer_init and lets the IM2 handler drive the card every frame).
    /// Returns the chip, or nullptr on a failed launch - the TEST_F-level
    /// ASSERT on the result makes that a failure; diagnostics land here.
    ///
    /// The staging reproduces the demo's own boot contract (shipped
    /// moonsound.bin + moonsound_demo.asm):
    ///   - the player binary is copied to $6200 and entered at $6258 - the
    ///     post-load segment (right after the two melody disk loads) that
    ///     re-programs SP/#7FFD/IM2 itself;
    ///   - melody 0/1 are staged as raw .MFM files at offset 0 of RAM pages
    ///     1/3, where the melody table at $6315 (pages 0x11/0x13, $C000,
    ///     lengths 0x1CC0/0x2240) points the 256-byte block copy loop;
    ///   - no SRAM upload: nothing on the disk generates wave-port upload
    ///     traffic (the only #7E/#7F writers are MBPlayer's runtime register
    ///     writes), so the PCM side plays from the card's built-in 2 MB
    ///     sample ROM. MFMSampl.B is an optional RAM bank this demo never
    ///     loads.
    SoundChip_Moonsound* StagePlayer()
    {
        SoundManager* soundManager = _context->pSoundManager;
        if (soundManager == nullptr || !soundManager->hasMoonSound())
        {
            ADD_FAILURE() << "staged config must construct the MoonSound card";
            return nullptr;
        }
        SoundChip_Moonsound* moonsound = soundManager->getMoonSound();
        if (moonsound == nullptr)
        {
            ADD_FAILURE() << "card enabled but chip missing";
            return nullptr;
        }
        if (moonsound->waveRomLoadedBytes() < 0x100000)
        {
            ADD_FAILURE() << "MoonSound sample ROM not loaded - the melodies' PCM side would be silent";
            return nullptr;
        }

        // STEP 1: fixtures. The entry-pattern check (ld sp,#5FFF at file
        // offset $58 = Z80 $6258) guards against staging an unexpected build.
        std::vector<uint8_t> demo;
        std::vector<uint8_t> melody1;
        std::vector<uint8_t> melody2;
        if (!LoadFixture("sound/moonsound/mfm-sample/moonsound-demo.bin", 20924, "demo binary", demo) ||
            !LoadFixture("sound/moonsound/mfm-sample/bcareful.mfm", 7349, "melody 1", melody1) ||
            !LoadFixture("sound/moonsound/mfm-sample/melodies.mfm", 8762, "melody 2", melody2))
        {
            return nullptr;
        }
        if (demo[0x58] != 0x31 || demo[0x59] != 0xFF || demo[0x5A] != 0x5F || demo[0x5B] != 0x3E)
        {
            ADD_FAILURE() << "demo binary is not the expected build: entry pattern at $6258 mismatch";
            return nullptr;
        }
        if (melody1.size() > 0x1CC0 || melody2.size() > 0x2240)
        {
            ADD_FAILURE() << "melody exceeds its melody-table length";
            return nullptr;
        }

        // STEP 2: guest memory. Binary to $6200, melodies to RAM pages 1/3
        // (page offset 0 - #7FFD 0x11/0x13 map them at $C000 for the copy
        // loop), screen area cleared so the crawl starts from a clean
        // display file (the original MoonSound_Start zeroes it too).
        Memory* memory = _context->pMemory;
        HostCopyToZ80(0x6200, demo.data(), demo.size());
        const std::vector<uint8_t> zeros(6912, 0);
        HostCopyToZ80(0x4000, zeros.data(), zeros.size());
        memset(memory->RAMPageAddress(1), 0, 0x4000);
        memset(memory->RAMPageAddress(3), 0, 0x4000);
        memcpy(memory->RAMPageAddress(1), melody1.data(), melody1.size());
        memcpy(memory->RAMPageAddress(3), melody2.data(), melody2.size());

        // STEP 3: card-traffic hook (per-frame and cumulative counts).
        // Low-byte matching covers both OUT forms; #C4-#C7 never collide
        // with the FDC mirror ports the way #7E/#7F can during disk access
        // (there is none after staging).
        Z80* cpu = _context->pCore->GetZ80();
        if (cpu == nullptr)
        {
            ADD_FAILURE() << "no CPU";
            return nullptr;
        }
        cpu->busTraceHook = [this](char type, uint16_t port, uint8_t value)
        {
            if (type == 'O')
            {
                const uint16_t low = static_cast<uint16_t>(port & 0x00FF);
                if (low >= SoundChip_Moonsound::PORT_FM_ADDR1 && low <= SoundChip_Moonsound::PORT_FM_DATA2)
                {
                    _stats.fmOuts++;
                    _stats.fmTotal++;
                }
                else if (low == SoundChip_Moonsound::PORT_WAVE_ADDR || low == SoundChip_Moonsound::PORT_WAVE_DATA)
                {
                    _stats.waveOuts++;
                    _stats.waveTotal++;
                }
            }
        };

        // STEP 4: launch. Interrupts are masked until the entry installs
        // its IM2 vectors (the window between the PC handoff and `im 2`
        // would otherwise risk a ROM-mode INT), then turbo frames until the
        // player drives the FM side of the card.
        cpu->iff1 = 0;
        cpu->iff2 = 0;
        cpu->halted = 0;
        cpu->pc = 0x6258;

        int launchFrames = 0;
        for (int i = 0; i < 600 && _stats.fmTotal < 300; i++)
        {
            _stats.ResetFrame();
            MainLoop()->RunFrame();
            _frame++;
            launchFrames++;
        }
        std::cout << "[stage] frames=" << launchFrames << " fmTotal=" << _stats.fmTotal
                  << " waveTotal=" << _stats.waveTotal
                  << " waveRom=" << moonsound->waveRomLoadedBytes() << "\n";
        if (_stats.fmTotal < 300)
        {
            ADD_FAILURE() << "player never drove the FM side of the card. Screen:\n"
                          << ocrCurrentScreen();
            return nullptr;
        }
        return moonsound;
    }

    /// Playback mechanics - must hold on every FM backend: the guest keeps
    /// playing (MBPlayer advances its position through the window; the
    /// window is longer than one position at either tune's speed, so a
    /// healthy crossing is guaranteed) and keeps driving both halves of the
    /// card (register traffic is event-sparse: MBPlayer writes only on note
    /// triggers, so a handful of writes per window is full health).
    void AssertMelodyAdvances(const PlaybackMetrics& m, const char* melody)
    {
        EXPECT_GT(m.fmPortWrites, 0) << melody << ": FM register writes stopped";
        EXPECT_GT(m.wavePortWrites, 0) << melody << ": wave-side register writes stopped";
        EXPECT_NE(m.endPos, m.startPos) << melody << ": MBPlayer position froze at " << m.startPos;
    }

    /// Rendered-symptom fences. Since the classic-map adoption (2026-09-15)
    /// every backend must render the tunes healthy: loud against the
    /// (legitimately silent - these are FM-only "MB FOR MOONSOUND FM"
    /// tunes) PCM side, tonal rather than hiss-like. The per-channel
    /// "missing notes" diagnostic stays a printout metric: the FM
    /// channelPeak taps are in-tree instrumentation and report zero on the
    /// ymfm wrapper (measured 2026-09-15: fmRms 2635 with 18 zero peaks),
    /// so it cannot be fenced cross-backend.
    void AssertRenderedMelody(const PlaybackMetrics& m, const char* melody)
    {
        // Classic-map reference values (measured 2026-09-15 on the ymfm
        // backend; the in-tree engine holds the same fences after the
        // adoption): melody1 fmRms 2635 / HF 0.19 / ZC 0.031, melody2 fmRms
        // 1514 / HF 0.74 / ZC 0.174 - the brighter melody2 numbers reflect
        // its percussive content, both well clear of white noise (1.41 / 0.5).
        EXPECT_GT(m.fmRms, 500.0) << melody << ": FM buffer near-silent";
        EXPECT_LT(m.fmHfRatio, 1.0) << melody << ": FM high-frequency character is hiss-like";
        EXPECT_LT(m.fmZcRate, 0.30) << melody << ": FM zero-crossing density is hiss-like";
    }
};

/// Melody 1 - BCAREFUL.MFM (page 0x11, length 0x1CC0): starts by itself
/// after the staged launch. Fences the reported FM-under-PCM / hiss /
/// missing-notes symptoms on the tune the disk opens with (PCM is
/// legitimately silent - see AssertRenderedMelody).
TEST_F(MoonSoundMfmGuest_Test, MfmSample_Melody1_BCareful_RendersHealthyFm)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    SoundChip_Moonsound* moonsound = StagePlayer();
    ASSERT_NE(moonsound, nullptr);

    // Let the tune get past its intro ramp before sampling
    RunTurboFrames(90);

    const PlaybackMetrics m = MeasurePlayback(150, moonsound);
    PrintMetrics("melody1-bcareful", m);

    _context->pCore->GetZ80()->busTraceHook = nullptr;
    AssertMelodyAdvances(m, "melody1-bcareful");
    AssertRenderedMelody(m, "melody1-bcareful");
}

/// Melody 2 - MELODIES.MFM (page 0x13, length 0x2240): selected with the
/// demo's Space binding (port #7FFE bit 0) while melody 1 is playing. Same
/// fences on the second tune (PCM legitimately silent - see
/// AssertRenderedMelody).
TEST_F(MoonSoundMfmGuest_Test, MfmSample_Melody2_Melodies_RendersHealthyFm)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    SoundChip_Moonsound* moonsound = StagePlayer();
    ASSERT_NE(moonsound, nullptr);

    // Confirm melody 1 is rolling, then advance to melody 2
    const int fmBefore = _stats.fmTotal;
    RunTurboFrames(30);
    ASSERT_GT(_stats.fmTotal, fmBefore) << "melody 1 not driving the card";

    PressMelodyKey(6);
    RunTurboFrames(120); // let the new tune ramp

    const PlaybackMetrics m = MeasurePlayback(150, moonsound);
    PrintMetrics("melody2-melodies", m);

    _context->pCore->GetZ80()->busTraceHook = nullptr;
    AssertMelodyAdvances(m, "melody2-melodies");
    AssertRenderedMelody(m, "melody2-melodies");
}

#endif // UNREALNG_HAVE_OPL4
