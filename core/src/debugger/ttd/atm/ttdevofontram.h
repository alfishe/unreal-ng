#pragma once

/// @file ttdevofontram.h
/// @brief TTD serializer for the ZX-Evo text-mode font RAM: the 2 KB a program loads through `#BF` bit 2, and the
/// glyph byte `#0EBD` reads. The font is chipset state the paging blob does not carry (docs/inprogress/
/// 2026-09-15-atm-baseconf-highres-ports/tdd-e8-wrprot-font-pal444-dosstall.md section 4).

#include "debugger/ttd/ttdserializable.h"

class EmulatorContext;

namespace ttd
{

class TTDEvoFontRam : public TTDSerializable
{
public:
    explicit TTDEvoFontRam(EmulatorContext* context) : _context(context) {}

    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "EvoFontRam"; }
    PeripheralId TTDPeripheralId() const override { return PeripheralId::EvoFontRam; }
    uint64_t TTDHashState() const override;

private:
    EmulatorContext* _context;
};

}  // namespace ttd
