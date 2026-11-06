#pragma once

/// @file nextdivmmc.h
/// @brief The Next's DivMMC (esxdos-and-sd.md section 3): port #E3, the automap driven by NR #B8-#BB, and the
/// memory view that NextMemory shows at #0000-#3FFF. The SPI side (#E7 / #EB) is the decoder's.
///
/// #E3: bit 7 CONMEM (map in now), bit 6 MAPRAM (sticky: a write can only set it; NR #09 bit 3 clears it), bits 3:0
/// the RAM bank at #2000. Automap: NR #B8 enables the eight RST addresses (reset #83: #0000 #0008 #0038), NR #B9 says
/// which hold always (1) or only while the 48K BASIC ROM is paged (0), NR #BA which map instantly (the opcode already
/// comes from the DivMMC) rather than after the fetch, NR #BB the extras (#3Dxx, the off-area #1FF8-#1FFF, #056A
/// #04D7 #0562 #04C6, the NMI entry #0066: bit 1 instant, bit 0 delayed). NR #0A bit 4 enables the automap as a whole: clearing it (or
/// the DivMMC port, NR #83 bit 0) is automap_reset and drops the latch at once. The ROM-3-only entries need the MMU to map ROM at the
/// fetch (RAM in slot 0 / 1 is the program's own code), not config mode, and the 48K BASIC ROM selected.
/// Audit: docs/inprogress/2026-10-07-zx-next/audit-divmmc-gap-2026-10-10.md

#include <cstdint>
#include <functional>

#include "emulator/cpu/z80.h"
#include "emulator/io/z80n/nextdivmmcjournal.h"
#include "emulator/memory/next/nextmemory.h"

class NextBoard;

class NextDivMmc final : public IMachineM1Hook
{
public:
    static constexpr uint8_t kPort = 0xE3;
    static constexpr uint8_t kRegAutomapEnable = 0xB8;
    static constexpr uint8_t kRegAutomapValid = 0xB9;
    static constexpr uint8_t kRegAutomapTiming = 0xBA;
    static constexpr uint8_t kRegAutomapExtra = 0xBB;

    NextDivMmc(NextMemory* memory, NextBoard* board) : _memory(memory), _board(board) {}

    /// Is the 48K BASIC ROM the one paged at #0000 (the ROM-3-only entries need it)
    std::function<bool()> isBasicRomPaged;

    /// The journal (automation-gaps-2026-10-11.md, P2). `fillClock` sets frame, T and PC of an event, `fillCall` the esxDOS call of an entry at
    /// #0008 (the byte after the RST and the registers; the DivMMC does not know the CPU)
    /// The machine's event watch (EmulatorContext::machineEventWatch) and its listener: while it is set every event
    /// the journal would record goes to `onWatchedEvent` too, the journal on or off
    const bool* eventWatch = nullptr;
    std::function<void(const NextDivMmcEvent&)> onWatchedEvent;
    NextDivMmcJournal& Journal() { return _journal; }
    const NextDivMmcJournal& Journal() const { return _journal; }
    std::function<void(NextDivMmcEvent&)> fillClock;
    std::function<void(NextDivMmcEvent&)> fillCall;
    /// NR #0A bit 4 and the port enable: is the automap armed at all
    bool AutomapArmed() const { return AutomapEnabled(); }

    /// A reset (the power-on one and NR #02's): E3 = 0, the latches cleared (zxnext.vhd 4175, divmmc.vhd 108-139)
    void Reset();
    /// NR #0A bit 4 or NR #83 bit 0 changed: automap_reset (zxnext.vhd 4112) clears the latches and the memory follows
    void OnEnableChanged();
    /// A program starts (the NEX loader): the DivMMC is out - NEXLOAD ends its last esxDOS call through #1FF8 with CONMEM clear
    void ReleaseForProgramStart();
    /// NR #83 bit 0: port #E3 is decoded and the DivMMC's memory is enabled (port_divmmc_io_en)
    bool PortEnabled() const;
    void WritePort(uint8_t value);
    uint8_t ReadPort() const { return static_cast<uint8_t>(_control & 0xCF); }
    /// NR #09 bit 3 = 1 clears the sticky MAPRAM
    void ClearMapram();

    /// The DRIVE button (divmmc.vhd button_nmi): the next M1 at #0066 maps the DivMMC in, instantly with NR #BB bit 1, after the fetch with
    /// bit 0, not at all with neither. While the automap is disabled the latch is held clear (automap_reset beats the button).
    /// The board pulses /NMI at the same time (PortDecoder_Next::RequestBoardNmi)
    void PressButton();
    /// disable_nmi_o: the handler is in (or on its way in), a second button press waits
    bool NmiHold() const { return _automap || _buttonNmi; }
    bool ButtonPending() const { return _buttonNmi; }
    /// A RETN went by (zxnext.vhd divmmc_retn_seen = ED 45 and no Multiface session): the automap and the button latch are cleared
    void OnRetn();

    bool Mapped() const { return PortEnabled() && (_automap || (_control & 0x80) != 0); }
    bool Automapped() const { return _automap; }
    bool Mapram() const { return (_control & 0x40) != 0; }
    uint8_t Bank() const { return _control & 0x0F; }

    void BeforeMachineM1(uint16_t address) override;
    void OnMachineM1(uint16_t address) override;

private:
    friend struct NextStateCodec;  // the TTD state codec (nextstatecodec.cpp)
    enum class Hit : uint8_t { None, Delayed, Instant, MapOut };
    Hit Classify(uint16_t address) const;
    /// NR #0A bit 4 and the port enable: automap_reset is their inverse
    bool AutomapEnabled() const;
    void Publish();
    void Note(NextDivMmcKind kind, uint16_t address = 0, bool instant = false);

    NextDivMmcJournal _journal;
    NextMemory* _memory;
    NextBoard* _board;
    uint8_t _control = 0;
    bool _automap = false;
    bool _buttonNmi = false;
};
