#pragma once

// The Fuller Box (Fuller Micro Systems, 1983) in a slot: an AY-3-8912 and an Atari-style joystick socket on the
// Sinclair edge (docs/inprogress/2026-10-03-zx-bus-slots/research-cards.md "Fuller Box", controller manager
// retro-devices.md §1). No schematic survives; the ports are the emulators' (FUSE, zxsp, MAME, nocash):
//
//   #3F  write: AY register select; read: AY register data (FUSE, zxsp, nocash; MAME and Kio's notes read #5F)
//   #5F  write: AY register data
//   #7F  read: the stick, F---RLDU active low, bits 6-4 high
//
// The card sees A7-A5 (001 / 010 / 011); how many more lines it decodes is the `decode` option of its claims (the full
// low byte by default, FUSE; A7-A5 only, zxsp). No IORQGE, no /DOS gating: on a Beta-128 machine TR-DOS's #3F / #5F /
// #7F cycles reach it too. The AY clock is MAME's 3.579545 MHz / 2 (a colour-burst crystal, unverified; zxsp guesses
// 1.7 MHz). Mono output: one mixer row, an AY row (voicing and character chain as for the AY socket).
//
// The stick is the controller manager's connector `fuller` (ICard::Connectors): its held controls are journaled as
// ConnectorState and saved in the Controllers blob; the card only converts them to the port byte.
//
// Time. The card's axis is the MultiSound adapter's: origin + AudioTstate(t) x the host speed multiplier, at
// CPU_CLOCK_RATE per second, the origin moving by the frame length x the multiplier at each frame end. The generators
// tick at the AY clock / 8; a register write first runs them up to its time, so it lands at the T-state it happens
// in. Each frame end turns the frame's tick levels into the mixer's samples (an average per sample).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "emulator/slots/card.h"
#include "emulator/sound/audio.h"

class EmulatorContext;
class SoundChip_AY8910;
class FullerBoxTtd;

class FullerBoxCard : public ICard
{
public:
    static constexpr const char* kCardId = "fuller-box";
    /// MAME fuller.cpp: AY8912 at 3.579545 MHz / 2 ("unverified clock")
    static constexpr double kAyClockHz = 3579545.0 / 2.0;
    static constexpr uint8_t kPortSelect = 0x3F;
    static constexpr uint8_t kPortData = 0x5F;
    static constexpr uint8_t kPortStick = 0x7F;

    /// CardType::create
    static std::unique_ptr<ICard> Create(const CardContext& context);

    explicit FullerBoxCard(const CardContext& context);
    ~FullerBoxCard() override;

    /// The controller bits (kRight ... kFire) as the #7F byte: F---RLDU, active low, bits 6-4 high
    static uint8_t StickByte(uint16_t controls);

    /// region <PortDevice>
    uint8_t portDeviceInMethod(uint16_t port) override;
    void portDeviceOutMethod(uint16_t port, uint8_t value) override;
    uint8_t portDeviceReadCycle(uint16_t port, bool& drives) override;
    /// endregion </PortDevice>

    /// region <ICard>
    uint8_t Peek(uint16_t port, bool& drives) const override;
    void BusReset() override;
    void FrameStart() override;
    void FrameEnd(size_t samples) override;
    void SetOutputRate(uint32_t rate) override;
    void MixerRows(std::vector<CardMixerRow>& out) const override;
    const int16_t* MixerBuffer(AudioSourceType type) const override;
    int16_t* SsgMixerBuffer(AudioSourceType type) override;
    /// ay3.a / .b / .c: the chip's channel mute and volume
    void MixerChannels(AudioSourceType type, std::vector<MixerChannelDesc>& out) const override;
    void SetMixerChannel(AudioSourceType type, size_t index, bool muted, float volume) override;
    void Connectors(std::vector<CardConnector>& out) const override;
    /// Time travel: one blob, the card's time base and its AY (FullerBoxTtd)
    void CollectTtdDevices(std::vector<CardTtdDevice>& out) override;
    /// endregion </ICard>

    SoundChip_AY8910& Chip()
    {
        return *_chip;
    }

    /// region <Time travel state (FullerBoxTtd)>
    uint64_t Origin() const
    {
        return _origin;
    }
    uint64_t LastTime() const
    {
        return _last;
    }
    double TickPosition() const
    {
        return _tickPos;
    }
    void RestoreTime(uint64_t origin, uint64_t last, double tickPos);
    /// The mixer's frame-start sample phase (SoundManager::samplePhase); see MultiSoundSlotCard::MixerSamplePhase
    uint64_t MixerSamplePhase() const;
    void AdoptMixerSamplePhase(uint64_t phase);
    /// endregion </Time travel state>

private:
    uint64_t FrameDuration() const;
    /// The card axis now (monotonic)
    uint64_t Now();
    /// Run the generators up to card time @p t, keeping each tick's level
    void AdvanceTo(uint64_t t);

    EmulatorContext* _context;
    std::unique_ptr<SoundChip_AY8910> _chip;
    std::unique_ptr<FullerBoxTtd> _ttd;
    uint64_t _origin = 0;
    uint64_t _last = 0;
    double _tickPos = 0;  ///< card time of the next generator tick
    std::vector<float> _ticks;  ///< this frame's tick levels
    int16_t _buffer[MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS] = {};
};
