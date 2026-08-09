import AppKit

/// Maps macOS virtual key codes (NSEvent.keyCode - layout independent, physical position)
/// onto the ZX Spectrum key matrix.
///
/// Ported from the Qt POC's KeyboardManager table, with two deliberate differences:
///
///  1. Qt maps `Qt::Key_Control` (which is Command on macOS) to SYM_SHIFT. Command is
///     NOT mapped here: every Command chord is a menu shortcut, and AppKit routes the
///     key-equivalent to the menu without ever delivering the matching key-up, which
///     latches SYM_SHIFT in the matrix and makes the keyboard look dead. Control and
///     Option carry SYM_SHIFT instead.
///  2. Punctuation is mapped by physical key rather than by produced character, since
///     NSEvent.keyCode is layout independent (values below are the Carbon kVK_* codes).
enum ZXKeyboardMap {
    private static let table: [UInt16: UNZXKey] = [
        // Letters
        0x00: .letterA, 0x0B: .letterB, 0x08: .letterC, 0x02: .letterD, 0x0E: .letterE,
        0x03: .letterF, 0x05: .letterG, 0x04: .letterH, 0x22: .letterI, 0x26: .letterJ,
        0x28: .letterK, 0x25: .letterL, 0x2E: .letterM, 0x2D: .letterN, 0x1F: .letterO,
        0x23: .letterP, 0x0C: .letterQ, 0x0F: .letterR, 0x01: .letterS, 0x11: .letterT,
        0x20: .letterU, 0x09: .letterV, 0x0D: .letterW, 0x07: .letterX, 0x10: .letterY,
        0x06: .letterZ,

        // Digit row
        0x1D: .digit0, 0x12: .digit1, 0x13: .digit2, 0x14: .digit3, 0x15: .digit4,
        0x17: .digit5, 0x16: .digit6, 0x1A: .digit7, 0x1C: .digit8, 0x19: .digit9,

        // Keypad digits
        0x52: .digit0, 0x53: .digit1, 0x54: .digit2, 0x55: .digit3, 0x56: .digit4,
        0x57: .digit5, 0x58: .digit6, 0x59: .digit7, 0x5B: .digit8, 0x5C: .digit9,

        // Main keys
        0x31: .space,       // Space
        0x24: .enter,       // Return
        0x4C: .enter,       // Keypad Enter
        0x33: .extDelete,   // Backspace -> CAPS SHIFT + 0
        0x35: .extBreak,    // Escape    -> CAPS SHIFT + SPACE
        0x32: .extEdit,     // `         -> CAPS SHIFT + 1

        // Cursor keys -> CAPS SHIFT + 5/6/7/8
        0x7B: .extLeft,
        0x7C: .extRight,
        0x7E: .extUp,
        0x7D: .extDown,

        // Punctuation
        0x2F: .extDot,      // .
        0x2B: .extComma,    // ,
        0x1B: .extMinus,    // -
        0x18: .extEqual,    // =
        0x2A: .extBkSlash,  // \
        0x2C: .extDiv,      // /
        0x27: .extDblQuote, // '
        0x43: .extMul,      // Keypad *
        0x45: .extPlus,     // Keypad +
        0x4E: .extMinus,    // Keypad -
        0x4B: .extDiv,      // Keypad /
        0x51: .extEqual,    // Keypad =
        0x41: .extDot,      // Keypad .
    ]

    static func zxKey(forVirtualKeyCode code: UInt16) -> UNZXKey? {
        table[code]
    }

    /// Modifier keys mirrored into the matrix. Caps Lock is deliberately excluded:
    /// macOS reports it as a latching state whose release is unreliable.
    static let modifiers: [(flag: NSEvent.ModifierFlags, key: UNZXKey)] = [
        (.shift, .capsShift),
        (.control, .symShift),
        (.option, .symShift),
    ]
}
