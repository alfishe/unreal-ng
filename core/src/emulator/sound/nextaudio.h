#pragma once

/// @file nextaudio.h
/// @brief The ZX Spectrum Next's sound (research-fpga-vhdl.md section 18): three AY / YM chips (turbosound) with a
/// per-chip stereo pan, ABC / ACB / mono mixing, and the four-channel 8-bit DAC. One device owned by the Next's
/// port decoder and mixed by SoundManager as a model audio source (the Sprinter's slot): it replaces the two-chip
/// TurboSound of the Spectrum 128 path, which stays silent on this machine.
///
/// Ports: #FFFD (read: the selected chip's register; write: bit 7 = 1 with bits 4:2 = 111 selects chip and pan, else the
/// register), #BFFD (data); the DAC ports and NR #2C / #2D / #2E feed WriteDac. The generators run at the AY clock
/// (1.75 MHz, a tick every 16 base T-states) whatever the CPU speed; register writes land at the T-state they happen
/// in and the output is rebuilt per frame from the per-tick levels.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/modelaudiosource.h"

class EmulatorContext;

class NextAudio : public IModelAudioSource
{
public:
    static constexpr unsigned kChips = 3;
    static constexpr size_t kParts = 4;  ///< the three chips, then the DAC

    explicit NextAudio(EmulatorContext* context);
    ~NextAudio() override = default;

    /// Absolute base T-state (3.5 MHz units, fractions allowed) of the CPU now
    void SetTimeSource(std::function<double()> now) { _now = std::move(now); }
    void Reset();

    /// region <Ports>
    void WriteSelect(uint8_t value);  ///< #FFFD
    void WriteData(uint8_t value);    ///< #BFFD
    uint8_t ReadData();               ///< #FFFD read
    /// #BFF5 read (psg_d_o_reg_i, ym2149.vhd 220-221): the selected chip's identity and register address, `AY_ID & '0' & addr`
    /// (the chips answer "11", "10" and "01" for chip 0, 1 and 2)
    uint8_t ReadIdentity() const;
    /// The Pi's I2S audio as audio_mixer.vhd adds it (10 bit unsigned, #200 silence)
    void SetPiAudio(unsigned left, unsigned right);
    /// endregion

    /// region <Configuration (NextREG)>
    /// NR #06 bits 1:0: the chip type is bit 0 (0 YM, 1 AY: aymode_i is nr_06_psg_mode(0)), 11 holds all three chips in reset
    /// (audio_ay_reset, zxnext.vhd 6379); NR #08 bit 1 turbosound, bit 3 DAC, bit 5 ACB; NR #09 bits 7:5 mono per chip
    /// speakerExcluded: NR #06 bit 6 with NR #08 bit 4 - the beeper leaves the line mix (it only goes to the internal speaker)
    void Configure(bool ymMode, bool turboSound, bool dacEnabled, bool acb, uint8_t monoMask, bool speakerExcluded, bool held = false);
    bool Held() const { return _held; }
    /// The ULA beeper at the Next's levels: EAR 2048, MIC 512 (of 32768, audio_mixer.vhd); the standard beeper's circuit levels come back with it gone
    void ApplyBeeperLevels();
    void ReleaseBeeperLevels();
    /// endregion

    /// region <DAC>
    /// Channel 0-3 = A-D, unsigned 8-bit (#80 silence); left = A + B, right = C + D
    void WriteDac(unsigned channel, uint8_t value);
    uint8_t Dac(unsigned channel) const { return _dac[channel & 3]; }
    /// endregion

    unsigned SelectedChip() const { return _selected; }
    SoundChip_AY8910* Chip(unsigned index) const { return index < kChips ? _chips[index].get() : nullptr; }
    uint8_t PanMask(unsigned chip) const { return _pan[chip % kChips]; }  ///< bit 1 left, bit 0 right
    bool TurboSound() const { return _turboSound; }
    bool DacEnabled() const { return _dacEnabled; }
    /// The configuration as NR #06 / #08 / #09 left it (the report): YM or AY chips, ACB or ABC stereo, the chips mixed mono, the beeper
    /// kept out of the line mix
    bool YmMode() const { return _ym; }
    bool Acb() const { return _acb; }
    uint8_t MonoMask() const { return _monoMask; }
    bool SpeakerExcluded() const { return _speakerExcluded; }

