#pragma once

/// @file ttdvdac2.h
/// @brief TTD serializers for the TS-Conf VDAC2 card's FT812
/// (docs/inprogress/2026-10-01-tsconf-vdac2/line-budget-metrics.md §3.4).
///
/// Two blobs, restored in id order:
///   - PeripheralId::Vdac2Memory (42): the chip's memory regions, whole (RAM_G
///     1 MB, both display lists, REG, CMD, SPECIAL, INFLIGHT). Until TTD v2
///     memory regions exist (only changed pages stored) it is in every checkpoint,
///     as the classic General Sound card's RAM is;
///   - PeripheralId::Vdac2 (43): the card's time, its INT edge queue, what the
///     monitor shows, and the chip's control state (EveSaveState, with the line
///     budget metrics of the last finished frame).
/// The FT812 picture is not state: TTD composes it by replaying (the card is a
/// display participant, ttddisplayparticipant.h).

#include "debugger/ttd/engine/ttdregiontracker.h"
#include "debugger/ttd/ttdserializable.h"

class Vdac2Card;

namespace ttd
{

/// Also the time-travel engine's region source: the chip's seven memory
/// regions, written pieces found from eve-emu's own dirty bitmap (one bit per
/// 4 KB page, set by every chip write path) at each capture
class TTDVdac2Memory : public TTDSerializable, public ITTDRegionSource
{
public:
    explicit TTDVdac2Memory(Vdac2Card& card) : _card(card) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Vdac2Memory"; }
    /// Zero runs dropped: an empty chip saves a few tokens, not 2 MB
    bool TTDVariableSize() const override { return true; }
    void TTDSaveStateTo(std::vector<uint8_t>& out) const override;
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Vdac2Memory; }
    uint64_t TTDHashState() const override;

    void TTDRegions(std::vector<TTDDeviceRegion>& out) override;
    void TTDArmRegions(bool on) override;
    void TTDBeforeCapture() override;
    /// The engine keeps the whole blob in its regions: the state left is the
    /// blob's header with no tokens (magic, 0 bytes)
    bool TTDStateWithoutRegions(uint8_t& peripheralId, std::vector<uint8_t>& state) const override;

private:
    static constexpr size_t kMaxRegions = 7;   ///< RAM_G, DL0, DL1, REG, CMD, SPECIAL, INFLIGHT
    Vdac2Card& _card;
    TTDRegionTracker _trackers[kMaxRegions];
    size_t _regionCount = 0;
    bool _armed = false;
};

class TTDVdac2 : public TTDSerializable
{
public:
    explicit TTDVdac2(Vdac2Card& card) : _card(card) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "Vdac2"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::Vdac2; }
    uint64_t TTDHashState() const override;

private:
    Vdac2Card& _card;
};

}  // namespace ttd
