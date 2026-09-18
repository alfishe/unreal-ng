#pragma once

#include <stdafx.h>
#include "emulator/ports/models/portdecoder_pentagon512.h"

/// Port decoder for Pentagon 1024K model
/// Extends Pentagon 512K with port #EFF7 for hardware features and memory gating.
///
/// Port #EFF7 bit assignments (per Born Dead #10 article):
/// - Bit 0: a4b (attribute per byte) hardware multicolor
/// - Bit 1: 512x192 video mode
/// - Bit 2: Memory above 128K latch (0=present, 1=absent) - gates extended paging
/// - Bit 3: Read-only cache (proposed)
/// - Bit 4: GigaScreen (hardware overlay of screens 0 and 1)
/// - Bits 5-6: Reserved (Rom-Disk expansion)
/// - Bit 7: Gluk CMOS (real-time clock)
///
/// Memory paging (6-bit bank selection for 64 pages / 1024KB):
/// - When #EFF7 bit 2 = 0 (extended memory present):
///   - Bits [0:2] from #7FFD = pb0..pb2
///   - Bit 5 from #7FFD = pb5 (NOT the paging lock!)
///   - Bits [6:7] from #7FFD = pb3, pb4
///   - Total: 6 bits = 64 pages = 1024KB
/// - When #EFF7 bit 2 = 1: 3-bit bank only (128K mode), bit 5 is paging lock
///
/// Address decode for #EFF7: low byte = 0xF7 (A3=0 distinguishes from Beta128 #FF)
/// See: https://zxpress.ru/ru/ezines/born-dead/10/tehnicheskie-podrobnosti-kompyuterov-semeystva-pentagon-osobennosti-pentagon-1024-upravlenie
class PortDecoder_Pentagon1024 : public PortDecoder_Pentagon512
{
    /// region <Constructors / Destructors>
public:
    PortDecoder_Pentagon1024() = delete;
    PortDecoder_Pentagon1024(EmulatorContext* context);
    virtual ~PortDecoder_Pentagon1024() = default;
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    /// endregion </Interface methods>

protected:
    void switchRAMPage(uint8_t value) override;
    void Port_7FFD_Out(uint16_t port, uint8_t value, uint16_t pc);
    bool IsPort_EFF7(uint16_t port);
    void Port_EFF7_Out(uint16_t port, uint8_t value, uint16_t pc);

    /// region <Debug>
    std::string Dump_EFF7_value(uint8_t value);
    /// endregion </Debug>
};
