// libsam2695 - the chip core: the MIDI implementation, channels, the voice pool and its allocation.
//
// Works in control blocks of kControlBlock internal samples: BeginBlock() re-targets every voice for
// the block, Message() applies a MIDI message at a sample offset inside the block (a note starts or
// releases exactly there), RenderSegment() mixes the voices between two offsets.
#pragma once

#include "channel/channel.h"
#include "midi/parser.h"
#include "sam2695/sam2695.h"
#include "voice/voice.h"

#include <array>
#include <cstdint>

namespace sam2695
{

class SynthCore
{
public:
    void Configure(const SynthConfig& cfg) { _cfg = cfg; }
    void SetBank(const ISoundBank* bank) { _bank = bank; }

    void PowerOn();

    void BeginBlock();
    // Applies a message at `offset` samples into the current block. Returns true when the message
    // was a "reset all" (NRPN 375Fh = 45h) that stops the firmware for kResetBusyMs.
    bool Message(const MidiMessage& m, uint32_t offset);
    void RenderSegment(uint32_t from, uint32_t to, float* left, float* right);

    void SetChannelMute(int ch, bool mute) { _mute[ch & 15] = mute; }
    void SetInterpolation(Interpolation mode) { _cfg.interpolation = mode; }
    uint32_t PolyphonyLimit() const;
    void Describe(SynthReport& out) const;
    void Sanitize();

    const Voice& VoiceAt(size_t i) const { return _voices[i]; }
    const Channel& ChannelAt(size_t i) const { return _channels[i]; }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(_channels);
        ar(_voices);
        ar(_effectsWord);
        ar(_noteCounter);
        ar(_voicesStolen);
        ar(_notesDropped);
    }

private:
    const BankModel* Model() const { return _bank != nullptr ? &_bank->Model() : nullptr; }
    void ResolvePreset(Channel& ch);
    void NoteOn(uint8_t chIndex, uint8_t key, uint8_t vel, uint32_t offset);
    void NoteOff(uint8_t chIndex, uint8_t key, uint32_t offset);
    void ControlChange(uint8_t chIndex, uint8_t cc, uint8_t value, uint32_t offset);
    void DataEntry(uint8_t chIndex, bool msb, uint8_t value);
    bool Nrpn(uint8_t chIndex, uint16_t number, uint8_t value);
    void SetEffectsWord(uint8_t word);
    void EnforceLimit();
    Voice* Allocate(uint64_t currentNote);
    int ChooseVictim(uint64_t currentNote) const;
    PitchContext PitchFor(const Channel& ch) const;
    void Retarget(Voice& v, uint32_t offset, bool first);

    SynthConfig _cfg;
    const ISoundBank* _bank = nullptr;
    std::array<Channel, 16> _channels{};
    std::array<Voice, kVoiceSlots> _voices{};
    std::array<bool, 16> _mute{};
    uint8_t _effectsWord = kPowerUpEffectsWord;
    uint64_t _noteCounter = 0;
    uint64_t _voicesStolen = 0;
    uint64_t _notesDropped = 0;
};

} // namespace sam2695
