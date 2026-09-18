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
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_moonsound.h"
#include "emulator/sound/soundmanager.h"

/// Guest-level verification of "MFM Music sample 2" (testdata/sound/moonsound/
/// mfm_sample_2.trd, 15 melodies per SOURCES.md; fixtures staged at
/// testdata/sound/moonsound/mfm-sample-2/ from the author's build tree).
///
/// Symptom report (2026-09-17): melody 7 (CRYOGENT.MFM - the first tune on
/// melody page 0x13 in MoonSound_tabl_music, reached with six Space presses)
/// plays only some channels; the rest render as noise.
///
/// This suite reproduces the full guest path: the shipped moonsound.bin is
/// staged at $6200 with the five melody pages (0x11/0x13/0x14/0x16/0x17)
/// staged from the author's .MFM files exactly as the disk's own TR-DOS loads
/// place them (MoonSound_tabl_music offsets), launched at the post-load entry
/// $6297 (ld sp,#5FFF segment of this build), melody 1 starts by itself, and
/// Space advances to melody 7. All rendered-audio metrics consume the
/// registry buffers as what they are - interleaved stereo - by mono-summing
/// each L/R pair; reading the pair stream linearly as mono fakes a sqrt(2)
/// first-difference "noise" on any channel routed to one side only (C0 out
/// bits). Diagnostics beyond the sample-1 metrics:
///   - a reconstructed FM register snapshot (the card's #C4/#C6 latch model)
///     over the measurement window, printed as a per-channel operator table -
///     the "which operators are involved" evidence;
///   - a per-channel isolation sweep (mute every FM channel but one, measure
///     that channel's rendered HF character) - in-tree backend only, the
///     ymfm adapter passes the pre-mixed pair through unmuted.
class MoonSoundMfm2Guest_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();

        // Same staging contract as the sample-1 suite (MoonSound=1, idle
        // TSFM pair dropped to AY, Pentagon 128K, harness-staged launch).
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

    MainLoop_CUT* MainLoop()
    {
        return reinterpret_cast<MainLoop_CUT*>(_context->pMainLoop);
    }

    /// Demo state variables (moonsound_demo.lst): the melody counter the
    /// Space handler increments and the on-screen number derived from it.
    static constexpr uint16_t kCountMusicAddress = 0x654D;

    uint8_t GuestPeek(uint16_t address) const
    {
        return _context->pMemory->DirectReadFromZ80Memory(address);
    }

    // region <Card traffic + FM register reconstruction>

    int _fmOuts = 0;   // per frame
    int _waveOuts = 0; // per frame
    int _fmTotal = 0;
    int _waveTotal = 0;
    int _frame = 0;

    /// Reconstructed FM register writes: the card's own latch model
    /// (#C4/#C6 latch the bank's address, #C5/#C7 carry the data - the same
    /// decode portDeviceOutMethod applies), so this stream is exactly what
    /// the engine receives.
    struct FmWrite
    {
        int frame;
        int bank;
        uint8_t reg;
        uint8_t data;
    };
    std::vector<FmWrite> _fmWrites;
    bool _captureFm = false;
    int _captureStartFrame = 0;
    uint8_t _fmLatch[2] = {};
    /// Persistent register shadow over the whole session (patch writes land
    /// at note-on time, long before any measurement window)
    uint8_t _captureRegs[512] = {};
    /// Data-write routing evidence: which data port carried each write,
    /// keyed by the most recent ADDRESS port (#C4 vs #C6) - the card-decode
    /// question (bank latched by address port vs keyed by data port)
    int _lastAddrPort = 0; // 0 = #C4, 1 = #C6
    int _dataVia[2][2] = {}; // [addrPort][dataPort: 0=#C5, 1=#C7]

    void ResetFrameStats()
    {
        _fmOuts = 0;
        _waveOuts = 0;
    }

    void InstallTraceHook()
    {
        _context->pCore->GetZ80()->busTraceHook = [this](char type, uint16_t port, uint8_t value)
        {
            if (type != 'O')
                return;
            const uint16_t low = static_cast<uint16_t>(port & 0x00FF);
            if (low >= SoundChip_Moonsound::PORT_FM_ADDR1 && low <= SoundChip_Moonsound::PORT_FM_DATA2)
            {
                _fmOuts++;
                _fmTotal++;
                if (low == SoundChip_Moonsound::PORT_FM_ADDR1)
                {
                    _fmLatch[0] = value;
                    _lastAddrPort = 0;
                }
                else if (low == SoundChip_Moonsound::PORT_FM_ADDR2)
                {
                    _fmLatch[1] = value;
                    _lastAddrPort = 1;
                }
                else
                {
                    const int dataPort = low == SoundChip_Moonsound::PORT_FM_DATA1 ? 0 : 1;
                    _dataVia[_lastAddrPort][dataPort]++;
                    const int bank = _lastAddrPort;
                    const uint8_t reg = _fmLatch[bank];
                    _captureRegs[bank * 256 + reg] = value;
                    if (_captureFm)
                        _fmWrites.push_back({_frame, bank, reg, value});
                }
            }
            else if (low == SoundChip_Moonsound::PORT_WAVE_ADDR || low == SoundChip_Moonsound::PORT_WAVE_DATA)
            {
                _waveOuts++;
                _waveTotal++;
            }
        };
    }

    void RunFrames(int frames)
    {
        for (int i = 0; i < frames; i++)
        {
            ResetFrameStats();
            MainLoop()->RunFrame();
            _frame++;
        }
    }

    // endregion

    // region <Rendered-audio measurement>

    struct PlaybackMetrics
    {
        int frames = 0;
        double fmRms = 0.0;
        int fmPeak = 0;
        double pcmRms = 0.0;
        double fmHfRatio = 0.0;
        double fmZcRate = 0.0;
        int fmPortWrites = 0;
        int wavePortWrites = 0;
        float fmChannelPeak[18] = {};
        int fmChannelsAudible = 0;
    };

    static constexpr float kChannelAudibleFloor = 0.02f;

    PlaybackMetrics MeasurePlayback(int frames, SoundChip_Moonsound* moonsound)
    {
        _emulator->DisableTurboMode();
        PlaybackMetrics m;
        m.frames = frames;
        const int fmTotalStart = _fmTotal;
        const int waveTotalStart = _waveTotal;
        double fmEnergy = 0.0;
        double pcmEnergy = 0.0;
        double fmDiffEnergy = 0.0;
        long long fmSamples = 0;
        long long fmZeroCrossings = 0;

        for (int i = 0; i < frames; i++)
        {
            ResetFrameStats();
            MainLoop()->RunFrame();
            _frame++;

            const int16_t* fm = moonsound->getFmBuffer();
            const int16_t* pcm = moonsound->getPcmBuffer();
            double lastV = 0.0;
            for (int s = 0; s < SAMPLES_PER_FRAME; s++)
            {
                // Interleaved stereo: mono-sum the L/R pair (see the class
                // comment) so side-routed channels measure as what they are.
                const double v = (static_cast<double>(fm[2 * s]) + fm[2 * s + 1]) * 0.5;
                fmEnergy += v * v;
                const double d = (s == 0) ? 0.0 : (v - lastV);
                fmDiffEnergy += d * d;
                const int absV = v < 0 ? -static_cast<int>(v) : static_cast<int>(v);
                if (absV > m.fmPeak)
                    m.fmPeak = absV;
                if (s > 0 && ((lastV < 0) != (v < 0)))
                    fmZeroCrossings++;
                lastV = v;
                const double p = (static_cast<double>(pcm[2 * s]) + pcm[2 * s + 1]) * 0.5;
                pcmEnergy += p * p;
            }
            fmSamples += SAMPLES_PER_FRAME;

            for (int ch = 0; ch < 18; ch++)
            {
                const float peak = moonsound->channelPeak(opl4::ChannelGroup::Fm, static_cast<size_t>(ch));
                if (peak > m.fmChannelPeak[ch])
                    m.fmChannelPeak[ch] = peak;
            }
        }

        m.fmRms = std::sqrt(fmEnergy / static_cast<double>(fmSamples));
        m.pcmRms = std::sqrt(pcmEnergy / static_cast<double>(fmSamples));
        m.fmHfRatio = std::sqrt(fmDiffEnergy / (fmEnergy + 1e-30));
        m.fmZcRate = static_cast<double>(fmZeroCrossings) / static_cast<double>(fmSamples);
        m.fmPortWrites = _fmTotal - fmTotalStart;
        m.wavePortWrites = _waveTotal - waveTotalStart;
        for (int ch = 0; ch < 18; ch++)
        {
            if (m.fmChannelPeak[ch] > kChannelAudibleFloor)
                m.fmChannelsAudible++;
        }
        return m;
    }

    void PrintMetrics(const char* melody, const PlaybackMetrics& m) const
    {
        std::cout << "[metrics:" << melody << "] frames=" << m.frames
                  << " fmRms=" << m.fmRms << " fmPeak=" << m.fmPeak
                  << " pcmRms=" << m.pcmRms << " fmHfRatio=" << m.fmHfRatio
                  << " fmZcRate=" << m.fmZcRate << " fmPortWrites=" << m.fmPortWrites
                  << " wavePortWrites=" << m.wavePortWrites
                  << " fmChannelsAudible=" << m.fmChannelsAudible << "\n[metrics:" << melody << "] peaks:";
        for (int ch = 0; ch < 18; ch++)
        {
            char buf[24];
            snprintf(buf, sizeof(buf), " %.2f", m.fmChannelPeak[ch]);
            std::cout << buf;
        }
        std::cout << "\n";
    }

    // endregion

    // region <Per-channel isolation sweep (in-tree backend)>

