// hexfmt.cpp - definitions for the printf-style helpers declared in hexfmt.h.
#include "model/hexfmt.h"

#include <string>

namespace dbg {

std::string DecRight(int64_t value, int width) {
    // Decimal with sign; right-aligned, zero-padded on overflow-width loss is
    // not a concern: widths used by the layout are large enough by contract.
    std::string digits;
    const bool negative = value < 0;
    uint64_t v = negative ? static_cast<uint64_t>(-(value + 1)) + 1 : static_cast<uint64_t>(value);
    do {
        digits += static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0);
    std::string out;
    out.reserve(digits.size() + 1);
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) out += *it;
    if (negative) out.insert(out.begin(), '-');
    if (static_cast<int>(out.size()) < width) {
        out.insert(0, static_cast<size_t>(width) - out.size(), ' ');
    }
    return out;
}

std::string StrRight(const std::string& text, int width) {
    if (static_cast<int>(text.size()) >= width) return text.substr(0, static_cast<size_t>(width));
    return std::string(static_cast<size_t>(width) - text.size(), ' ') + text;
}

std::string StrFit(const std::string& text, int width) {
    if (static_cast<int>(text.size()) > width) {
        return text.substr(0, static_cast<size_t>(width));
    }
    if (static_cast<int>(text.size()) < width) {
        return text + std::string(static_cast<size_t>(width) - text.size(), ' ');
    }
    return text;
}

}  // namespace dbg
