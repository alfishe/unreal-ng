// glyphs.h - byte code -> Unicode glyph mapping (terminal back-ends).
// TDD-DBG-01 §1.2/Appendix D: map through CP437 for 0x00-0x1F and 0x7F,
// through CP866 for 0x80-0xFF, ASCII for 0x20-0x7E. 0x00 is shown as a
// space (the monitor prints '.' for zero bytes in dumps itself).
#pragma once

#include <cstdint>

namespace dbg {

extern const char* const kGlyphs[256];

// Glyph for a screen byte (0x00 renders as blank, matching the original).
inline const char* GlyphOf(uint8_t code) { return kGlyphs[code]; }

}  // namespace dbg