#if !defined(OPL4_FM_YMFM)
    /// Mutes every FM channel except `ch`, measures that channel alone
    /// (mono-summed interleaved pair, as in MeasurePlayback), and restores
    /// an all-unmuted mixer. The verdict bands are the suite-wide tonal/noise
    /// references (HF < 1.0 and ZC < 0.30 vs white noise ~1.41 / 0.5).
    struct ChannelCharacter
    {
        double rms;
        double hf;
        double zc;
    };

    ChannelCharacter MeasureChannelAlone(int ch, SoundChip_Moonsound* moonsound)
    {
        for (int i = 0; i < 18; i++)
            moonsound->setChannelMute(opl4::ChannelGroup::Fm, static_cast<size_t>(i), i != ch);
        ChannelCharacter c{};
        double energy = 0.0;
        double diffEnergy = 0.0;
        long long samples = 0;
        long long zeroCrossings = 0;
        _emulator->DisableTurboMode();
        for (int f = 0; f < 50; f++)
        {
            ResetFrameStats();
            MainLoop()->RunFrame();
            _frame++;
            const int16_t* fm = moonsound->getFmBuffer();
            double lastV = 0.0;
            for (int s = 0; s < SAMPLES_PER_FRAME; s++)
            {
                const double v = (static_cast<double>(fm[2 * s]) + fm[2 * s + 1]) * 0.5;
                energy += v * v;
                const double d = (s == 0) ? 0.0 : (v - lastV);
                diffEnergy += d * d;
                if (s > 0 && ((lastV < 0) != (v < 0)))
                    zeroCrossings++;
                lastV = v;
            }
            samples += SAMPLES_PER_FRAME;
        }
        c.rms = std::sqrt(energy / static_cast<double>(samples));
        c.hf = std::sqrt(diffEnergy / (energy + 1e-30));
        c.zc = static_cast<double>(zeroCrossings) / static_cast<double>(samples);
        for (int i = 0; i < 18; i++)
            moonsound->setChannelMute(opl4::ChannelGroup::Fm, static_cast<size_t>(i), false);
        return c;
    }

    void PrintChannelSweep(SoundChip_Moonsound* moonsound)
    {
        std::cout << "[sweep] per-channel isolation (mute all but one; tonal: hf<1.0 zc<0.30):\n";
        for (int ch = 0; ch < 18; ch++)
        {
            const ChannelCharacter c = MeasureChannelAlone(ch, moonsound);
            const char* verdict = c.rms < 1.0 ? "silent" : (c.hf < 1.0 && c.zc < 0.30) ? "tonal" : "NOISE";
            std::cout << "[sweep] ch" << ch << (ch < 10 ? "  " : " ") << "rms=" << c.rms
                      << " hf=" << c.hf << " zc=" << c.zc << " -> " << verdict << "\n";
        }
    }
