#pragma once

/// @file p2666audio.h
/// @brief The Pentagon 2.666 LE's sound (hardware-reference.md §5, PaE.vhd:1300-1330, 1399-1432,
/// common/YM2149/YM2149_linmix.vhd): two YM2149 cores in the FPGA, the Covox #FB and the beeper summed per side into
/// one 8-bit DAC word that wraps (D-11: the board as built). A model audio source owned by the port decoder, as the
/// Next's sound is; the Spectrum 128 TurboSound and the standard beeper stay silent on this machine.
///
/// Mix (PaE.vhd:1407-1415): right = A0 + B0 + A1 + B1 + covox, left = B0 + C0 + B1 + C1 + covox, each + 50 when one of
/// FE bit 4 (beeper) / bit 3 (tape out) is set, + 100 when both; std_logic_vector(7 downto 0) keeps the low 8 bits.
/// A channel is the linmix DAC table of its 5-bit amplitude (0..#3F).
///
/// Ports (the decoder's parallel decode): A15..14 = 11, A1..0 = 01 write: the selected chip's address (#FF selects
/// chip 0, #FE chip 1 after it); A15..14 = 10, A1..0 = 01 write: data; A15..13 = 111, A1..0 = 01 read. The register
/// address is eight bits of which the chip uses four: #10 is register 0 again; registers 14 and 15 do not exist (a
/// write is lost, a read leaves the last value read on the bus, O_DA).
///
/// Clock: ym_clk_ena = mainclk / 31, the tone and envelope counters step once in 16 of those (ena_div): a generator
/// tick every 496 mainclk = 15.5 base T, an AY clock of 114 MHz / 62.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/modelaudiosource.h"

class EmulatorContext;

class P2666Audio : public IModelAudioSource
{
public:
    static constexpr unsigned kChips = 2;
    static constexpr size_t kParts = 4;     ///< YM 0, YM 1, Covox, beeper
    static constexpr double kTickT = 15.5;  ///< a generator tick in base T
    /// YM2149_linmix.vhd: the DAC word of a 5-bit amplitude
    static constexpr uint8_t kLinmix[32] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                                            0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x09, 0x0B,
                                            0x0D, 0x0F, 0x12, 0x16, 0x1A, 0x1F, 0x25, 0x2C, 0x35, 0x3F};

    explicit P2666Audio(EmulatorContext* context);
    ~P2666Audio() override;

    /// Absolute base T (fractions allowed) of the CPU now
    void SetTimeSource(std::function<double()> now) { _now = std::move(now); }
    /// CPU reset (RESET_L = cpu_res): both register files and address latches 0, chip 0, O_DA 0, Covox 0
    void Reset();

    /// region <Ports>
    void WriteAddress(uint8_t value);  ///< A15..14 = 11, A1..0 = 01
    void WriteData(uint8_t value);     ///< A15..14 = 10, A1..0 = 01
    uint8_t ReadData();                ///< A15..13 = 111, A1..0 = 01
    void WriteCovox(uint8_t value);
    /// OUT (#FE): bits 4 (beeper) and 3 (tape out)
    void WriteFe(uint8_t value);
    void SetHistory(AYHistory* history) { _history = history; }
    /// endregion

    /// Silence the standard beeper (its levels to 0) while this device mixes the beeper; release on detach
    void ApplyBeeperLevels();
    void ReleaseBeeperLevels();

    unsigned SelectedChip() const { return _selected; }
    SoundChip_AY8910* Chip(unsigned index) const { return index < kChips ? _chips[index].get() : nullptr; }
    uint8_t Covox() const { return _covox; }
    uint8_t Register(unsigned chip, unsigned reg) const { return _regs[chip % kChips][reg & 15]; }
    /// The 8-bit DAC words the last tick made (right, left)
    uint8_t LastRight() const { return _lastRight; }
    uint8_t LastLeft() const { return _lastLeft; }
    /// The DAC words of the state now (the mix of the chips' current amplitudes; no generator step)
    void MixNow(uint8_t& right, uint8_t& left);

    /// region <IModelAudioSource>
    const char* AudioSourceName() const override { return "Pentagon 2.666 sound"; }
    void AudioFrameStart(bool synthesisSuppressed) override;
    void AudioFrameEnd(size_t samples) override;
    int16_t* AudioBuffer() override { return _buffer; }
    bool AudioHadSoundLastFrame() const override { return _hadSound; }
    void AudioSetSampleRate(size_t rate) override { _rate = rate; }
    std::vector<std::pair<std::string, std::string>> AudioStateFields() const override;
    /// Parts on the standard rows: AY 1 / AY 2 (the TurboSound pair's), COVOX and the beeper
    size_t AudioPartCount() const override { return kParts; }
    AudioSourceType AudioPartType(size_t part) const override;
    std::string AudioPartName(size_t part) const override;
    void SetAudioPartGain(size_t part, float gain) override;
    float AudioPartPeak(size_t part) const override { return part < kParts ? _partPeak[part] : 0.0f; }
    bool AudioPartActive(size_t part) const override { return part < kParts && _partPeak[part] > 0.002f; }
    void AudioChannels(AudioSourceType type, std::vector<MixerChannelDesc>& out) const override;
    void SetAudioChannel(AudioSourceType type, size_t index, bool muted, float volume) override;
    void SetAudioPartTap(size_t part, bool on) override;
    const int16_t* AudioPartTap(size_t part) const override;
    /// endregion

