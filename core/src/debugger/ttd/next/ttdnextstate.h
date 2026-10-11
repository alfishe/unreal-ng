#pragma once

/// @file ttdnextstate.h
/// @brief TTD serializer for the ZX Spectrum Next (PeripheralId::NextState): one image of NextStateCodec, so a seek puts the NextREG file,
/// the memory map, the palettes, sprites, copper, DMA, CTC, interrupt controller, I2C RTC, Multiface and DivMMC back to the checkpoint.
/// Without it a restore left all of those at their values from the moment of the seek (the guest RAM came back, its machine did not).

#include "debugger/ttd/ttdserializable.h"

class PortDecoder_Next;
class Memory;

namespace ttd {

class TTDNextState : public TTDSerializable
{
public:
    explicit TTDNextState(PortDecoder_Next& decoder);

    size_t TTDStateSize() const override { return _size; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "NextState"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::NextState; }
    /// FNV-1a over the image
    uint64_t TTDHashState() const override;

private:
    PortDecoder_Next& _decoder;
    size_t _size;  ///< the image has a fixed length for the life of the machine
};

/// The DivMMC RAM (system-area pages 8-15, 128 KB): esxDOS and NextZXOS keep their work there. It is not a RAM page of the page store, so it has
/// its own blob, the plain bytes (an engine region once the Next is in the v2 engine)
class TTDNextDivMmcRam : public TTDSerializable
{
public:
    static constexpr unsigned kFirstPage = 8;   ///< NextMemory::DivMmcRamBank: SRAM #020000, system area pages 8-15
    static constexpr unsigned kPages = 8;
    static constexpr size_t kBytes = kPages * 16384;

    explicit TTDNextDivMmcRam(Memory& memory) : _memory(memory) {}

    size_t TTDStateSize() const override { return kBytes; }
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "NextDivMmcRam"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::NextDivMmcRam; }
    uint64_t TTDHashState() const override;

private:
    Memory& _memory;
};

}  // namespace ttd

