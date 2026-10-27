#pragma once

// A symbol file codec (symbols/tdd.md §4, decision D-1): one per format, decoding bytes into a SymbolFile and encoding
// sets back; every codec does both. The registry detects the format from the bytes (the extension a hint).

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/symbols/namerules.h"
#include "unrealasm/symbols/symbol.h"

namespace unrealasm::symbols
{
enum class Family : uint8_t
{
    Text,       ///< one symbol per line in a tool's text format
    Script,     ///< a program for another tool that sets names (IDC, IDAPython, Ghidra, MAME)
    Native,     ///< the lossless *.usym.json
};

struct CodecInfo
{
    std::string id;             ///< "native", "unreal-map", "sjasmplus-sym", ...
    std::string title;
    Family family = Family::Text;
    std::vector<std::string> extensions;   ///< usual extensions without the dot, lower case (detection hint)
    NameRules rules;                       ///< what names the format takes (DT-3)
    bool pages = false;                    ///< holds page symbols (else DT-4 applies)
    std::string comment;                   ///< what starts a comment line ("" = the format has none)
};

struct Probe
{
    std::span<const uint8_t> bytes;        ///< the start of the file (detection reads at most the first 4 KB)
    std::string extension;                 ///< without the dot; "" = unknown
};

struct SymbolDecodeResult
{
    SymbolFile file;            ///< the native file: its sets; another format: one set without an id (the importer names it)
    Diagnostics diagnostics;
    bool ok = false;
};

struct SymbolEncodeOptions
{
    std::string lineEnd = "\n";
    Unrepresentable unrepresentable = Unrepresentable::Fold;   ///< a page symbol in a format without pages (DT-4)
};

struct SymbolEncodeResult
{
    std::vector<uint8_t> bytes;
    size_t written = 0;
    Diagnostics diagnostics;    ///< what the format could not hold, renames
    bool ok = false;
};

/// A file made ready for a format: names by its rules (DT-3), page symbols by the options (DT-4); `header` holds the
/// comment lines (renames, commented symbols) the codec writes first, already with the format's comment prefix
struct PreparedFile
{
    SymbolFile file;
    std::vector<std::string> header;
};
PreparedFile Prepare(const SymbolFile& file, const CodecInfo& info, const SymbolEncodeOptions& options, Diagnostics& diagnostics);

class ISymbolCodec
{
public:
    virtual ~ISymbolCodec() = default;
    virtual const CodecInfo& Info() const = 0;
    /// 0..100: how sure the codec is that it can read these bytes
    virtual int Detect(const Probe& probe) const = 0;
    virtual SymbolDecodeResult Decode(std::span<const uint8_t> bytes) const = 0;
    virtual SymbolEncodeResult Encode(const SymbolFile& file, const SymbolEncodeOptions& options) const = 0;
};

struct SymbolDetectCandidate
{
    const ISymbolCodec* codec = nullptr;
    int score = 0;
};

struct SymbolDetectResult
{
    std::vector<SymbolDetectCandidate> candidates;   ///< by score, highest first (score > 0 only)
    const ISymbolCodec* chosen = nullptr;            ///< null when ambiguous or unknown
    std::string reason;
};

class SymbolCodecRegistry
{
public:
    static const SymbolCodecRegistry& Builtin();

    const std::vector<std::unique_ptr<ISymbolCodec>>& All() const { return _codecs; }
    const ISymbolCodec* Find(std::string_view id) const;
    /// The best codec when it scores at least 60 and at least 15 above the next (architecture.md §4)
    SymbolDetectResult Detect(std::span<const uint8_t> bytes, std::string_view extension = {}) const;

    void Add(std::unique_ptr<ISymbolCodec> codec);

    static constexpr int kMinScore = 60;
    static constexpr int kMinLead = 15;
    static constexpr size_t kProbeBytes = 4096;

private:
    std::vector<std::unique_ptr<ISymbolCodec>> _codecs;
};
}  // namespace unrealasm::symbols