#endif // !OPL4_FM_YMFM

    // endregion

    // region <FM register snapshot>

    /// Replays the captured window into a 512-byte shadow and prints the
    /// per-channel operator table (classic YMF262 map; the interleaved
    /// operator offsets 0x20+{0,1,2,8,9,10,16,17,18} for modulators, +3 for
    /// carriers - per melody page bank for channels 9-17).
    void PrintFmRegisterSnapshot() const
    {
        const uint8_t* regs = _captureRegs;
        std::cout << "[regs] window writes=" << _fmWrites.size()
                  << " dataVia C4->C5=" << _dataVia[0][0] << " C4->C7=" << _dataVia[0][1]
                  << " C6->C5=" << _dataVia[1][0] << " C6->C7=" << _dataVia[1][1]
                  << " NEW=" << (regs[0x105] & 1) << " NEW2=" << ((regs[0x105] >> 1) & 1)
                  << " conn104=" << std::hex << static_cast<int>(regs[0x104] & 0x3F) << std::dec
                  << " rhythmBD=" << std::hex << static_cast<int>(regs[0x0BD] >> 5) << std::dec << "\n";
        static const int kModOff[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
        for (int ch = 0; ch < 18; ch++)
        {
            const int bank = ch / 9;
            const int b = ch % 9;
            const uint8_t c0 = regs[bank * 256 + 0xC0 + b];
            const uint8_t b0 = regs[bank * 256 + 0xB0 + b];
            const uint8_t a0 = regs[bank * 256 + 0xA0 + b];
            const int mo = bank * 256 + 0x20 + kModOff[b];
            const int ca = mo + 3;
            const int fnum = ((b0 & 0x03) << 8) | a0; // 10-bit F-number (B0 bits 1:0)
            std::cout << "[regs] ch" << (ch < 10 ? " " : "") << ch
                      << " FB=" << ((c0 >> 1) & 7) << (c0 & 1 ? " AM" : " FM")
                      << " out=" << ((c0 >> 4) & 0xF)
                      << " KON=" << ((b0 >> 5) & 1) << " blk=" << ((b0 >> 2) & 7)
                      << " fnum=" << fnum
                      << " | mod M" << (regs[mo] & 0xF)
                      << " e" << ((regs[mo] >> 5) & 1) << "k" << ((regs[mo] >> 4) & 1)
                      << " TL" << (regs[mo + 0x20] & 0x3F)
                      << " AR" << (regs[mo + 0x40] >> 4) << " DR" << (regs[mo + 0x40] & 0xF)
                      << " SL" << (regs[mo + 0x60] >> 4) << " RR" << (regs[mo + 0x60] & 0xF)
                      << " WS" << (regs[0x100 + 0xE0 + kModOff[b]] & 7)
                      << " | car M" << (regs[ca] & 0xF)
                      << " e" << ((regs[ca] >> 5) & 1) << "k" << ((regs[ca] >> 4) & 1)
                      << " TL" << (regs[ca + 0x20] & 0x3F)
                      << " AR" << (regs[ca + 0x40] >> 4) << " DR" << (regs[ca + 0x40] & 0xF)
                      << " SL" << (regs[ca + 0x60] >> 4) << " RR" << (regs[ca + 0x60] & 0xF)
                      << " WS" << (regs[0x100 + 0xE0 + kModOff[b] + 3] & 7) << "\n";
        }
    }

    // endregion

#if !defined(OPL4_FM_YMFM)
    /// Isolates one FM channel and dumps `frames` frames of its mono-summed
    /// rendered samples to `path` - the offline-FFT evidence for hiss
    /// channels (broadband vs a high squeal line read identically in the
    /// hf/zc scalars of the sweep).
    bool DumpChannelSamples(int ch, int frames, SoundChip_Moonsound* moonsound, const std::string& path)
    {
        for (int i = 0; i < 18; i++)
            moonsound->setChannelMute(opl4::ChannelGroup::Fm, static_cast<size_t>(i), i != ch);
        _emulator->DisableTurboMode();
        std::vector<int16_t> mono(static_cast<size_t>(frames) * SAMPLES_PER_FRAME);
        for (int f = 0; f < frames; f++)
        {
            ResetFrameStats();
            MainLoop()->RunFrame();
            _frame++;
            const int16_t* fm = moonsound->getFmBuffer();
            for (int s = 0; s < SAMPLES_PER_FRAME; s++)
                mono[static_cast<size_t>(f) * SAMPLES_PER_FRAME + s] =
                    static_cast<int16_t>((fm[2 * s] + fm[2 * s + 1]) / 2);
        }
        for (int i = 0; i < 18; i++)
            moonsound->setChannelMute(opl4::ChannelGroup::Fm, static_cast<size_t>(i), false);
        FILE* f = fopen(path.c_str(), "wb");
        if (f == nullptr)
            return false;
        const size_t written = fwrite(mono.data(), sizeof(int16_t), mono.size(), f);
        fclose(f);
        std::cout << "[dump] ch" << ch << " -> " << path << " (" << written << " samples)\n";
        return written == mono.size();
    }

    /// His-or-reference channel sample dumps per melody: name -> channels
    /// (hiss set from the sweep verdicts, plus a tonal reference or two).
    void DumpMelodyChannelSamples(const char* melody, SoundChip_Moonsound* moonsound)
    {
        static const struct
        {
            const char* melody;
            const int channels[8];
        } kDumps[] = {
            {"melody3-djingle2", {6, 7, 8, 9, 15, 16, 17, -1}},
            {"melody5-djingle4", {6, 7, 8, 15, 16, 17, 9, -1}},
            {"melody10-fountain", {2, 5, 9, 11, 13, 14, 7, -1}},
            {"melody11-patstory", {6, 7, 8, 9, 15, 16, 17, -1}}};
        const std::string scratchPrefix = TestPathHelper::GetUniqueTestScratchPath("mfm2-hiss");
        for (const auto& d : kDumps)
        {
            if (strcmp(d.melody, melody) != 0)
                continue;
            for (int i = 0; i < 8 && d.channels[i] >= 0; i++)
            {
                DumpChannelSamples(d.channels[i], 100, moonsound,
                                   scratchPrefix + "-" + melody + "-ch" + std::to_string(d.channels[i]) + ".raw");
            }
        }
    }
#endif // !OPL4_FM_YMFM

    // region <FM write-stream dump>

    /// Prints the first `maxFrames` frames of the captured window as a
    /// decoded per-write stream (frequency/KON and C0 routing decoded to a
    /// channel, operator registers raw) - the "what exactly does the player
    /// command" evidence for a hissing channel.
    void PrintFmWriteStream(int maxFrames, const char* label) const
    {
        std::cout << "[stream:" << label << "] first " << maxFrames << " frames of capture:\n";
        int lastFrame = -1;
        for (const FmWrite& w : _fmWrites)
        {
            const int relFrame = w.frame - _captureStartFrame;
            if (relFrame >= maxFrames)
                break;
            if (relFrame < 0)
                continue; // stale write from an earlier window
            if (relFrame != lastFrame)
            {
                std::cout << "[stream] f" << relFrame << ":";
                lastFrame = relFrame;
            }
            const int r = w.reg;
            if (r >= 0xA0 && r <= 0xA8)
                std::cout << " ch" << (w.bank * 9 + r - 0xA0) << " fnumL=" << static_cast<int>(w.data);
            else if (r >= 0xB0 && r <= 0xB8)
                std::cout << " ch" << (w.bank * 9 + r - 0xB0) << (w.data & 0x20 ? " KON" : " koff")
                          << " blk=" << ((w.data >> 2) & 7) << " fnumH=" << (w.data & 3);
            else if (r >= 0xC0 && r <= 0xC8)
                std::cout << " ch" << (w.bank * 9 + r - 0xC0) << " C0=" << std::hex << static_cast<int>(w.data) << std::dec;
            else
                std::cout << " r" << std::hex << r << std::dec << "=" << static_cast<int>(w.data);
        }
        std::cout << "\n";
    }

    // endregion

    /// Dumps the whole captured window as CSV (relFrame,bank,reg,data) for
    /// offline analysis - the per-register value timelines the last-value
    /// snapshot hides (row-rate patch rewrites on hiss channels).
    void WriteFmWriteCsv(const char* melody) const
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(std::string("mfm2-hiss-capture-") + melody + ".csv");
        FILE* f = fopen(path.c_str(), "w");
        if (f == nullptr)
            return;
        for (const FmWrite& w : _fmWrites)
            fprintf(f, "%d,%d,%d,%d\n", w.frame - _captureStartFrame, w.bank, w.reg, w.data);
        fclose(f);
        std::cout << "[capture] " << melody << " -> " << path << " (" << _fmWrites.size() << " writes)\n";
    }

    /// The demo's melody-advance key: port #7FFE bit 0. One press = one
    /// melody (the handler debounces through MoonSound_key_press).
    void PressMelodyKey(int frames)
    {
        Keyboard* keyboard = _context->pKeyboard;
        ASSERT_NE(keyboard, nullptr);
        keyboard->PressKey(ZXKEY_SPACE);
        RunFrames(frames);
        keyboard->ReleaseKey(ZXKEY_SPACE);
    }

    bool LoadFixture(const std::string& relativePath, size_t expectedSize, const char* what, std::vector<uint8_t>& out)
    {
        const std::string path = TestPathHelper::GetTestDataPath(relativePath);
        if (!FileHelper::FileExists(path) || FileHelper::GetFileSize(path) != expectedSize)
        {
            ADD_FAILURE() << what << ": missing or wrong size: " << path;
            return false;
        }
        out.assign(expectedSize, 0);
        if (FileHelper::ReadFileToBuffer(path, out.data(), expectedSize) != expectedSize)
        {
            ADD_FAILURE() << what << ": short read: " << path;
            return false;
        }
        return true;
    }

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

    /// One staged melody page: tunes copied to their MoonSound_tabl_music
    /// offsets (the table's $C000-based window offsets become page-relative
    /// offsets), zero elsewhere.
    struct MelodyPlacement
    {
        const char* file;
        size_t size;
        size_t offset;
    };

    void StagePage(int ramPage, const MelodyPlacement* placements, size_t count)
    {
        Memory* memory = _context->pMemory;
        memset(memory->RAMPageAddress(ramPage), 0, 0x4000);
        for (size_t i = 0; i < count; i++)
        {
            std::vector<uint8_t> tune;
            if (!LoadFixture(std::string("sound/moonsound/mfm-sample-2/") + placements[i].file,
                             placements[i].size, placements[i].file, tune))
                return;
            memcpy(memory->RAMPageAddress(ramPage) + placements[i].offset, tune.data(), tune.size());
        }
    }

    /// Stages the demo binary + all five melody pages and launches the player
    /// (melody 1 starts by itself). Returns the chip, or nullptr on failure.
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
            ADD_FAILURE() << "MoonSound sample ROM not loaded - the PCM side would be silent";
            return nullptr;
        }

        std::vector<uint8_t> demo;
        if (!LoadFixture("sound/moonsound/mfm-sample-2/moonsound-demo-2.bin", 21920, "demo binary", demo))
            return nullptr;
        // Entry-pattern check: the post-load segment of this build starts at
        // $6297 (file offset 0x97) with ld sp,#5FFF; ld a,10h
        if (demo[0x97] != 0x31 || demo[0x98] != 0xFF || demo[0x99] != 0x5F || demo[0x9A] != 0x3E)
        {
            ADD_FAILURE() << "demo binary is not the expected build: entry pattern at $6297 mismatch";
            return nullptr;
        }

        // Pages per MoonSound_tabl_music (moonsound_demo.asm): page 0x11
        // carries melodies 1-6, 0x13 carries 7-8 (CRYOGENT first), 0x14
        // carries 9-11, 0x16 carries 12-13, 0x17 carries 14-15.
        const MelodyPlacement page11[] = {
            {"alonebtl.mfm", 9663, 0x0000}, {"djingle1.mfm", 1388, 0x25C0}, {"djingle2.mfm", 1046, 0x2B30},
            {"djingle3.mfm", 1106, 0x2F50}, {"djingle4.mfm", 1257, 0x33B0}, {"djingle5.mfm", 1628, 0x38A0}};
        const MelodyPlacement page13[] = {
            {"cryogent.mfm", 7924, 0x0000}, {"dertigap.mfm", 6909, 0x1F00}};
        const MelodyPlacement page14[] = {
            {"feedback.mfm", 6541, 0x0000}, {"fountain.mfm", 6161, 0x1990}, {"patstory.mfm", 1299, 0x31B0}};
        const MelodyPlacement page16[] = {
            {"jdk2.mfm", 10801, 0x0000}, {"salmon.mfm", 4489, 0x2A40}};
        const MelodyPlacement page17[] = {
            {"memory.mfm", 6540, 0x0000}, {"palaceod.mfm", 8811, 0x1990}};
        StagePage(1, page11, 6);
        StagePage(3, page13, 2);
        StagePage(4, page14, 3);
        StagePage(6, page16, 2);
        StagePage(7, page17, 2);

        Memory* memory = _context->pMemory;
        HostCopyToZ80(0x6200, demo.data(), demo.size());
        const std::vector<uint8_t> zeros(6912, 0);
        HostCopyToZ80(0x4000, zeros.data(), zeros.size());

        InstallTraceHook();

        Z80* cpu = _context->pCore->GetZ80();
        if (cpu == nullptr)
        {
            ADD_FAILURE() << "no CPU";
            return nullptr;
        }
        cpu->iff1 = 0;
        cpu->iff2 = 0;
        cpu->halted = 0;
        cpu->pc = 0x6297;

        int launchFrames = 0;
        for (int i = 0; i < 600 && _fmTotal < 300; i++)
        {
            ResetFrameStats();
            MainLoop()->RunFrame();
            _frame++;
            launchFrames++;
        }
        std::cout << "[stage] frames=" << launchFrames << " fmTotal=" << _fmTotal
                  << " waveTotal=" << _waveTotal << "\n";
        if (_fmTotal < 300)
        {
            ADD_FAILURE() << "player never drove the FM side of the card";
            return nullptr;
        }
        return moonsound;
    }
};

