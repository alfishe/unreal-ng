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
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_moonsound.h"
#include "emulator/sound/soundmanager.h"

/// Guest-level verification of "MFM Music sample 4" (testdata/sound/moonsound/
/// mfm_sample_4.trd, fixtures staged at testdata/sound/moonsound/mfm-sample-4/
/// from the author's build tree - the TRD's raw-sector player and HAPERT.MFM
/// are byte-identical to moonsound.bin/HAPERT.MFM there).
///
/// Symptom report (2026-09-18): melody 1 (HAPERT.MFM - plays by itself at
/// boot) shows dirt on "F13", which is either hardware FM channel 13 or FM
/// step 13 (hardware channel 2). Both candidates play the same two
/// instruments with a change in each direction: instrument 3 (accordion:
/// FM connection FB7, modulator 24 dB down with slow attack - a gently
/// phase-modulated sine) and instrument 4 (FB6 additive pluck). Operator
/// maths already ruled out: both instruments' bytes stay clean and periodic
/// under libopl4 and ymfm for all 16 feedback/connection values except
/// instrument 4 at FB7. The FM data-port read-back defect (mfm-sample-3
/// findings, fixed 2026-09-18) cannot explain it either: the C0 pan pass
/// would move instrument 3 toward additive (cleaner), and an instrument
/// change restores the authored FB/CONN even with a 0xFF read. HAPERT has
/// zero 4-op chains and the player writes 0x104=0.
///
/// So the remaining suspects are in the delivered register stream: bank-1
/// port routing (#C6/#C7 lanes for hardware channel 13), or a mis-landed
/// 0x104 write pairing channel 13 with channel 10. The harness captures the
/// FM write stream from BEFORE launch (MBPlayer_init for melody 1 lands in
/// the launch loop), reconstructs the register snapshot (conn104 + the
/// data-port routing histogram answer the two suspects directly), and runs
/// the burst-aligned per-channel isolation sweep so both F13 candidates
/// (hardware ch13 and ch2) carry an explicit tonal/NOISE verdict.
///
/// RESOLVED (2026-09-18, on the read-back-fixed build): no dirt anywhere in
/// HAPERT, and none of the three remaining suspects holds. The first
/// battery's ~54 s window never sounds either F13 candidate (ch2/ch13 are
/// programmed at init and first KON only at f3152), so the deep-scan pass
/// below walks the whole 3-minute tune. Delivered-stream verdicts: 0x104 is
/// written exactly twice (f14/f9207, both 0 - zero 4-op chains ever), the
/// bank-1 lanes carry the authored bytes (ch13's register line matches the
/// quoted instrument 3: FB7-FM, modulator TL32 = 24 dB down, AR2 slow
/// attack), and the pan-pass RMW preserves the feedback nibble (ch13
/// 3E->1E, ch2 3E->2E at f14; pre-fix these collapsed to 0x0F|pan = FB7
/// additive). Authored C0 restores land at every section change
/// (f3138/f4489/f5826/f7177 - the predicted instrument-change restore), so
/// the pre-fix corruption window on the F13 voices was f3152..f4489; the
/// f4489 pluck bytes (FB6-ADD authored) would have been clean even then.
/// Per-channel: all 18 tonal or silent in the sweep; ch13 attack/sustain
/// hf 0.10/0.33, ch2 0.09/0.09, ch12 0.08/0.08 - all far under the 1.0/0.30
/// noise bands. The ymfm backend replays byte-identical register traffic
/// (55294 writes, same KON frames) and the offline three-engine replay of
/// the deep capture (scratch/replay3way-mfm4-hapert.log) is tonal in all 54
/// engine x channel combinations. Whatever the audible F13 dirt was, it
/// lived on the pre-fix build.
class MoonSoundMfm4Guest_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();

        // Same staging contract as the sample-1/2/3 suites (MoonSound=1,
        // idle TSFM pair dropped to AY, Pentagon 128K, harness-staged
        // launch).
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

    /// Demo state variables (moonsound_demo.lst of this build): the melody
    /// counter ($653B) and the player's loop-position shadow
    /// (MBPlayer_xloop, $8F6C).
    static constexpr uint16_t kCountMusicAddress = 0x653B;
    static constexpr uint16_t kXloopShadowAddress = 0x8F6C;

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
    /// keyed by the most recent ADDRESS port (#C4 vs #C6) - the bank-1
    /// routing suspect for hardware channel 13.
    int _lastAddrPort = 0; // 0 = #C4, 1 = #C6
    int _dataVia[2][2] = {}; // [addrPort][dataPort: 0=#C5, 1=#C7]
    /// Last frame each channel received a KON (B0 bit 5) - the deep scan's
    /// per-voice burst alignment (a channel's own note-on, not any row).
    int _konFrame[18] = {};

    void ResetFrameStats()
    {
        _fmOuts = 0;
        _waveOuts = 0;
    }

    void InstallTraceHook()
    {
        for (int i = 0; i < 18; i++)
            _konFrame[i] = -1;
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
                    if (reg >= 0xB0 && reg <= 0xB8 && (value & 0x20) != 0)
                        _konFrame[bank * 9 + (reg - 0xB0)] = _frame;
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
                // Interleaved stereo: mono-sum the L/R pair so side-routed
                // channels measure as what they are.
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

    /// Row advances show up as write bursts (one koff/KON per channel), so:
    /// run until a frame carries `minWrites` FM writes (a fresh row), then
    /// measure `frames` frames - the ring-out.
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

    /// Isolates one FM channel and measures it right NOW (no burst wait -
    /// the caller just saw the channel's own KON, so its envelope is
    /// opening). Restores the all-unmuted mixer afterwards.
    ChannelCharacter MeasureChannelNow(int ch, int frames, SoundChip_Moonsound* moonsound)
    {
        for (int i = 0; i < 18; i++)
            moonsound->setChannelMute(opl4::ChannelGroup::Fm, static_cast<size_t>(i), i != ch);
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

    static const char* ChannelVerdict(const ChannelCharacter& c)
    {
        return c.rms < 1.0 ? "silent" : (c.hf < 1.0 && c.zc < 0.30) ? "tonal" : "NOISE";
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
        // inter-row silence.
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

    /// Dumps every FM channel of the melody (the dirty set is confirmed by
    /// the sweep first, so all 18 go to scratch for offline FFT).
    void DumpMelodyChannelSamples(const char* melody, SoundChip_Moonsound* moonsound)
    {
        const std::string scratchPrefix = TestPathHelper::GetUniqueTestScratchPath("mfm4-hapert");
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
    /// offline analysis (replay into the PoC engines, per-register value
    /// timelines the last-value snapshot hides).
    void WriteFmWriteCsv(const char* melody) const
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(std::string("mfm4-hapert-capture-") + melody + ".csv");
        FILE* f = fopen(path.c_str(), "w");
        if (f == nullptr)
            return;
        for (const FmWrite& w : _fmWrites)
            fprintf(f, "%d,%d,%d,%d\n", w.frame - _captureStartFrame, w.bank, w.reg, w.data);
        fclose(f);
        std::cout << "[capture] " << melody << " -> " << path << " (" << _fmWrites.size() << " writes)\n";
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
            if (!LoadFixture(std::string("sound/moonsound/mfm-sample-4/") + placements[i].file,
                             placements[i].size, placements[i].file, tune))
                return;
            memcpy(memory->RAMPageAddress(ramPage) + placements[i].offset, tune.data(), tune.size());
        }
    }

    /// Stages the demo binary + the melody-1 page (HAPERT) and launches the
    /// player (melody 1 starts by itself - count_music defaults to 0 in the
    /// staged binary). Returns the chip, or nullptr on failure.
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
        if (!LoadFixture("sound/moonsound/mfm-sample-4/moonsound-demo-4.bin", 22114, "demo binary", demo))
            return nullptr;
        // Entry-pattern check: the post-load segment of this build starts at
        // $629A (file offset 0x9A) with ld sp,#5FFF; ld a,10h
        if (demo[0x9A] != 0x31 || demo[0x9B] != 0xFF || demo[0x9C] != 0x5F || demo[0x9D] != 0x3E)
        {
            ADD_FAILURE() << "demo binary is not the expected build: entry pattern at $629A mismatch";
            return nullptr;
        }

        // Page per MoonSound_tabl_music (moonsound_demo.asm): page 0x11
        // carries melody 1 - HAPERT at page-relative offset 0, length 0x3C00.
        const MelodyPlacement page11[] = {
            {"hapert.mfm", 15348, 0x0000}};
        StagePage(1, page11, 1);

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
        cpu->pc = 0x629A;

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

/// Symptom target: melody 1 (HAPERT.MFM) - dirt on F13 (either hardware
/// channel 13 or FM step 13 = hardware channel 2; both play the accordion
/// instrument 3 and the pluck instrument 4 with one change each way).
/// Capture starts BEFORE the launch so the MBPlayer_init window (default
/// instruments, the C0 panning pass, 0x104) is inside the stream - the CSV
/// is the replay source for the offline engines. The register snapshot's
/// conn104 and data-port routing histogram answer the 4-op-pairing and
/// bank-routing suspects directly; the per-channel sweep gives both F13
/// candidates an explicit tonal/NOISE verdict. The player's loop-position
/// shadow (MBPlayer_xloop, $8F6C) is pinned to 0 so the tune cannot fall
/// silent mid-sweep.
///
/// Outcome (see the class comment): all 18 channels tonal or silent in the
/// ~54 s window; ch2/ch13 first sound only at f3152, which is why the
/// DeepScan test below exists.
TEST_F(MoonSoundMfm4Guest_Test, Mfm4_Melody1_Hapert_F13_Diagnostic)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    // Capture from BEFORE launch: MBPlayer_init for melody 1 lands inside
    // StagePlayer's launch loop, and that is where the instruments, C0 and
    // 0x104 are programmed.
    _fmWrites.clear();
    _captureFm = true;
    _captureStartFrame = _frame;

    SoundChip_Moonsound* moonsound = StagePlayer();
    ASSERT_NE(moonsound, nullptr);
    ASSERT_EQ(GuestPeek(kCountMusicAddress), 0) << "melody 1 (HAPERT) must be the boot tune";

    RunFrames(30); // let the first rows land

    _context->pMemory->DirectWriteToZ80Memory(kXloopShadowAddress, 0);
    ASSERT_EQ(GuestPeek(kXloopShadowAddress), 0) << "loop-position pin failed";

    // Two steady-state reference rounds (metrics + snapshot), then the
    // burst-aligned diagnostics: rows advance as write bursts and the
    // instruments ring out between them, so the sweep and dumps start at a
    // detected row advance.
    for (int round = 1; round <= 2; round++)
    {
        const PlaybackMetrics m = MeasurePlayback(120, moonsound);
        char roundName[48];
        snprintf(roundName, sizeof(roundName), "melody1-hapert-r%d", round);
        PrintMetrics(roundName, m);
        PrintFmRegisterSnapshot();
    }
#if !defined(OPL4_FM_YMFM)
    PrintChannelSweep(moonsound);
    DumpMelodyChannelSamples("melody1-hapert", moonsound);
#endif
    _captureFm = false;
    PrintFmWriteStream(12, "melody1-hapert");
    WriteFmWriteCsv("melody1-hapert");

    _context->pCore->GetZ80()->busTraceHook = nullptr;

    // Playback mechanics only - this is a diagnostic pass; the per-channel
    // sweep verdicts in the output carry the findings.
    EXPECT_GT(_fmTotal, 0) << "FM register writes stopped";
}

/// Deep coverage: the first battery samples only ~54 s of the tune and in
/// that span neither F13 candidate ever sounds (ch2/ch13 are programmed at
/// init and then sit silent; ch10/12/16/17 carry the accordion from frame
/// 16, ch8/15 join at ~f470, ch5-9 at ~f1770). This pass walks the tune
/// frame by frame watching each channel's own KON: the moment a target
/// fires a note-on it is isolated and measured through its attack window,
/// then again ~6 s later in sustain. The full-mix hf/zc is tracked per
/// 150-frame round so a noisy section anywhere in the covered span stands
/// out even on channels the targets do not cover, and the whole span goes
/// to CSV for the offline replay (replay3way: in-tree vs ymfm vs Nuked on
/// the identical register stream).
///
/// Outcome: the tune runs ~3 minutes (last KONs f9018) and the scan covers
/// it end to end (9299 frames before the silence stop). ch13 and ch2 first
/// sound together at f3152 (the F13 section entry) carrying the authored
/// accordion C0 1E/2E, and measure tonal through attack and sustain
/// (ch13 hf 0.10/0.33, ch2 0.09/0.09); ch12 (direct voice mapping) is
/// tonal from its first KON at f51. No round's mix hf exceeds the dense
/// final section's 0.45-0.67 at zc <= 0.18 - bright tonal, nowhere near
/// the 1.4/0.5 white-noise band - and the ymfm backend tracks the same
/// shape on byte-identical traffic.
TEST_F(MoonSoundMfm4Guest_Test, Mfm4_Melody1_Hapert_F13_DeepScan)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    _fmWrites.clear();
    _captureFm = true;
    _captureStartFrame = _frame;

    SoundChip_Moonsound* moonsound = StagePlayer();
    ASSERT_NE(moonsound, nullptr);
    ASSERT_EQ(GuestPeek(kCountMusicAddress), 0) << "melody 1 (HAPERT) must be the boot tune";

    RunFrames(30); // let the first rows land

    _context->pMemory->DirectWriteToZ80Memory(kXloopShadowAddress, 0);
    ASSERT_EQ(GuestPeek(kXloopShadowAddress), 0) << "loop-position pin failed";

    // F13 candidates: hardware channel 13, FM step 13 as hardware channel
    // 2, and (direct voice mapping) FM voice 13 as hardware channel 12.
    static constexpr int kTargets[] = {13, 2, 12};
    int konBaseline[18];
    for (int ch = 0; ch < 18; ch++)
        konBaseline[ch] = _konFrame[ch];
    bool attackDone[18] = {};
    int sustainDue[18] = {};
    bool sustainDone[18] = {};

    _emulator->DisableTurboMode();
    double energy = 0.0;
    double diffEnergy = 0.0;
    long long samples = 0;
    long long zeroCrossings = 0;
    static constexpr int kDeepFrames = 12000; // ~4 min of tune
    static constexpr int kRoundFrames = 150;
    int silentRounds = 0;
    int f = 0;
    for (; f < kDeepFrames; f++)
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

#if !defined(OPL4_FM_YMFM)
        // A target fired its first note-on: isolate it now (envelope
        // opening) and schedule the sustain re-measure.
        for (int t : kTargets)
        {
            if (!attackDone[t] && _konFrame[t] > konBaseline[t])
            {
                attackDone[t] = true;
                sustainDue[t] = _frame + 300; // ~6 s of settled timbre
                std::cout << "[deep] ch" << t << " first KON at f" << _konFrame[t]
                          << " C0=" << std::hex << static_cast<int>(_captureRegs[(t / 9) * 256 + 0xC0 + t % 9]) << std::dec << "\n";
                const ChannelCharacter c = MeasureChannelNow(t, 40, moonsound);
                std::cout << "[deep] ch" << t << " attack  rms=" << c.rms
                          << " hf=" << c.hf << " zc=" << c.zc << " -> " << ChannelVerdict(c) << "\n";
            }
            if (attackDone[t] && !sustainDone[t] && _frame >= sustainDue[t])
            {
                sustainDone[t] = true;
                const ChannelCharacter c = MeasureChannelNow(t, 60, moonsound);
                std::cout << "[deep] ch" << t << " sustain rms=" << c.rms
                          << " hf=" << c.hf << " zc=" << c.zc << " -> " << ChannelVerdict(c) << "\n";
            }
        }
#else
        // ymfm build: no mixer isolation - announce the KONs only.
        (void)sustainDue;
        (void)sustainDone;
        for (int t : kTargets)
        {
            if (!attackDone[t] && _konFrame[t] > konBaseline[t])
            {
                attackDone[t] = true;
                std::cout << "[deep] ch" << t << " first KON at f" << _konFrame[t] << "\n";
            }
        }
#endif // !OPL4_FM_YMFM

        if ((f + 1) % kRoundFrames == 0)
        {
            const double rms = std::sqrt(energy / static_cast<double>(samples));
            const double hf = std::sqrt(diffEnergy / (energy + 1e-30));
            const double zcRate = static_cast<double>(zeroCrossings) / static_cast<double>(samples);
            std::cout << "[deep] f" << _frame << " round rms=" << rms << " hf=" << hf << " zc=" << zcRate << "\n";
            if (rms < 1.0)
                silentRounds++;
            else
                silentRounds = 0;
            energy = 0.0;
            diffEnergy = 0.0;
            samples = 0;
            zeroCrossings = 0;
            // Re-pin the loop shadow so the tune cannot run itself out.
            _context->pMemory->DirectWriteToZ80Memory(kXloopShadowAddress, 0);
            if (silentRounds >= 2)
            {
                std::cout << "[deep] tune ran silent for " << silentRounds * kRoundFrames << " frames - stopping\n";
                break;
            }
        }
    }
    std::cout << "[deep] covered " << f << " frames (" << (f / 50) << " s of tune)\n";
    for (int ch = 0; ch < 18; ch++)
        std::cout << "[deep] ch" << ch << (ch < 10 ? "  " : " ")
                  << "lastKON=" << (_konFrame[ch] < 0 ? "never" : std::to_string(_konFrame[ch])) << "\n";
    PrintFmRegisterSnapshot();

    _captureFm = false;
    WriteFmWriteCsv("melody1-hapert-deep");

    _context->pCore->GetZ80()->busTraceHook = nullptr;

    EXPECT_GT(_fmTotal, 0) << "FM register writes stopped";
}

#endif // UNREALNG_HAVE_OPL4
