#include "stdafx.h"

#include "emulator/memory/scorpion/scorpionromwindow.h"

/// region <Verified quadrant transition table>

// Hardware-verified read-strobe transitions (hardware-reference §5.2). While
// the Service ROM window is paged at #0000, a CPU read of the #0100-#010F block
// clocks the ProfROM GAL: row selector S = A3:A2 (read #0100 + 4*S), A0/A1 are
// not bonded on the chip. Column = current GAL 2-bit state, cell = next state.
// Byte-identical to the original UnrealSpeccy set_scorp_profrom() switch_table,
// ZXMAK2 s_profPlaneMap and Xpeccy ZSLays; independently confirmed by the
// Scorpion 256 Turbo+ GAL decode (RDR- clocked plane latches).
//
//           S=0 #0100-03  S=1 #0104-07  S=2 #0108-0B  S=3 #010C-0F
//   from Q0:    Q0            Q3            Q2            Q1
//   from Q1:    Q1            Q3            Q2            Q0
//   from Q2:    Q2            Q3            Q0            Q1
//   from Q3:    Q3            Q2            Q1            Q0
//
// The S=0 row holds: the monitor reads its plane ID from #0101 (value >> 2)
// without switching, and any quadrant's boot/execution fetch of the block is
// inert - which is what makes the scheme transparent to normal execution
static constexpr uint8_t ScorpionRomSwitchTable[4][4] =
        {
                {0, 1, 2, 3},   // S = 0: #0100-#0103 - hold
                {3, 3, 3, 2},   // S = 1: #0104-#0107
                {2, 2, 0, 1},   // S = 2: #0108-#010B
                {1, 0, 1, 0}    // S = 3: #010C-#010F
        };

/// endregion </Verified quadrant transition table>

void ScorpionRomWindow::Configure(TEMP& temp, uint16_t imageBanks)
{
    uint16_t quadrants = static_cast<uint16_t>(imageBanks / ROM_QUADRANT_PAGES);

    // State-machine bits: 64 KB (1 quadrant) never switches, 128 KB carries a
    // single bit, 256 KB and up carry both (hardware-reference §5.2)
    temp.profrom_mask = quadrants >= 4 ? 3 : static_cast<uint8_t>(quadrants - 1);

    // #7EFD[5:4] window select only exists beyond the state machine's reach:
    // 512 KB images use bit 4, 1 MB/2 MB use bits 5:4 (hardware-reference §5.3)
    temp.profrom_window_mask = quadrants >= 16 ? 3 : (quadrants >= 8 ? 1 : 0);

    // Geometry clamp: quadrant values are composed masked, so they can never
    // name a quadrant the image does not carry (the extension bit of a 2 MB
    // image is the only source of bit 4)
    _imageQuadrantMask = static_cast<uint8_t>(quadrants - 1);
}

bool ScorpionRomWindow::OnRomRead(EmulatorState& state, const TEMP& temp, uint16_t addr)
{
    uint8_t previous = state.profrom_bank;

    // The GAL advances its 2-bit state per the verified table with the row
    // selector taken from A3:A2, masked by the image size; the window bits
    // above it are preserved (hardware-reference §5.2)
    uint8_t selector = static_cast<uint8_t>((addr >> 2) & 0b11);
    uint8_t galState = static_cast<uint8_t>(ScorpionRomSwitchTable[selector][previous & 0b11] & temp.profrom_mask);
    state.profrom_bank = static_cast<uint8_t>((previous & 0b11111100 & _imageQuadrantMask) | galState);

    return state.profrom_bank != previous;
}

void ScorpionRomWindow::OnWindowPortWrite(EmulatorState& state, const TEMP& temp, uint8_t value)
{
    state.p7EFD = value;

    // Window bits replace their part of the quadrant, the GAL 2-bit state is
    // preserved; bit 6 is the emulator extension (debug/API, 2 MB images only -
    // the image clamp drops it everywhere else)
    uint8_t window = static_cast<uint8_t>((value >> 4) & temp.profrom_window_mask);
    uint8_t extension = static_cast<uint8_t>((value >> 6) & 0b1);
    uint8_t composed = static_cast<uint8_t>(((window << 2) | (state.profrom_bank & 0b11)) | (extension << 4));

    state.profrom_bank = static_cast<uint8_t>(composed & _imageQuadrantMask);
}

uint8_t ScorpionRomWindow::Quadrant(const EmulatorState& state) const
{
    // Kept inside the image bounds by construction - every composition path
    // masks with _imageQuadrantMask
    return state.profrom_bank;
}

void ScorpionRomWindow::Reset(EmulatorState& state) const
{
    // Power-on: quadrant 0 (boot always comes from the image's first 64 KB)
    // with a cleared window latch (hardware-reference §5.2)
    state.profrom_bank = 0;
    state.p7EFD = 0;
}
