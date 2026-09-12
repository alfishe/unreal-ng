#pragma once
#include "stdafx.h"

#include "emulator/platform.h"

/// @brief Scorpion ProfROM quadrant window - stateless policy object (design §4.2).
///
/// Every byte of quadrant state lives in the existing EmulatorState / TEMP fields
/// (profrom_bank, p7EFD, profrom_mask, profrom_window_mask), so TTD checkpoints,
/// the divergence hash and any future serializer see it without special cases.
/// The object itself only carries image geometry derived by Configure().
///
/// Selection mechanism (hardware-reference §5.2): while the Service ROM window is
/// paged at #0000, any CPU read of the #0100-#010F block clocks a GAL quadrant
/// state machine (row selector S = A3:A2, A0/A1 unwired - the read of #0100+4*S
/// applies row S); images above 256 KB additionally take the quadrant high bits
/// from #7EFD[5:4] (plus the emulator extension bit #7EFD[6] for 2 MB images -
/// debug/API only). The effective quadrant is (window << 2 | state), clamped by
/// the image size. The caller gates the strobe on MM_PROFSCORP && CF_PROFROM.
class ScorpionRomWindow
{
public:
    /// @brief Derive the image-size masks from the loaded bank count (16 KB banks).
    /// @param temp TEMP carrying profrom_mask / profrom_window_mask (written here)
    /// @param imageBanks banks loaded and validated by the ROM loader
    void Configure(TEMP& temp, uint16_t imageBanks);

    /// @brief Read strobe for the #0100-#010F block (caller gates on the Service
    ///        ROM being paged).
    /// @param state EmulatorState carrying profrom_bank (updated in place)
    /// @param temp TEMP carrying profrom_mask
    /// @param addr address being read (only bits 3:2 select the table row)
    /// @return true when the quadrant changed and the ROM bases need a rebuild
    bool OnRomRead(EmulatorState& state, const TEMP& temp, uint16_t addr);

    /// @brief #7EFD latch write: refresh the quadrant's window bits above the
    ///        state machine's two (inert while profrom_window_mask is 0).
    /// @param state EmulatorState carrying profrom_bank and p7EFD (both updated)
    /// @param temp TEMP carrying profrom_window_mask
    /// @param value byte written to #7EFD
    void OnWindowPortWrite(EmulatorState& state, const TEMP& temp, uint8_t value);

    /// @brief Effective quadrant 0..31 - state.profrom_bank, kept inside the
    ///        image bounds by construction (masks compose the value from parts)
    uint8_t Quadrant(const EmulatorState& state) const;

    /// @brief Power-on: quadrant 0 (boot always comes from the image's first 64 KB,
    ///        hardware-reference §5.2) and a cleared window latch
    void Reset(EmulatorState& state) const;

private:
    /// Image quadrant count mask (geometry set by Configure, not quadrant state)
    uint8_t _imageQuadrantMask = 0;
};
