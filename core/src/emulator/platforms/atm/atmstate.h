#pragma once

#include <cstdint>

/// @file atmstate.h
/// @brief ATM Turbo family state: latches, palette and font RAM shared by PortDecoder_ATM450,
/// PortDecoder_ATM710 and PortDecoder_ATM3 (ATM3 and ZX-Evo BaseConf), read by the ATM and Alco
/// video and by memory paging.
///
/// Embedded by value in EmulatorState::atm: the emulator reset (EmulatorState{}) zeroes it and
/// the TTD paging blob copies it field by field. Plain values only, no pointers.
/// Design: docs/inprogress/2026-10-07-model-state/tdd.md
struct AtmState
{
    uint8_t aFE = 0;            // ATM 4.50 system port #FE
    uint8_t aFB = 0;            // ATM 4.50 system port #FB
    unsigned pFFF7[8] = {};     // ATM 7.10 / ATM3(4Mb) memory map
                                // |7ffd|rom|b7b6|b5..b0| b7b6 = 0 for atm2
    unsigned aFF77 = 0;         // Last #xx77 address: video mode, CP/M, pen2 (A14)
    bool memSwapped = false;    // ATM A5-A7 <-> A8-A10 swap flag (vestigial: the swap is not emulated - reference
                                // gates it behind the default-off AtmMemSwap ini; kept for the TTD paging blob)

    /// region <ATM Turbo 2+ / ZX-Evo BaseConf video state>

    // 16-cell programmable palette RAM behind port #FF (both machines). Cell
    // pointer = the 4-bit border color (border_attr + the FE bright bit), the
    // write gate = A14 of the last #xx77 write (aFF77 & 0x4000, "pen2").
    // palette carries the ABGR cell colors (same packing as the ULA
    // _rgbaColors tables); paletteRegs keeps the raw written byte for the
    // ATM3 #BE.0D readback. borderBright is the 4th border bit latched from
    // A3 of every #FE port write (A3 = 0 -> bright border).
    uint32_t palette[16] = {};
    uint8_t paletteRegs[16] = {};
    uint8_t borderBright = 0;

    // Text-mode font RAM (ATM Turbo 2+ renders from it too, nothing writes it there). Address = code * 8 + row,
    // the FPGA's read address {char, row}; the built-in font (stored row * 256 + code) is copied in by
    // InitFont. ZX-Evo #BF bit 2 mirrors every memory write into it (EvoFontOverlay). fontByte is the
    // glyph byte the text renderer fetched last, what #0EBD reads back (#FF until a text frame is drawn)
    uint8_t fontRam[2048] = {};
    uint8_t fontByte = 0;

    /// endregion </ATM Turbo 2+ / ZX-Evo BaseConf video state>

    /// Seed the palette with the standard 16 ZX colors - what the machine
    /// shows until software overrides cells through #FF (xpeccy vid_reset()
    /// / zx_set_pal() copy the preset into the live palette the same way at
    /// every reset). Values mirror ScreenZX's TransformZXSpectrumColorsToRGBA
    /// tables (ABGR: 0xFF << 24 | B << 16 | G << 8 | R).
    void InitPalette()
    {
        static const uint32_t ZXPAL[16] = {
            // Brightness = 0
            0xFF000000, 0xFFC72200, 0xFF1628D6, 0xFFC733D4,
            0xFF25C500, 0xFFC9C700, 0xFF2AC8CC, 0xFFCACACA,
            // Brightness = 1
            0xFF000000, 0xFFFB2B00, 0xFF1C33FF, 0xFFFC40FF,
            0xFF2FF900, 0xFFFEFB00, 0xFF36FCFF, 0xFFFFFFFF};
        for (int i = 0; i < 16; i++)
        {
            palette[i] = ZXPAL[i];
            paletteRegs[i] = 0x00;
        }
        borderBright = 0;
    }

    /// Copy the built-in font into fontRam; fontByte = #FF
    void InitFont();
};
