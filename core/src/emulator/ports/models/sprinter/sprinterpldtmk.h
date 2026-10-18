#pragma once

#include "emulator/memory/sprinter/sprinteraccelerator.h"
#include "emulator/ports/models/sprinter/sprinterpldconfiguration.h"

/// The "TMK" PLD configuration (tmk-accelerator.md): Standard's 1K30 design (BIOS-TT sprinter-computer-hard with
/// the 2022 keyboard / ZX-port changes) with an extended accelerator, by tmk79 (K30 sources of 2026-02-25, the
/// author's ACCELER.TDF / SP2_ACEX.TDF; test program TmKonf.exe). What it changes against Standard:
///   1. the accelerator's buffer is 1 024 bytes, its down-counter 10 bits;
///   2. code #80 ACC_CTRL (write): bits 0-3 clip stores into the graphics pages by PORT_Y bit 7 / address bit 7,
///      bit 4 the rectangle, bit 5 length x 4, bit 6 the address runs down;
///   3. code #81 ACC_RECT_W (write): bytes per rectangle row - 1; any access to the code reloads the row counter.
/// Codes #80 / #81 are free in Standard; a program puts them into the port table itself (page #40). ACC_CTRL = 0
/// after /RESET: the accelerator is Standard's until a program writes it. Shift+F12 toggles the TV / VGA output
/// (V_CS1 of the video DAC, nothing to emulate); NMI_SW is wired to nothing (NMI_ENA = VCC).
///
/// Worked example (TmKonf.exe): the port table maps #7785 -> #80, #7786 -> #81. OUT (#7786),31 : OUT (#7785),#30
/// (rectangle, x 4) : LD D,D : LD A,0 (1 024 bytes) : LD L,L : LD (DE),A stores the buffer as 32 rows of 32 bytes
/// from DE, PORT_Y + 1 per row.
class SprinterPldTmk : public SprinterPldConfiguration
{
public:
    /// SP2_ACEX.ttf of the author's K30 build (59 215 bytes, = an .ACX): hashes over the loader's writes
    static constexpr uint32_t kFullHash = 0x84BA5A9A;
    static constexpr uint32_t kHeadHash = 0xE1BA76FB;

    static constexpr uint8_t kCodeCtrl = 0x80;
    static constexpr uint8_t kCodeRectWidth = 0x81;

    SprinterPldTmk();

    const SprinterPldModuleDescriptor& Descriptor() const override { return _descriptor; }

    bool ReadCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t& value) override;
    bool WriteCode(PortDecoder_Sprinter& decoder, uint8_t code, uint16_t port, uint8_t value) override;
    /// The standard accelerator with the TMK additions attached
    SprinterAccelerator* Accelerator(PortDecoder_Sprinter& decoder) override;

    void OnReset(SprinterResetKind kind, SprinterPldState& pld) override;
    size_t StateSize() const override { return sizeof(SprinterAccelTmkState); }
    void SaveState(uint8_t* dst) const override;
    void LoadState(const uint8_t* src) override;

    SprinterAccelTmkState& State() { return _state; }
    const SprinterAccelTmkState& State() const { return _state; }

private:
    SprinterPldModuleDescriptor _descriptor;
    SprinterAccelTmkState _state{};
};
