#pragma once

/// @file ttdtsconfstate.h
/// @brief TTD serializer for the TS-Conf machine state (PeripheralId::TsConfPaging,
/// TSConf technical-design §3.13).
///
/// The blob is TsConfState itself: plain fixed-width fields, no padding
/// (static_assert in tsconfstate.h), so save / load are one copy. After a
/// load the decoder re-derives what follows from the state (banks, M1 hook,
/// FM window and cache overlays, CPU clock, video page).

#include "debugger/ttd/ttdserializable.h"

class PortDecoder_TSConf;

namespace ttd {

class TTDTsConfState : public TTDSerializable
{
public:
    explicit TTDTsConfState(PortDecoder_TSConf& decoder) : _decoder(decoder) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "TsConfState"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::TsConfPaging; }
    /// FNV-1a over the blob
    uint64_t TTDHashState() const override;

private:
    PortDecoder_TSConf& _decoder;
};

}  // namespace ttd
