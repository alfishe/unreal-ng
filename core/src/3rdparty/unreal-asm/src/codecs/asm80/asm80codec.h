#pragma once

// ASM80 / Asm80Win sources (Vyacheslav Mednonogov, Copper Feet, 1995-1999: "Assembler 512 for Z80", a PC cross
// assembler for the Spectrum). An ASM80 source is a text file in CP866 (`.a80`); this codec is the text codec with
// the asm80 dialect and its detection by what only ASM80 writes: key lines in column 0 (*F file, *B file, *L+, *O,
// *Z80, *Pn, ...), DISP ... ENDD, DEFR, INF, macro parameters =0..=9 after NAME MAC.

#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
class Asm80Codec : public TextCodec
{
public:
    Asm80Codec();
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
};
}  // namespace unrealasm::codecs
