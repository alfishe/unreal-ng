// hexfmt.h - exact printf-style formatting helpers for on-screen fields.
// The TDD formats are normative: %02X, %04X, %X, %6u, %3s etc. These tiny
// helpers reproduce them byte-for-byte without locale or iostream overhead.
#pragma once

#include <cstdint>
#include <string>

namespace dbg {

inline std::string Hex2(unsigned value) {  // "%02X"
    const unsigned v = value & 0xFF;
    const char digits[] = "0123456789ABCDEF";
    return {digits[v >> 4], digits[v & 0xF]};
}

inline std::string Hex3(unsigned value) {  // 3 hex digits (unused by layout, kept for titles)
    const unsigned v = value & 0xFFF;
    const char digits[] = "0123456789ABCDEF";
    return {digits[(v >> 8) & 0xF], digits[(v >> 4) & 0xF], digits[v & 0xF]};
}

inline std::string Hex4(unsigned value) {  // "%04X"
    const unsigned v = value & 0xFFFF;
    const char digits[] = "0123456789ABCDEF";
    return {digits[(v >> 12) & 0xF], digits[(v >> 8) & 0xF], digits[(v >> 4) & 0xF],
            digits[v & 0xF]};
}

inline std::string Hex6(unsigned value) {  // "%06X"
    return Hex4(value >> 8) + Hex2(value);
}

inline std::string Hex8(unsigned value) {  // "%08X"
    const unsigned v = value;
    const char digits[] = "0123456789ABCDEF";
    std::string out;
    for (int shift = 28; shift >= 0; shift -= 4) {
        out += digits[(v >> shift) & 0xF];
    }
    return out;
}

inline std::string HexDigit(unsigned value) {  // "%X"
    const char digits[] = "0123456789ABCDEF";
    return std::string(1, digits[value & 0xF]);
}

// "%6u" / "%14I64d" style right-aligned decimal.
std::string DecRight(int64_t value, int width);
inline std::string Dec6(unsigned value) { return DecRight(static_cast<int64_t>(value), 6); }
inline std::string Dec14(int64_t value) { return DecRight(value, 14); }
inline std::string Dec3(unsigned value) { return DecRight(static_cast<int64_t>(value), 3); }

// Right-aligned fixed-width string ("%3s:" style prefixes are built by callers).
std::string StrRight(const std::string& text, int width);
// Truncate/pad to exact cell count.
std::string StrFit(const std::string& text, int width);

}  // namespace dbg
