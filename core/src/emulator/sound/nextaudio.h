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

    explicit NextAudio(EmulatorContext* context);
    ~NextAudio() override = default;

    /// Absolute base T-state (3.5 MHz units, fractions allowed) of the CPU now
    void SetTimeSource(std::function<double()> now) { _now = std::move(now); }
    void Reset();

    /// region <Ports>
    void WriteSelect(uint8_t value);  ///< #FFFD
    void WriteData(uint8_t value);    ///< #BFFD
    uint8_t ReadData();               ///< #FFFD read
    /// endregion

    /// region <Configuration (NextREG)>
    /// NR #06 bits 1:0: 00 YM, 01 AY; NR #08 bit 1 turbosound, bit 3 DAC, bit 5 ACB; NR #09 bits 7:5 mono per chip
    void Configure(bool ymMode, bool turboSound, bool dacEnabled, bool acb, uint8_t monoMask);
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

    /// region <IModelAudioSource>
    const char* AudioSourceName() const override { return "Next audio"; }
    void AudioFrameStart(bool synthesisSuppressed) override;
    void AudioFrameEnd(size_t samples) override;
    int16_t* AudioBuffer() override { return _buffer; }
    bool AudioHadSoundLastFrame() const override { return _hadSound; }
    void AudioSetSampleRate(size_t rate) override { _rate = rate; }
    std::vector<std::pair<std::string, std::string>> AudioStateFields() const override;
    /// endregion

private:
    /// Run the generators up to absolute base T `t` (a tick every 16 T), recording the level of each tick
    void AdvanceTo(double t);
    void Tick();

    EmulatorContext* _context;
    std::array<std::unique_ptr<SoundChip_AY8910>, kChips> _chips;
    std::array<uint8_t, kChips> _pan{{3, 3, 3}};
    unsigned _selected = 0;
    bool _turboSound = true;
    bool _dacEnabled = true;
    bool _ym = true;
    bool _acb = false;
    uint8_t _monoMask = 0;
    std::array<uint8_t, 4> _dac{{0x80, 0x80, 0x80, 0x80}};

    std::function<double()> _now;
    double _tickT = 0;         ///< absolute base T of the next generator tick
    double _frameStartT = 0;
    bool _timeKnown = false;
    bool _suppressed = false;
    bool _hadSound = false;
    size_t _rate = AUDIO_SAMPLING_RATE;
    std::vector<float> _tickLeft, _tickRight;  ///< the level of each tick of this frame
    AudioFrameDescriptor _audioDescriptor;
    int16_t* const _buffer = reinterpret_cast<int16_t*>(_audioDescriptor.memoryBuffer);
};
