#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/profi/proficmos.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

/// ZX Profi 1024 port decoder.
///
/// Behaviour follows the UnrealSpeccy reference implementation (io.cpp / memory.cpp),
/// cross-checked with ZXMAK2 and Xpeccy. Design: docs/inprogress/2026-09-21-profi/technical-design.md
///
/// Latches:
///   #7FFD  bits 2:0 RAM low, 3 screen (5|7), 4 ROM14 (1 = 48K/DOS side), 5 lock
///   #DFFD  bits 2:0 RAM high, 3 SCO, 4 WOROM, 5 CPM, 6 SCR, 7 DS80 (512x240 + palette)
///   DOS latch = CF_TRDOS (set by the $3Dxx M1 trap, cleared on fetch from >= $4000)
class PortDecoder_Profi : public PortDecoder
{
    /// region <Constructors / Destructors>
public:
    PortDecoder_Profi() = delete;                 // Disable default constructor; C++ 11 feature
    PortDecoder_Profi(EmulatorContext* context);
    virtual ~PortDecoder_Profi();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    void SetRAMPage(uint8_t page) override;
    void SetROMPage(uint8_t page) override;

    /// Latch-to-bank translation (called from Memory::UpdateZ80Banks after the ROM slot is chosen)
    void UpdateModelMemoryBanks() override;

    std::vector<ttd::PeripheralId> GetTTDModelStateIds() const override;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;
    /// endregion </Interface methods>

    /// region <Helper methods>
public:
    /// #7FFD: A15=0, A1=0 (A2 is not decoded)
    static bool IsPort_7FFD(uint16_t port);
    /// #DFFD: A15=1, A13=0, A1=0
    static bool IsPort_DFFD(uint16_t port);

    /// Decode a Profi FDC port for the current mode.
    /// @return canonical Beta128 port (#1F/#3F/#5F/#7F/#FF) or 0 when the address is not an FDC port
    uint16_t DecodeFDCPort(uint16_t port) const;

    /// Palette write (OUT to #xx7E family with DFFD.7): colour from ~A15..A8, index from the previous #FE value
    void Port_Palette_Out(uint16_t port);

    /// #FE read bit 7 ("GX0" / UniCopy palette-present flag, 5.xx boards): in DS80, the palette
    /// entry selected by the previous #FE write's index reports (bit6 XOR bit0) on readback;
    /// outside DS80 the wire is pulled high. Karabas video.vhd:219, ZXMAK2 UlaProfi5XX.cs:34-53.
    uint8_t Port_FE_In_GX0() const;

    /// EXT mode qualifier (UnrealSpeccy default: cpm && rom14; Karabas additionally allows
    /// dosAct && !rom14, not implemented here - unproven by UnrealSpeccy sources)
    bool IsExtMode() const;
    /// endregion <Helper methods>

protected:
    void Port_7FFD(uint8_t value, uint16_t pc);
    void Port_DFFD(uint8_t value, uint16_t pc);
    void ResetPalette();

    /// RTC/CMOS (DS12885-style), EXT mode only. Address: #BF/#FF, data: #9F/#DF
    /// (UnrealSpeccy io.cpp: `(port & 0x9F) == 0x9F`, bit 5 selects address vs data).
    ProfiCMOS _cmos;

    /// Tracks whether Covox currently has a live port set - either #3F/#5F (NORMAL,
    /// !dosPorts) or #C7/#A7 (CP/M-extended mode, IsExtMode()) - so the transition into a
    /// plain TR-DOS/Beta128 FDC session (dosPorts && !IsExtMode(), where Covox has no
    /// ports at all) can silence the DAC exactly once (see DecodePortOut).
    bool _covoxWasReachable = true;
};