/// Melody 7 - CRYOGENT.MFM: the reported "only some channels play, the rest
/// noise" tune. Fences hold on every backend: the mono-summed mix must be
/// loud and tonal - the old defect read as white noise here (hf ~ sqrt(2) ~
/// 1.41, zc ~ 0.5), while the healthy mix measures hf ~ 0.10 in-tree and
/// hf ~ 0.06 on ymfm with zc ~ 0.005 on both.
TEST_F(MoonSoundMfm2Guest_Test, Mfm2_Melody7_Cryogent_PlaysAllChannelsTonal)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    SoundChip_Moonsound* moonsound = StagePlayer();
    ASSERT_NE(moonsound, nullptr);

    // Six Space presses: melody 1 -> 7 (CRYOGENT, page 0x13)
    for (int i = 0; i < 6; i++)
    {
        PressMelodyKey(6);
        RunFrames(50);
    }
    ASSERT_EQ(GuestPeek(kCountMusicAddress), 6) << "melody counter did not reach 7";
    RunFrames(150); // let the tune ramp

    _captureFm = true;
    const PlaybackMetrics m = MeasurePlayback(240, moonsound);
    _captureFm = false;
    PrintMetrics("melody7-cryogent", m);
    PrintFmRegisterSnapshot();
#if !defined(OPL4_FM_YMFM)
    PrintChannelSweep(moonsound);
