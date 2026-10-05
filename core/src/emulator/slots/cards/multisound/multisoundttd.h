#pragma once

// The ZX-MultiSound's time-travel devices (docs/inprogress/2026-10-03-zx-multisound/tdd-integration.md §4, MS-5).
// The card's state is in four blobs, each a device of its own in the engine's table, named by the card's slot:
//   MultiSound   (58) "<slot>.multisound"           the adapter's time base + MultiSoundCard (CPLD, YM2203 pair, MIDI
//                                                    line, DACs)
//   Saa1099      (53) "<slot>.multisound.saa1099"   the SAA1099 itself (Saa1099 is a TTDSerializable)
//   Sam2695      (59) "<slot>.multisound.sam2695"   the General MIDI synthesizer (Sam2695Ttd below)
//   MultiSoundGs (60) "<slot>.multisound.gs"        the board's General Sound (SoundChip_GeneralSound, its GS profile
//                                                    names the id and the RAM region MultiSoundGsRam)
// A v1 checkpoint holds one blob per id, so one card of this kind per machine: the slot planner refuses a second one
// (its functions clash), and a GS card next to it keeps the GS card's own id 5.

#include <cstddef>
#include <cstdint>
#include <string>

#include "debugger/ttd/ttdserializable.h"

class MultiSoundSlotCard;

namespace sam2695
{
class Synth;
}

/// The card's own blob: u1 version, u8 adapter origin, u8 adapter last time, then MultiSoundCard::TtdSave
class MultiSoundCardTtd final : public ttd::TTDSerializable
{
public:
    static constexpr uint8_t kVersion = 1;
    static constexpr uint16_t kCardOffset = 1 + 8 + 8;

    explicit MultiSoundCardTtd(MultiSoundSlotCard& card) : _card(card)
    {
    }

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override
    {
        return "MultiSound";
    }
    ttd::PeripheralId TTDPeripheralId() const override
    {
        return ttd::PeripheralId::MultiSound;
    }
    uint64_t TTDHashState() const override;
    ttd::TTDDeviceDescriptor TTDDescribe() const override;
    /// The YM2203 pair has caught up with the CPU's position on the card axis
    bool TTDSyncedTime(int64_t& offset) const override;

private:
    MultiSoundSlotCard& _card;
};

/// The SAM2695's blob: sam2695::Synth::SaveState (it names its bank by SHA-256 and a load refuses another bank)
class Sam2695Ttd final : public ttd::TTDSerializable
{
public:
    explicit Sam2695Ttd(sam2695::Synth& synth) : _synth(synth)
    {
    }

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override
    {
        return "Sam2695";
    }
    ttd::PeripheralId TTDPeripheralId() const override
    {
        return ttd::PeripheralId::Sam2695;
    }
    uint64_t TTDHashState() const override;
    /// The bank is not recorded: its SHA-256 (folded to 64 bits) is the device's firmware fingerprint
    ttd::TTDDeviceDescriptor TTDDescribe() const override;

    /// The loaded bank's SHA-256 folded to 64 bits (FNV-1a over the digest); 0 without a bank
    static uint64_t BankFingerprint(const sam2695::Synth& synth);
    /// Loads refused because the blob named another bank (or had another layout)
    uint64_t RefusedLoads() const
    {
        return _refused;
    }

private:
    sam2695::Synth& _synth;
    uint64_t _refused = 0;
};
