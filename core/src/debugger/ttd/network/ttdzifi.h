#pragma once

/// @file ttdzifi.h
/// @brief TTD serializer for the TS AVR firmware's ZiFi API block (registers,
/// the data-register selector, the last-byte times of both rings). Its UART
/// and peer travel in the ZiFiLine blob, the RS-232 rings in the SerialPort
/// blob. A blob of another version is not loaded.
///
/// Variable size: the fixed ZiFi::State (32 bytes, all a blob had before Z3b),
/// then, when the line's peer is a ZIFI-NATIVE module, its file bridge
/// (ZiFiNativeModule::SaveBridge: the VFS client, the FTP server, the sockets
/// beyond the eighth; received bytes by journal reference). It loads after the
/// ZiFiLine blob (ascending ids), which brought the module itself back.

#include <vector>

#include "debugger/ttd/ttdserializable.h"

class EmulatorContext;

namespace ttd
{

class TTDZiFi : public TTDSerializable
{
public:
    explicit TTDZiFi(EmulatorContext* context) : _context(context) {}

    /// The largest blob: the state and a bridge with full rings and queued uploads
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    bool TTDVariableSize() const override { return true; }
    void TTDSaveStateTo(std::vector<uint8_t>& out) const override;
    std::string TTDDeviceName() const override { return "ZiFi"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::ZiFi; }
    uint64_t TTDHashState() const override;

private:
    EmulatorContext* _context = nullptr;
};

}  // namespace ttd