#endif

    _context->pCore->GetZ80()->busTraceHook = nullptr;

    // Playback mechanics - every backend
    EXPECT_GT(m.fmPortWrites, 0) << "melody7: FM register writes stopped";
    EXPECT_GT(m.wavePortWrites, 0) << "melody7: wave-side register writes stopped";

    // Symptom fences (every backend): the mono-summed mix is a drum/lead
    // arrangement - loud and smooth, not the white-noise bed of the defect.
    EXPECT_GT(m.fmRms, 300.0) << "melody7: FM output too quiet";
    EXPECT_LT(m.fmHfRatio, 0.5) << "melody7: FM output is HF noise, not tonal";
    EXPECT_LT(m.fmZcRate, 0.05) << "melody7: FM zero-crossing rate is noise-like";
}

/// Follow-up symptom report (2026-09-17): after the melody-7 fixes, melodies
/// 3 (DJINGLE2), 5 (DJINGLE4), 10 (FOUNTAIN) and 11 (PATSTORY) still hiss on
/// one or more FM channels while the other eleven tunes sound clean.
///
/// Diagnostic walk through exactly those four melodies: for each one, the
/// full-mix metrics, the reconstructed FM register snapshot and the
/// per-channel isolation sweep name the channel(s) with noise character and
/// the operators behind them. The player's loop-position shadow
/// (MBPlayer_xloop, $8E75) is pinned to 0 after every switch - three of the
/// four tunes are loop=255 jingles that would otherwise fall silent a few
/// seconds in, mid-sweep (MBPlayer_init re-copies the shadow from the song
/// block, so the pin must be re-applied per melody).
///
/// RESOLVED (2026-09-17): the hiss is authentic to the demo binary, not an
/// emulator defect. The capture now starts before the melody-advance
/// keypress, so melody init is visible: each channel gets C0 written twice
/// - first the instrument's fbconn|0x30 (lead = FB6-add, pad = FB5-add),
/// then a stereo-panning pass writing 0x0F|pan, leaving every FM channel at
/// FB7 additive. Sustained low-TL instruments (lead TL10/18, melody-3
/// variant TL5/62, pad TL7/10) self-oscillate into chaos at FB7 on every
/// accurate implementation (in-tree, ymfm, Nuked-OPL3; see
/// Diagnostic_MfmHissPatches_SustainedVsRetrig), while the eleven clean
/// melodies' instruments keep the FB7 loop stable. The same register stream
/// reaches real hardware from this binary (the TRD's player is byte-
/// identical to the analyzed 2016 build).
TEST_F(MoonSoundMfm2Guest_Test, Mfm2_HissingMelodies_Diagnostic)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    SoundChip_Moonsound* moonsound = StagePlayer();
    ASSERT_NE(moonsound, nullptr);

    static constexpr uint16_t kXloopShadow = 0x8E75; // MBPlayer_xloop (moonsound_demo.lst)

    struct Target
    {
        int melody;
        const char* name;
    };
    const Target targets[] = {
        {3, "melody3-djingle2"}, {5, "melody5-djingle4"}, {10, "melody10-fountain"}, {11, "melody11-patstory"}};

    int pressed = 0;             // melody 1 is already playing (count = 0)
    int lastFmPortWrites = 0;     // mechanics guard from the final melody
    for (const Target& t : targets)
    {
        // Capture from BEFORE the advance key presses: the switch into this
        // melody happens inside PressMelodyKey, and that is where
        // MBPlayer_init reprograms the instruments, C0 and 0x104 - windows
        // starting after arrival only ever see A0/B0 traffic and cannot
        // answer where the FB=7 C0 state comes from.
        _fmWrites.clear();
        _captureFm = true;
        _captureStartFrame = _frame;
        while (pressed < t.melody - 1)
        {
            PressMelodyKey(6);
            RunFrames(50);
            pressed++;
        }
        ASSERT_EQ(GuestPeek(kCountMusicAddress), t.melody - 1) << "melody counter did not reach " << t.melody;

        RunFrames(30); // let MBPlayer_init and the first rows land

        _context->pMemory->DirectWriteToZ80Memory(kXloopShadow, 0);
        ASSERT_EQ(GuestPeek(kXloopShadow), 0) << "loop-position pin failed";

        const PlaybackMetrics m = MeasurePlayback(120, moonsound);
        _captureFm = false;
        lastFmPortWrites = m.fmPortWrites;
        PrintMetrics(t.name, m);
        PrintFmRegisterSnapshot();
        PrintFmWriteStream(12, t.name);
        WriteFmWriteCsv(t.name);
#if !defined(OPL4_FM_YMFM)
        PrintChannelSweep(moonsound);
        DumpMelodyChannelSamples(t.name, moonsound);
#endif
    }

    _context->pCore->GetZ80()->busTraceHook = nullptr;

    // Playback mechanics only - this is a diagnostic pass; the per-melody
    // noise verdicts in the sweep output carry the findings.
    EXPECT_GT(lastFmPortWrites, 0) << "FM register writes stopped";
}

#endif // UNREALNG_HAVE_OPL4
