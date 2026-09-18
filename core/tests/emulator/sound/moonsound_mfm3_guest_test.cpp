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

/// Guest-level verification of "MFM Music sample 3" (testdata/sound/moonsound/
/// mfm_sample_3.trd, 13 melodies per the demo's MoonSound_tabl_music; fixtures
/// staged at testdata/sound/moonsound/mfm-sample-3/ from the author's build
/// tree - the TRD's player and all melodies are byte-identical to that tree).
///
/// Symptom report (2026-09-17): module 5 (JAMMED2.MFM - first tune on melody
/// page 0x13, reached with four Space presses) produces "dirty" sound on 1-3
/// note channels in the FM synthesizer - "sounds like highly quantized with
/// pink noise"; the other melodies sound clean.
///
/// Same guest path as the sample-2 suite: the shipped moonsound.bin is staged
/// at $6200 with the five melody pages (0x11/0x13/0x14/0x16/0x17) placed at
/// their MoonSound_tabl_music offsets, launched at the post-load entry $62D7
/// (ld sp,#5FFF segment of this 22424-byte build), melody 1 starts by itself,
/// Space advances the counter at $6592. All rendered-audio metrics mono-sum
/// the interleaved L/R pair; diagnostics: reconstructed FM register snapshot,
/// per-channel isolation sweep and raw channel dumps (in-tree backend only),
/// plus a full FM write-stream CSV captured from before the melody switch so
/// module-5 init traffic is visible.
class MoonSoundMfm3Guest_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();

        // Same staging contract as the sample-1/2 suites (MoonSound=1, idle
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

    /// Demo state variable (moonsound_demo.lst of this build): the melody
    /// counter the Space handler increments ($6592 - moved from the sample-2
    /// build's $654D by the larger player).
    static constexpr uint16_t kCountMusicAddress = 0x6592;

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

    /// This tune's rows advance only every ~2.4 s and most instruments decay
    /// to their SL15 sustain floor (silence) within a second - a steady-state
    /// window samples nothing. A row advance shows up as a write burst (one
    /// koff/KON per channel), so: run until a frame carries `minWrites` FM
    /// writes (a fresh row), then measure `frames` frames - the ring-out.
    bool RunUntilRowBurst(int minWrites, int maxFrames)
    {
        _emulator->DisableTurboMode();
        for (int i = 0; i < maxFrames; i++)
        {
            ResetFrameStats();
            MainLoop()->RunFrame();
            _frame++;
            if (_fmOuts >= minWrites)
                return true;
        }
        return false;
    }

    /// Burst-aligned per-channel isolation: waits for the next row advance,
    /// then measures `frames` frames of channel `ch` alone (envelopes open).
    ChannelCharacter MeasureChannelAfterBurst(int ch, int frames, SoundChip_Moonsound* moonsound)
    {
        for (int i = 0; i < 18; i++)
            moonsound->setChannelMute(opl4::ChannelGroup::Fm, static_cast<size_t>(i), i != ch);
        RunUntilRowBurst(20, 300);
        ChannelCharacter c{};
        double energy = 0.0;
        double diffEnergy = 0.0;
        long long samples = 0;
        long long zeroCrossings = 0;
        for (int f = 0; f < frames; f++)
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
            const ChannelCharacter c = MeasureChannelAfterBurst(ch, 25, moonsound);
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
    /// rendered samples to `path` - the offline-FFT evidence for dirty
    /// channels (pink vs white slope, quantization comb - read identically
    /// in the hf/zc scalars of the sweep).
    bool DumpChannelSamples(int ch, int frames, SoundChip_Moonsound* moonsound, const std::string& path)
    {
        for (int i = 0; i < 18; i++)
            moonsound->setChannelMute(opl4::ChannelGroup::Fm, static_cast<size_t>(i), i != ch);
        // Burst-aligned like the sweep: catch the row's ring-out, not the
        // SL15 silence between rows.
        RunUntilRowBurst(20, 300);
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

    /// Dumps every FM channel of `melody` (the dirty set is not yet known,
    /// so all 18 go to scratch for offline FFT).
    void DumpMelodyChannelSamples(const char* melody, SoundChip_Moonsound* moonsound)
    {
        const std::string scratchPrefix = TestPathHelper::GetUniqueTestScratchPath("mfm3-dirty");
        for (int ch = 0; ch < 18; ch++)
        {
            DumpChannelSamples(ch, 100, moonsound,
                               scratchPrefix + "-" + melody + "-ch" + std::to_string(ch) + ".raw");
        }
    }
#endif // !OPL4_FM_YMFM

    // region <FM write-stream dump>

    /// Prints the first `maxFrames` frames of the captured window as a
    /// decoded per-write stream (frequency/KON and C0 routing decoded to a
    /// channel, operator registers raw) - the "what exactly does the player
    /// command" evidence for a dirty channel.
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
    /// snapshot hides (row-rate patch rewrites on dirty channels).
    void WriteFmWriteCsv(const char* melody) const
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(std::string("mfm3-dirty-capture-") + melody + ".csv");
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
            if (!LoadFixture(std::string("sound/moonsound/mfm-sample-3/") + placements[i].file,
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
        if (!LoadFixture("sound/moonsound/mfm-sample-3/moonsound-demo-3.bin", 22424, "demo binary", demo))
            return nullptr;
        // Entry-pattern check: the post-load segment of this build starts at
        // $62D7 (file offset 0xD7) with ld sp,#5FFF; ld a,10h
        if (demo[0xD7] != 0x31 || demo[0xD8] != 0xFF || demo[0xD9] != 0x5F || demo[0xDA] != 0x3E)
        {
            ADD_FAILURE() << "demo binary is not the expected build: entry pattern at $62D7 mismatch";
            return nullptr;
        }

        // Pages per MoonSound_tabl_music (moonsound_demo.asm): page 0x11
        // carries melodies 1-4, 0x13 carries 5-6 (JAMMED2 first), 0x14
        // carries 7-8, 0x16 carries 9-11, 0x17 carries 12-13.
        const MelodyPlacement page11[] = {
            {"aleste.mfm", 4143, 0x0000}, {"forest.mfm", 5472, 0x1030}, {"huishuis.mfm", 3222, 0x2590},
            {"relaxed.mfm", 1197, 0x3230}};
        const MelodyPlacement page13[] = {
            {"jammed2.mfm", 7798, 0x0000}, {"jdktheme.mfm", 6511, 0x1E80}};
        const MelodyPlacement page14[] = {
            {"matin.mfm", 4000, 0x0000}, {"morngrow.mfm", 10086, 0x0FA0}};
        const MelodyPlacement page16[] = {
            {"parodius.mfm", 10182, 0x0000}, {"riedel.mfm", 2509, 0x27D0}, {"slowdown.mfm", 3199, 0x31A0}};
        const MelodyPlacement page17[] = {
            {"randam.mfm", 7909, 0x0000}, {"salmon_1.mfm", 6000, 0x1EF0}};
        StagePage(1, page11, 4);
        StagePage(3, page13, 2);
        StagePage(4, page14, 2);
        StagePage(6, page16, 3);
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
        cpu->pc = 0x62D7;

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

/// Symptom target: module 5 (JAMMED2.MFM) - "dirty" sound on 1-3 note
/// channels, "highly quantized with pink noise". Four Space presses reach
/// it (melody 1 plays by itself). The full-mix metrics, the reconstructed
/// FM register snapshot, the per-channel isolation sweep and the raw
/// channel dumps name the dirty channel(s) and the operators behind them;
/// the write-stream CSV captured from before the switch shows what the
/// player programs at module-5 init (the sample-2 investigation found the
/// init panning pass clobbering C0 feedback bits there). The player's
/// loop-position shadow (MBPlayer_xloop, $9026) is pinned to 0 after the
/// switch so a looping tune cannot fall silent mid-sweep.
///
/// RESOLVED (2026-09-18, overturning 2026-09-17's "authentic" verdict):
/// an emulator port bug, not the demo. The module-5 init capture (frame
/// ~175 of the CSV) writes each channel's C0 twice - first the instrument's
/// fbconn|0x30 (JAMMED2: FB4-FM lead on ch0-5/9/10/12/17, FB7-FM pluck on
/// ch6/14/16, FB6-ADD pad on ch7/8/11/13/15 - byte-exact against the .MFM
/// instrument table), then the pan pass's RMW result. That pass reads C0
/// back through the FM data port (in a,(c)), keeps the low nibble (and
/// 0Fh) and rewrites it with the pan bits - the original MSX driver does
/// exactly the same - but the emulator did not answer FM data-port reads,
/// so the read floated 0xFF and every channel ended at 0x0F|pan = FB7
/// additive. Real hardware returns the register value (openMSX
/// MSXMoonSound::readIO -> readReg). With SoundChip_Moonsound claiming
/// #C5/#C7 reads and returning the register-file shadow, the pad keeps its
/// authored FB6 and the sweep reads tonal (hf 0.06-0.10). The rate-0
/// freeze fix stands: DR0+EGT1 is what holds the pad modulator at its
/// attack peak for the sustained timbre. The ymfm backend shows identical
/// register traffic and mix-level behavior.
TEST_F(MoonSoundMfm3Guest_Test, Mfm3_Module5_Jammed2_DirtyChannels_Diagnostic)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    SoundChip_Moonsound* moonsound = StagePlayer();
    ASSERT_NE(moonsound, nullptr);

    static constexpr uint16_t kXloopShadow = 0x9026; // MBPlayer_xloop (moonsound_demo.lst)
    const char* name = "module5-jammed2";
    const int target = 5;

    // Capture from BEFORE the advance key presses: the switch into module 5
    // happens inside PressMelodyKey, and that is where MBPlayer_init
    // reprograms the instruments, C0 and 0x104 - windows starting after
    // arrival only ever see A0/B0 traffic.
    _fmWrites.clear();
    _captureFm = true;
    _captureStartFrame = _frame;
    int pressed = 0; // melody 1 is already playing (count = 0)
    while (pressed < target - 1)
    {
        PressMelodyKey(6);
        RunFrames(50);
        pressed++;
    }
    ASSERT_EQ(GuestPeek(kCountMusicAddress), target - 1) << "melody counter did not reach " << target;

    RunFrames(30); // let MBPlayer_init and the first rows land

    _context->pMemory->DirectWriteToZ80Memory(kXloopShadow, 0);
    ASSERT_EQ(GuestPeek(kXloopShadow), 0) << "loop-position pin failed";

    // Two steady-state reference rounds (metrics + snapshot), then the
    // burst-aligned diagnostics: rows advance only every ~2.4 s and the
    // SL15 instruments ring for under a second, so steady-state windows
    // show one survivor channel while the row retriggers all eighteen -
    // the sweep and dumps therefore start at a detected row advance.
    for (int round = 1; round <= 2; round++)
    {
        const PlaybackMetrics m = MeasurePlayback(120, moonsound);
        char roundName[48];
        snprintf(roundName, sizeof(roundName), "%s-r%d", name, round);
        PrintMetrics(roundName, m);
        PrintFmRegisterSnapshot();
    }
#if !defined(OPL4_FM_YMFM)
    PrintChannelSweep(moonsound);
    DumpMelodyChannelSamples(name, moonsound);
#endif
    _captureFm = false;
    PrintFmWriteStream(12, name);
    WriteFmWriteCsv(name);

    _context->pCore->GetZ80()->busTraceHook = nullptr;

    // Playback mechanics only - this is a diagnostic pass; the per-channel
    // sweep verdicts in the output carry the findings.
    EXPECT_GT(_fmTotal, 0) << "FM register writes stopped";
}

/// Module 7 (MATIN.MFM - first tune on melody page 0x14, six Space
/// presses). Symptom report (2026-09-18): FM8/FM9/FM10 render noise where
/// a clean sine+envelope is expected. Same diagnostic battery as module 5:
/// init-window capture (per-channel default instruments + the C0 double
/// write), steady-state metrics, register snapshot, burst-aligned sweep and
/// raw channel dumps, loop-position pinned so the tune cannot fall silent.
TEST_F(MoonSoundMfm3Guest_Test, Mfm3_Module7_Matin_Diagnostic)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    SoundChip_Moonsound* moonsound = StagePlayer();
    ASSERT_NE(moonsound, nullptr);

    static constexpr uint16_t kXloopShadow = 0x9026; // MBPlayer_xloop (moonsound_demo.lst)
    const char* name = "module7-matin";
    const int target = 7;

    _fmWrites.clear();
    _captureFm = true;
    _captureStartFrame = _frame;
    int pressed = 0; // melody 1 is already playing (count = 0)
    while (pressed < target - 1)
    {
        PressMelodyKey(6);
        RunFrames(50);
        pressed++;
    }
    ASSERT_EQ(GuestPeek(kCountMusicAddress), target - 1) << "melody counter did not reach " << target;

    RunFrames(30); // let MBPlayer_init and the first rows land

    _context->pMemory->DirectWriteToZ80Memory(kXloopShadow, 0);
    ASSERT_EQ(GuestPeek(kXloopShadow), 0) << "loop-position pin failed";

    for (int round = 1; round <= 2; round++)
    {
        const PlaybackMetrics m = MeasurePlayback(120, moonsound);
        char roundName[48];
        snprintf(roundName, sizeof(roundName), "%s-r%d", name, round);
        PrintMetrics(roundName, m);
        PrintFmRegisterSnapshot();
    }
#if !defined(OPL4_FM_YMFM)
    PrintChannelSweep(moonsound);
    DumpMelodyChannelSamples(name, moonsound);
#endif
    _captureFm = false;
    PrintFmWriteStream(12, name);
    WriteFmWriteCsv(name);

    _context->pCore->GetZ80()->busTraceHook = nullptr;

    EXPECT_GT(_fmTotal, 0) << "FM register writes stopped";
}

#endif // UNREALNG_HAVE_OPL4
