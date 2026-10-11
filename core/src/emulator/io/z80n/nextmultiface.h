#pragma once

/// @file nextmultiface.h
/// @brief The Next's Multiface (zxnext.vhd + device/multiface.vhd, docs/inprogress/2026-10-07-zx-next/design-nextreg-journal.md for how
/// it was found): the NMI that NextZXOS uses to start a snapshot (the `SPECTRUM` command, .sna / .z80 / .snx from the Browser) and the
/// M1 button's NMI menu. The 8K ROM (system page 5, loaded by the firmware from enNextMf.rom) and 8K RAM show at #0000-#3FFF
/// while it is "enabled"; ports (by NR #0A bits 7:6) page it in and out; a RETN ends the NMI session.
///
/// State (multiface.vhd): nmi_active (1 = the NMI was requested, the handler has not ended), invisible, mf_enable (memory paged in).
///   button (NR #02 bit 3, the M1 key; needs NR #06 bit 3, CONMEM off, no DivMMC handler): nmi_active = 1, invisible = 0
///   M1 fetch at #0066 while nmi_active: the memory is in at once (the fetch itself comes from the ROM)
///   enable port read: in unless invisible (mode 48: never invisible); disable port read: out; RETN: out and nmi_active = 0
///   writes to either port end nmi_active; the invisible flag follows the mode (+3: enable write, 128: disable write)
/// Modes (NR #0A bits 7:6): 00 +3 (enable #3F, disable #BF), 01 128 (#BF / #3F), 10 and 11 (#9F / #1F; 11 is 48K: always visible)

#include <cstdint>
#include <functional>

#include "emulator/cpu/z80.h"
#include "emulator/memory/next/nextmemory.h"

class NextBoard;

class NextMultiface final : public IMachineM1Hook
{
public:
    NextMultiface(NextMemory* memory, NextBoard* board, const EmulatorState* state) : _memory(memory), _board(board), _state(state) {}

    void Reset();

    /// NR #83 bit 1 (internal port enable): the Multiface's ports and memory exist
    bool Enabled() const;
    /// NR #0A bits 7:6
    unsigned Type() const;
    uint8_t EnablePort() const;
    uint8_t DisablePort() const;

    /// The button's NMI was accepted: the session starts (nmi_active, visible)
    void PressButton();
    /// A RETN completed
    void OnRetn();

    /// The memory is paged in (mf_enable) / the NMI session is open (nmi_active) / either (mf_is_active)
    bool MemoryIn() const { return _mfEnable; }
    bool NmiHold() const { return _nmiActive; }
    bool IsActive() const { return _mfEnable || _nmiActive; }
    bool Invisible() const { return _invisible; }

    /// An I/O cycle at the enable / disable port. Returns true with `value` set when the Multiface drives the read (enable port,
    /// visible, 128 or +3 mode); other devices answer otherwise
    bool PortRead(uint8_t lowByte, uint16_t port, uint8_t& value);
    void PortWrite(uint8_t lowByte);

    void BeforeMachineM1(uint16_t address) override;
    void OnMachineM1(uint16_t address) override { (void)address; }

private:
    void Publish();

    NextMemory* _memory;
    NextBoard* _board;
    const EmulatorState* _state;
    bool _nmiActive = false;
    bool _invisible = true;
    bool _mfEnable = false;
};
