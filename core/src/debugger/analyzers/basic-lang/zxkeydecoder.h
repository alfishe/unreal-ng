#pragma once

#include "stdafx.h"

#include "emulator/io/keyboard/keyboard.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

/// region <Documentation>

/// What code a key press gives, computed the way the 48 BASIC ROM computes it
/// (input-verification.md §6.1). The ROM's interrupt runs KEY-SCAN (key number
/// and shift), K-TEST (main code from the table at #0205) and K-DECODE (final
/// code from the cursor mode, FLAGS, FLAGS2 and the shift, through the tables
/// based at #01EB, #0205, #0229, #0230 and #0254). This is that logic, reading
/// the tables from the ROM page itself, so the result is the ROM's own answer
/// rather than a keyboard map written from memory.
///
/// The inverse (Find) is what the command typer uses: before each key it reads
/// the editor's live mode and asks which press gives the byte it needs next.
/// The editors of the 128K, +3 and TR-DOS use the same routine (the 128K ROM1
/// and the +3 ROM3 carry it at the same addresses).

/// endregion </Documentation>

namespace ZXKeyDecoder
{
    /// Shift byte as KEY-SCAN leaves it in D
    enum class Shift : uint8_t
    {
        None = 0xFF,
        Caps = 0x27,
        Symbol = 0x18
    };

    /// MODE system variable ($5C41): 0 = K/L/C, 1 = E, 2 = G
    struct EditorState
    {
        uint8_t mode = 0;    ///< MODE   ($5C41)
        uint8_t flags = 0;   ///< FLAGS  ($5C3B), bit 3 set = L mode
        uint8_t flags2 = 0;  ///< FLAGS2 ($5C6A), bit 3 set = CAPS LOCK
    };

    /// The 40 keys in KEY-SCAN order (index = key number, table #0205)
    const std::array<ZXKeysEnum, 40>& KeyOrder();

    /// Final code for `key` (not a shift) pressed with `shift`, or nullopt for a
    /// press the ROM ignores. `rom` is a 16K 48 BASIC page.
    std::optional<uint8_t> Decode(const uint8_t* rom, ZXKeysEnum key, Shift shift, const EditorState& state);

    struct Press
    {
        /// Press E mode first (CAPS + SYMBOL, final code #0E) when true
        bool extendedFirst = false;
        /// Keys held together for the code (shift first)
        std::vector<ZXKeysEnum> keys;
        uint8_t code = 0;
    };

    /// A press that gives `wanted` in `state`: plain key, then CAPS, then
    /// SYMBOL, then the same through E mode. nullopt when no press gives it
    /// (for example a letter while the editor is in K mode).
    std::optional<Press> Find(const uint8_t* rom, uint8_t wanted, const EditorState& state);
}
