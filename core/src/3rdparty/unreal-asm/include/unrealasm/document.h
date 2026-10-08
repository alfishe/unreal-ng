#pragma once

// The decoded form of a source (architecture.md §3): its lines as exact UTF-8 text in the assembler's own dialect,
// plus what the codec needs to write the same bytes again (attributes).

#include <cstdint>
#include <string>
#include <vector>

#include "unrealasm/encoding.h"

namespace unrealasm
{
/// Codec-private data kept beside a line or a document so encoding with the same codec is byte-exact. Another
/// codec ignores it (and writes its own canonical form)
struct AttrBag
{
    std::string codec;               ///< the codec id that wrote it ("" = none)
    std::vector<uint8_t> bytes;
    bool Empty() const { return codec.empty(); }
};

struct SourceLine
{
    std::string text;    ///< UTF-8, exactly as the assembler shows it, without the line break (and without its number)
    AttrBag attrs;
    int number = -1;     ///< the line's number in formats that number their lines (GENS, ZEUS); -1 = none
};

struct SourceDocument
{
    std::string name;                ///< for diagnostics (a file name)
    std::string dialect;             ///< "sjasmplus", "tasm", "alasm", ... ("" = not known)
    std::string format;              ///< the codec id it was decoded with ("" = made in memory)
    std::string subversion;          ///< the version decoded (a CodecInfo::subversions id) when the format has versions
    encoding::CodePage codePage = encoding::CodePage::Utf8;   ///< the original code page (decision D-11)
    encoding::LineEnd lineEnd = encoding::LineEnd::Lf;        ///< the dominant line end of the original
    std::vector<SourceLine> lines;
    AttrBag attrs;                   ///< file level
    /// The file is part of a project written for the ZX Spectrum Next (another file enables it with DEVICE ZXSPECTRUMNEXT
    /// or OPT --zxnext): its frontend reads the Z80N mnemonics from the first line. ConvertProject sets it
    bool z80n = false;

    /// The lines joined with '\n' (for display, comparisons, examples)
    std::string Text() const;
    /// A document of plain lines (split at '\n'; a trailing '\r' is kept in the text) with no attributes
    static SourceDocument FromText(const std::string& text, const std::string& dialect = "");
};
}  // namespace unrealasm
