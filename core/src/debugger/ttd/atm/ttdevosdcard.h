#pragma once

/// @file ttdevosdcard.h
/// @brief TTD serializer for the ZX-Evo SD card: the Z-Controller's chip
/// select and receive latch, and the card's SPI protocol state (command
/// bytes, queued response, multi-block position).
///
/// The card's sectors are not here. Under the storage rule of the media
/// manager (docs/inprogress/2026-09-28-storage-manager/integration-ttd-
/// snapshots.md §2) the media set is fixed while a recording runs and every
/// guest write is a replay barrier, so between barriers the medium does not
/// change and replay only needs the protocol state to read the same bytes.

#include "debugger/ttd/ttdserializable.h"

class PortDecoder_ATM3;

namespace ttd
{

class TTDEvoSdCard : public TTDSerializable
{
public:
    explicit TTDEvoSdCard(PortDecoder_ATM3& decoder) : _decoder(decoder) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "EvoSdCard"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::EvoSdCard; }
    uint64_t TTDHashState() const override;

private:
    PortDecoder_ATM3& _decoder;
};

}  // namespace ttd
