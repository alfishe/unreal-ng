#pragma once

// The label file formats the emulator's LabelManager read before the symbol module (symbols/formats.md §3, phase S2),
// now codecs that also write, plus Unreal's user.l:
//
//   unreal-map   [ROMn:|RAMn:]HHHH  NAME  [(TYPE)] [; comment]     our data/symbols/*.map
//   simple-sym   HHHH NAME [(TYPE)] [; comment]
//   unreal-l     HHHH name | PP:HHHH name                          Unreal's user.l: RAM pages only (HHHH = the linear
//                                                                  RAM address, page HHHH >> 14)
//   vice         al C:HHHH .name
//   sjasm-equ    NAME EQU $HHHH [; (TYPE)]
//   z88dk-defc   DEFC name = $HHHH [; (TYPE)]
//
// A format without pages writes a page symbol at the CPU address its window gives (CpuAddress) and reports it;
// what a format cannot hold at all is skipped with a diagnostic.

#include "unrealasm/symbols/codec.h"

namespace unrealasm::symbols::codecs
{
class LineCodec : public ISymbolCodec
{
public:
    explicit LineCodec(CodecInfo info) : _info(std::move(info)) {}
    const CodecInfo& Info() const override { return _info; }
    int Detect(const Probe& probe) const override;
    SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const override;
    SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const override;

    enum class Line : uint8_t
    {
        Skip,       ///< comment, empty, or a line of another kind the format ignores
        Symbol,
        Bad,        ///< looks like a symbol line but does not parse (diagnostic)
    };

protected:
    /// One trimmed, non-comment line -> a symbol; `message` says why a Bad line is bad
    virtual Line ParseLine(std::string_view line, Symbol& out, std::string& message) const = 0;
    /// The line for one symbol ("" with `message` when the format cannot hold it); `folded` when a page was dropped
    virtual std::string WriteLine(const Symbol& s, bool& folded, std::string& message) const = 0;
    /// Detection: 0..100 for the probe's lines (the extension is added by Detect)
    virtual int ScoreLines(const std::vector<std::string_view>& lines) const;
    /// Lines written before the symbols
    virtual std::string Header(const std::string& nl) const { (void)nl; return {}; }
    /// Whether a line test accepts a line (for the default ScoreLines)
    bool Accepts(std::string_view line) const;

private:
    CodecInfo _info;
};

class UnrealMapCodec : public LineCodec
{
public:
    UnrealMapCodec();

protected:
    Line ParseLine(std::string_view line, Symbol& out, std::string& message) const override;
    std::string WriteLine(const Symbol& s, bool& folded, std::string& message) const override;
    int ScoreLines(const std::vector<std::string_view>& lines) const override;
};

class SimpleSymCodec : public LineCodec
{
public:
    SimpleSymCodec();

protected:
    Line ParseLine(std::string_view line, Symbol& out, std::string& message) const override;
    std::string WriteLine(const Symbol& s, bool& folded, std::string& message) const override;
    std::string Header(const std::string& nl) const override;
};

class UnrealLCodec : public LineCodec
{
public:
    UnrealLCodec();

protected:
    Line ParseLine(std::string_view line, Symbol& out, std::string& message) const override;
    std::string WriteLine(const Symbol& s, bool& folded, std::string& message) const override;
    int ScoreLines(const std::vector<std::string_view>& lines) const override;
};

class ViceCodec : public LineCodec
{
public:
    ViceCodec();

protected:
    Line ParseLine(std::string_view line, Symbol& out, std::string& message) const override;
    std::string WriteLine(const Symbol& s, bool& folded, std::string& message) const override;
    int ScoreLines(const std::vector<std::string_view>& lines) const override;
};

class SjasmEquCodec : public LineCodec
{
public:
    SjasmEquCodec();

protected:
    Line ParseLine(std::string_view line, Symbol& out, std::string& message) const override;
    std::string WriteLine(const Symbol& s, bool& folded, std::string& message) const override;
    int ScoreLines(const std::vector<std::string_view>& lines) const override;
};

class Z88dkDefcCodec : public LineCodec
{
public:
    Z88dkDefcCodec();

protected:
    Line ParseLine(std::string_view line, Symbol& out, std::string& message) const override;
    std::string WriteLine(const Symbol& s, bool& folded, std::string& message) const override;
    int ScoreLines(const std::vector<std::string_view>& lines) const override;
};
}  // namespace unrealasm::symbols::codecs
