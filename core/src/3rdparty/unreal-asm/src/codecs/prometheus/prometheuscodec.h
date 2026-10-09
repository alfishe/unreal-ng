#pragma once

// PROMETHEUS (Proxima, Usti nad Labem, 1990-1993): the assembler-editor-monitor's own source save. The file (a tape
// CODE block, or a TR-DOS file of the disk adaptations) holds [source records][two bytes][symbol table]
// (research-prometheus.md). A record is one line: the opcode (or a pseudo-opcode 0-9) and an information byte
// (prefix family, a label flag, the storage class of the operand), then for a line with a label or an operand the
// label's symbol ordinal, the operand's characters with every name replaced by its ordinal (#80+high, low), and a
// marker #C0+length. The symbol table: the count, the ordinals' vectors, then value + name records. Names are upper
// case; the text shows mnemonics in lower case in PROMETHEUS' fields (label 9 columns, mnemonic 5).

#include <cstdint>
#include <string>
#include <vector>

#include "unrealasm/codec.h"

namespace unrealasm::codecs
{
class PrometheusCodec : public ISourceCodec
{
public:
    PrometheusCodec();
    const CodecInfo& Info() const override { return _info; }
    int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const override;
    DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const override;
    EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const override;

    struct Symbol
    {
        std::string name;
        uint16_t value = 0;
        bool defined = false;
        bool locked = false;
    };

    /// Where the records end in a whole save (the symbol table follows two bytes later); false when it is none
    static bool FindSourceLength(std::span<const uint8_t> bytes, size_t& length);
    /// The symbol table at the start of `bytes`, in ordinal order; `used` = its length; false when it is none
    static bool ReadSymbols(std::span<const uint8_t> bytes, std::vector<Symbol>& out, size_t& used);
    /// The text of the record at `pos` (`length` = its size); false when it is no valid record
    static bool DecodeRecord(std::span<const uint8_t> bytes, size_t pos, const std::vector<Symbol>& symbols, std::string& text, size_t& length);
    /// The record PROMETHEUS stores for a line of text (names become ordinals; a new name is added to `symbols`)
    static bool EncodeLine(const std::string& text, std::vector<Symbol>& symbols, std::vector<uint8_t>& out, std::string& error);
    /// The symbol table PROMETHEUS writes for these symbols (records sorted by name)
    static std::vector<uint8_t> WriteSymbols(const std::vector<Symbol>& symbols);

private:
    CodecInfo _info;
};
}  // namespace unrealasm::codecs
