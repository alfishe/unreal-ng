// libsam2695 - the chip core: the MIDI implementation (parts, RPN / NRPN, GM / GS / Dream SysEx), the
// voice pool and its allocation, the effects block.
//
// Works in control blocks of kControlBlock internal samples: BeginBlock() re-targets every voice for
// the block, Message() applies a MIDI message at a sample offset inside the block (a note starts or
// releases exactly there), RenderSegment() mixes the voices between two offsets into the GM bus and
// the effect sends, FinishBlock() runs the effects and the output stage.
#pragma once

#include "channel/channel.h"
#include "fx/effects.h"
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
    void Configure(const SynthConfig& cfg);
    void SetBank(const ISoundBank* bank) { _bank = bank; }

    void PowerOn();

    void BeginBlock();
    // Applies a message at `offset` samples into the current block. Returns true when the message
    // was a "reset all" (NRPN 375Fh = 45h) that stops the firmware for kResetBusyMs.
    bool Message(const MidiMessage& m, uint32_t offset);
    void RenderSegment(uint32_t from, uint32_t to, FxBuses& buses);
    void FinishBlock(FxBuses& buses, float* outL, float* outR);

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
        ar(_rxNrpn);
        ar(_rxNrpnSelected);
        ar(_masterTune);
        ar(_keyShift);
        ar(_deviceId);
        ar(_drums);
        ar(_fx);
    }

private:
    const BankModel* Model() const { return _bank != nullptr ? &_bank->Model() : nullptr; }
    void ResolvePreset(Channel& ch);
    void ResetAll(bool fadeVoices);
    void GsReset();
    void PartMessage(uint8_t part, const MidiMessage& m, uint32_t offset);
    void NoteOn(uint8_t part, uint8_t key, uint8_t vel, uint32_t offset);
    void NoteOff(uint8_t part, uint8_t key, uint32_t offset);
    void ControlChange(uint8_t part, uint8_t cc, uint8_t value, uint32_t offset);
    void DataEntry(uint8_t part, bool msb, uint8_t value);
    void PartNrpnWrite(uint8_t part, uint16_t number, uint8_t value);
    bool ChipNrpn(uint8_t index, uint8_t value);
    bool SysEx(const uint8_t* d, uint16_t n);
    void GsWrite(uint32_t address, uint8_t value);
    bool DeviceAccepted(uint8_t device, bool universal) const;
    void SetEffectsWord(uint8_t word);
    void ReleaseHeld(uint8_t part, uint32_t offset);
    void EnforceLimit();
    Voice* Allocate(uint64_t currentNote, int part);
    int ChooseVictim(uint64_t currentNote, int part) const;
    VoiceContext ContextFor(const Channel& ch) const;
    float MasterTuneCents() const;
    void TouchAllParts();
    void Retarget(Voice& v, uint32_t offset, bool first);

    SynthConfig _cfg;
    const ISoundBank* _bank = nullptr;
    std::array<Channel, 16> _channels{};          // the 16 GS parts
    std::array<Voice, kVoiceSlots> _voices{};
    std::array<bool, 16> _mute{};
    uint8_t _effectsWord = kPowerUpEffectsWord;
    uint64_t _noteCounter = 0;
    uint64_t _voicesStolen = 0;
    uint64_t _notesDropped = 0;
    // chip-level MIDI state
    std::array<uint16_t, 16> _rxNrpn{};           // NRPN number per MIDI channel, for the chip's 37xxh
    std::array<bool, 16> _rxNrpnSelected{};
    std::array<uint8_t, 4> _masterTune{};         // GS 40 00 00-03, nibbles (default 00 04 00 00 = 0 cents)
    uint8_t _keyShift = 0x40;                     // GS 40 00 05
    uint8_t _deviceId = 0x20;                     // NRPN 3757h, 20h = every device ID accepted
    std::array<DrumTable, 2> _drums{};            // drum edits: [0] received on channel 10, [1] the others
    Effects _fx;
};

} // namespace sam2695
