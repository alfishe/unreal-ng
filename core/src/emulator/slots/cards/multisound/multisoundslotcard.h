#pragma once

// ZX-MultiSound in a slot: the thin ICard adapter around MultiSoundCard (docs/inprogress/2026-10-03-zx-multisound/
// tdd-integration.md §3.1). One bus call is one card call; the adapter owns the only state the card does not, the
// absolute time base, and translates the slot's options and the machine's rates into the card's configuration.
//
// Time. The machine's T-state counter is frame-relative and rebased by the frame length at every frame end; the card
// wants absolute, monotonic ticks. The adapter's axis is the one SoundChip_Moonsound uses: origin + AudioTstate(t) x
// the host speed multiplier, at CPU_CLOCK_RATE ticks per second (the axis every sound device and the mixer's sample
// count share), the origin moving by the frame length x the multiplier at each frame end. The card's FrameStart /
// FrameEnd get the frame's start and nominal end on that axis.
//
// ROM-fetch lock. Before every bus cycle the adapter hands the card the M1 address of the IN / OUT instruction (the
// last opcode fetch before the I/O cycle), so the CPLD's rom_m1_access latch sees what the claim table's lock sees.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "emulator/slots/card.h"
#include "emulator/slots/cards/multisound/multisoundcard.h"
#include "emulator/slots/cards/multisound/multisoundttd.h"

class EmulatorContext;

class MultiSoundSlotCard : public ICard
{
public:
    /// CardType::create
    static std::unique_ptr<ICard> Create(const CardContext& context);

    /// The card's options from the slot's: dip (ym, saa, gs, sd), gsRam (1m, 2m), ctrlMask (pro, classic)
    static MultiSoundOptions OptionsFrom(const slots::CardDef& def, const slots::CardOptions& options);

    /// The card's configuration for a machine: options, the host tick rate (CPU_CLOCK_RATE), the mixer's core rate,
    /// the bank ([MIDI] Bank=, else the card's default)
    static MultiSoundCardConfig ConfigFrom(const CardContext& context);

    MultiSoundSlotCard(const CardContext& context, const MultiSoundCardConfig& config);
    ~MultiSoundSlotCard() override;

    /// region <PortDevice: bus cycles at the machine's now>
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;
    uint8_t portDeviceReadCycle(uint16_t port, bool& drives) override;
    /// endregion </PortDevice>

    /// region <ICard>
    uint8_t Peek(uint16_t port, bool& drives) const override;
    void BusReset() override;
    bool MidiPanic() override;
    void FrameStart() override;
    void FrameEnd(size_t samples) override;
    void SetOutputRate(uint32_t rate) override;
    void MixerRows(std::vector<CardMixerRow>& out) const override;
    int16_t* VoicedMixerBuffer(AudioSourceType type) override;
    uint64_t RenderEpoch() const override { return _card.RenderEpoch(); }
    bool SetFmTrimDb(double db) override;
    bool FmTrimDb(double& db) const override;
    const int16_t* MixerBuffer(AudioSourceType type) const override;
    bool WantsWideMix() const override
    {
        return true;
    }

    /// Time travel (multisoundttd.h): the card, its SAA1099, SAM2695 and General Sound, named by the slot
    void CollectTtdDevices(std::vector<CardTtdDevice>& out) override;
    /// `slots.<slot>.bank`: the MIDI bank's SHA-256 folded to 64 bits (0: no bank)
    void TtdFingerprint(std::vector<std::pair<std::string, uint64_t>>& out) const override;
    /// A session recorded with another MIDI bank is refused (the synthesizer's blob names its bank)
    bool TtdSessionMatches(const std::unordered_map<uint8_t, std::vector<uint8_t>>& blobs,
                           std::string& why) const override;
    /// endregion </ICard>

    MultiSoundCard& Card()
    {
        return _card;
    }
    const MultiSoundCard& Card() const
    {
        return _card;
    }

    /// The card axis now (monotonic)
    uint64_t Now();
    /// The absolute card time of the current frame's T-state 0
    uint64_t Origin() const
    {
        return _origin;
    }
    /// The latest card time handed out (Now is monotonic)
    uint64_t LastTime() const
    {
        return _last;
    }
    /// The card axis at the CPU's position, without moving the monotonic clamp (time-travel checks)
    uint64_t Position() const;
    /// A time-travel restore puts the time base back
    void RestoreTime(uint64_t origin, uint64_t last)
    {
        _origin = origin;
        _last = last;
    }
    /// The mixer's frame-start sample phase (SoundManager::samplePhase): how many samples the frames have, so how
    /// many the card renders. Part of the card's blob: a machine without a board AY device (a ZX-Evo, whose YM2149
    /// the card takes out of its socket) has nothing else that puts it back after a seek
    uint64_t MixerSamplePhase() const;
    void AdoptMixerSamplePhase(uint64_t phase);

    /// The time-travel devices (tests)
    MultiSoundCardTtd& CardTtd()
    {
        return *_cardTtd;
    }
    Sam2695Ttd& SynthTtd()
    {
        return *_synthTtd;
    }

private:
    uint64_t FrameDuration() const;
    void TrackM1();

    EmulatorContext* _context;
    MultiSoundCard _card;
    uint64_t _origin = 0;
    uint64_t _last = 0;
    std::unique_ptr<MultiSoundCardTtd> _cardTtd;
    std::unique_ptr<Sam2695Ttd> _synthTtd;
};
