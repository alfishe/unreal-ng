#pragma once
#include "stdafx.h"

#include "portdecoder_atm710.h"

/// ATM Turbo 2 v4.50 ("ATM1") Port Decoder
///
/// The 4.50 board has no 7.10 register file (#xx77 / #xxF7 / #EFF7). Its
/// paging lives in two address-bus latches and one data latch:
/// - aFE: the LOW address byte of every #xxFE write. A7 = ROM at #0000
///   (0 = RAM page 0 / page 4 in windows 0 / 1), A6-A5 = video mode
///   (0 = EGA 320x200, 1 = 640x200 hi-res, 3 = ZX 256x192)
/// - aFB: the LOW address byte of a read with A2 = 0 that no other device
///   claimed. A7 = CPSYS (system ROM at #0000); the read returns #FF
/// - pFDFD (#FDFD group): bits 1-0 extend the #C000 RAM page (512 KiB),
///   bit 2 is ROM A16, bit 3 (CPNET) forces the system ROM inside a TR-DOS
///   session
/// - #7DFD group: palette write, ATM1 data layout --grbGRB
///
/// Write groups by A15 / A9 / A1 (port & 0x8202): #7DFD = 0x0000,
/// #7FFD = 0x0200, #FDFD = 0x8000, AY = 0x8200 - all disjoint.
///
/// No turbo: the 4.50 board has no software clock switch.
///
/// Hardware: "ATM Turbo - instruction and computer description" (MicroArt,
/// ~1992), appendix 2 port table and the Ver 4.50 schematic -
/// https://zxpress.ru/book.php?id=170
/// See: Unreal Speccy io.cpp, memory.cpp (set_banks MM_ATM450), atm.cpp,
/// z80.cpp (reset); ZXMAK2 Atm/MemoryAtm450.cs, Atm/UlaAtm450.cs.
/// Design: docs/inprogress/2026-10-01-atm450/requirements.md

class PortDecoder_ATM450 : public PortDecoder_ATM710
{
    /// region <Constants>
public:
    static constexpr uint8_t ATM450_AFE_ROM       = 0x80;  // aFE.7 (A7): 1 = ROM at #0000, 0 = RAM pages 0 / 4
    static constexpr uint8_t ATM450_AFE_VMODE     = 0x60;  // aFE.6-5 (A6-A5): video mode RG0 / RG1
    static constexpr uint8_t ATM450_AFB_CPSYS     = 0x80;  // aFB.7 (A7): system ROM select
    static constexpr uint8_t ATM450_FDFD_RAM_EXT  = 0x03;  // pFDFD.1-0 (EA16 / EA17): #C000 RAM page bits 4-3
    static constexpr uint8_t ATM450_FDFD_ROM_A16  = 0x04;  // pFDFD.2 (RA16): ROM A16, 128 KiB "ROM disc"
    static constexpr uint8_t ATM450_FDFD_CPNET    = 0x08;  // pFDFD.3: TR-DOS session forces CPSYS

    // Port #FE bit 7 PAL marker (UnrealSpeccy atm450_z): three short zero
    // windows per normal-speed frame, 0x80 everywhere else
    static constexpr uint32_t ATM450_PALZ_WINDOW_STARTS[3] = {7200, 7284, 7326};
    static constexpr uint32_t ATM450_PALZ_WINDOW_LENGTH = 40;
    /// endregion </Constants>

    /// region <Constructors / Destructors>
public:
    PortDecoder_ATM450() = delete;
    PortDecoder_ATM450(EmulatorContext* context);
    ~PortDecoder_ATM450() override;
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    void ApplyBootROMDefaults(ROMModeEnum mode) override;
    /// A 48K / 128K snapshot: ROM (not RAM) at #0000, the ZX picture, not the system ROM (CPSYS off), no RAM extension
    void EnterSpectrum128Paging(uint16_t pc) override;

    /// No turbo states on the 4.50 board (ATM710 has two)
    uint8_t TtdClockUnits() const override { return 1; }

    /// The ATM IDE answers with the DOS ports: the TR-DOS session only, there is no ~CPM latch on 4.50
    IdeAdapter::Gate IdeGate() override;
    /// endregion </Interface methods>

    /// region <Port detection>
public:
    // Hide (not override) the ATM710 #7FFD decode: on 4.50 the group is
    // A15 = 0, A9 = 1, A1 = 0 - the #7DFD palette group takes A9 = 0
    bool IsPort_7FFD(uint16_t port) const { return (port & 0x8202) == 0x0200; }
    bool IsPort_7DFD(uint16_t port) const { return (port & 0x8202) == 0x0000; }
    bool IsPort_FDFD(uint16_t port) const { return (port & 0x8202) == 0x8000; }
    /// aFB latch candidate: a read with A2 = 0
    bool IsPort_FB(uint16_t port) const { return (port & 0x0004) == 0; }

    /// Port #FE bit 7, `sinceInt` T-states after the INT edge (UnrealSpeccy atm450_z coordinates)
    static uint8_t PalMarker(uint32_t sinceInt);
    /// Z80 frame T-state -> T-states since the INT edge of this frame
    uint32_t TStatesSinceInt(uint32_t z80T) const;
    /// endregion </Port detection>

    /// region <Port handlers>
protected:
    void Port_FE_AddressLatch(uint16_t port);
    void Port_FB_AddressLatch(uint16_t port);
    void Port_FDFD_Out(uint8_t value);
    void Port_7DFD_PaletteOut(uint8_t value);

    void updateMemoryBanks() override;
    void updateTurboMode() override;
    /// endregion </Port handlers>
};