private:
    friend struct P2666AudioCodec;
    void AdvanceTo(double t);
    void Tick();
    void Mix(bool step);
    void RecordWrite(uint16_t port, uint8_t value, uint8_t reg);
    int BeeperTerm() const;

    EmulatorContext* _context;
    std::array<std::unique_ptr<SoundChip_AY8910>, kChips> _chips;
    std::array<std::array<uint8_t, 16>, kChips> _regs{};  ///< reg (the VHDL register file, R0..R13)
    std::array<uint8_t, kChips> _addr{};                  ///< addr (eight bits)
    std::array<uint8_t, kChips> _oda{};                   ///< O_DA, the last value read
    unsigned _selected = 0;                               ///< ym_number
    uint8_t _covox = 0;
    uint8_t _fe = 0;
    AYHistory* _history = nullptr;

    std::function<double()> _now;
    double _dc[2] = {0, 0};  ///< right, left
    double _tickT = 0;
    double _frameStartT = 0;
    bool _timeKnown = false;
    bool _suppressed = false;
    bool _hadSound = false;
    size_t _rate = AUDIO_SAMPLING_RATE;
    uint8_t _lastRight = 0, _lastLeft = 0;
    std::vector<float> _tickLeft, _tickRight;

    /// region <The mixer's parts and channels (host-side, not state)>
    std::array<double, kParts> _partGain{{1.0, 1.0, 1.0, 1.0}};
    std::array<std::array<double, 3>, kChips> _channelGain{{{1.0, 1.0, 1.0}, {1.0, 1.0, 1.0}}};
    bool _gainActive = false;  ///< a gain is not 1: the sum is no longer the board's integers
    void UpdateGainActive();
    std::array<double, kParts> _meterMin{}, _meterMax{};
    std::array<float, kParts> _partPeak{};
    std::array<bool, kParts> _partTap{};
    bool _taps = false;
    std::array<std::vector<int32_t>, kParts> _tickTap;
    std::array<int64_t, kParts> _tapDc{};
    std::array<std::array<int16_t, MAX_SAMPLES_PER_FRAME>, kParts> _tapBuffer{};
    /// endregion

    AudioFrameDescriptor _audioDescriptor;
    int16_t* const _buffer = reinterpret_cast<int16_t*>(_audioDescriptor.memoryBuffer);
};