    /// region <IModelAudioSource>
    const char* AudioSourceName() const override { return "Next audio"; }
    void AudioFrameStart(bool synthesisSuppressed) override;
    void AudioFrameEnd(size_t samples) override;
    int16_t* AudioBuffer() override { return _buffer; }
    bool AudioHadSoundLastFrame() const override { return _hadSound; }
    void AudioSetSampleRate(size_t rate) override { _rate = rate; }
    std::vector<std::pair<std::string, std::string>> AudioStateFields() const override;
    /// Parts: AY 1..3 (next_ay1..3) and the DAC (next_dac), each a mixer row; the AYs' channels A / B / C go to the
    /// chips (SoundChip_AY8910's channel mute / volume), the DAC's A..D are gains here
    size_t AudioPartCount() const override { return kParts; }
    AudioSourceType AudioPartType(size_t part) const override;
    std::string AudioPartName(size_t part) const override;
    void SetAudioPartGain(size_t part, float gain) override;
    float AudioPartPeak(size_t part) const override { return part < kParts ? _partPeak[part] : 0.0f; }
    bool AudioPartActive(size_t part) const override { return part < kParts && _partPeak[part] > 0.002f; }
    void AudioChannels(AudioSourceType type, std::vector<MixerChannelDesc>& out) const override;
    void SetAudioChannel(AudioSourceType type, size_t index, bool muted, float volume) override;
    /// The DAC part alone (the haptic feed's `next-dac`): the four channels' mono sum at full scale, before the gains
    void SetAudioPartTap(size_t part, bool on) override;
    const int16_t* AudioPartTap(size_t part) const override;
    /// endregion

    /// Off: every tick through Tick (comparisons); the count of the ticks the quiet runs took (AdvanceTo)
    void SetQuietTicksEnabled(bool enabled) { _quietTicksEnabled = enabled; }
    uint64_t QuietTicks() const { return _quietTicks; }

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
    /// Run the generators up to absolute base T `t` (a tick every 16 T), recording the level of each tick
    void AdvanceTo(double t);
    void ResetChips();
    void Tick();
    /// `ticks` Tick() calls of three chips that are silentAtRest, with the DC filter at its fixed point for their silence,
    /// and true; false (nothing done) otherwise. Every chip level is then 0 and stays 0, so every tick has the one level
    /// the DC filter, the DAC and the Pi give, worked out once; the generators advance (SoundChip_AY8910::advanceSilent)
    bool QuietTicks(size_t ticks);

    EmulatorContext* _context;
    std::array<std::unique_ptr<SoundChip_AY8910>, kChips> _chips;
    std::array<uint8_t, kChips> _pan{{3, 3, 3}};
    unsigned _selected = 0;
    bool _turboSound = true;
    bool _dacEnabled = true;
    bool _ym = true;
    bool _acb = false;
    bool _speakerExcluded = false;
    bool _held = false;  ///< NR #06 bits 1:0 = 11: the chips are in reset
    unsigned _piLeft = 0x200, _piRight = 0x200;
    std::array<uint8_t, kChips> _addr{{0, 0, 0}};  ///< the selected register of each chip, five bits (ym2149.vhd `addr`)
    uint8_t _monoMask = 0;
    std::array<uint8_t, 4> _dac{{0x80, 0x80, 0x80, 0x80}};

    std::function<double()> _now;
    double _dcLeft = 0, _dcRight = 0;
    double _tickT = 0;         ///< absolute base T of the next generator tick
    double _frameStartT = 0;
    bool _timeKnown = false;
    bool _suppressed = false;
    bool _hadSound = false;
    size_t _rate = AUDIO_SAMPLING_RATE;
    std::vector<float> _tickLeft, _tickRight;  ///< the level of each tick of this frame
    /// The DAC tap (SetAudioPartTap): its mono level per tick, then per sample (integers: the DAC's levels are)
    bool _dacTap = false;
    std::vector<int32_t> _tickDac;
    std::array<int16_t, MAX_SAMPLES_PER_FRAME> _dacTapBuffer{};
    int32_t DacMono() const
    {
        return ((_dac[0] - 128) + (_dac[1] - 128) + (_dac[2] - 128) + (_dac[3] - 128)) * 8;
    }
    bool _quietTicksEnabled = true;
    uint64_t _quietTicks = 0;

    /// region <The mixer's parts and channels (host-side, not state)>
    /// The DAC's two sides with the channel gains (the hardware's sum while every gain is 1)
    double DacSide(bool right) const;
    void UpdateDacGains();
    std::array<double, kParts> _partGain{{1.0, 1.0, 1.0, 1.0}};
    bool _chipGainActive = false;                    ///< a chip's part gain is not 1
    std::array<double, 4> _dacChannelGain{{1.0, 1.0, 1.0, 1.0}};  ///< the mixer's channel volume of DAC A..D
    std::array<double, 4> _dacGain{{1.0, 1.0, 1.0, 1.0}};         ///< channel x part gain, applied per tick
    bool _dacGainActive = false;
    /// Per chip this frame, before its gain: the smallest and largest level of each side (a meter of the swing, the
    /// unsigned DC left out), and the DAC's largest side
    std::array<double, kChips * 4> _meter{};
    double _dacMeter = 0;
    std::array<float, kParts> _partPeak{};
    void ResetMeters();
    /// endregion
    AudioFrameDescriptor _audioDescriptor;
    int16_t* const _buffer = reinterpret_cast<int16_t*>(_audioDescriptor.memoryBuffer);
};
