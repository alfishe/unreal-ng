#pragma once

// A source codec (decision D-1, D-15): one per source format, decoding bytes into a SourceDocument and encoding a
// document back into bytes. It detects and supports every version of its format (CodecInfo::subversions): decoding
// records the version found, encoding writes the version asked for. Decoding then encoding with the same codec gives
// the input bytes.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/document.h"
#include "unrealasm/encoding.h"

namespace unrealasm
{
/// What a container says about a file (a TR-DOS catalog entry); empty when the bytes come from a host file
struct CatalogHints
{
    char type = 0;               ///< TR-DOS type letter ('A', 'H', 'C', ...); 0 = unknown
    uint16_t start = 0;          ///< the catalog's start field
    uint16_t length = 0;         ///< the catalog's length field
    std::string name;            ///< file name without the type
    std::string extension;       ///< host file extension without the dot ("asm", "a80", "$A"); "" = unknown
    /// The bytes after `length` up to the end of the file's last sector. A format whose program ignores the catalog
    /// length (XAS) reads its text from them too; the others leave them alone
    std::vector<uint8_t> slack;
};

enum class CodecFamily : uint8_t
{
    Text,
    Tokenized,
};

/// One version of a format a codec reads and writes
struct Subversion
{
    std::string id;              ///< "3", "4.44", "5.07", ...
    std::string title;           ///< "TASM 3.x", "ALASM 5.07-5.09", ...
};

struct CodecInfo
{
    std::string id;              ///< "text", "sjasmplus", "tasm3", ...
    std::string title;           ///< for lists and reports
    std::string dialect;         ///< the dialect it holds ("" for the generic text codec)
    CodecFamily family = CodecFamily::Text;
    std::vector<Subversion> subversions;   ///< oldest first; empty when the format has one version
};

struct DecodeOptions
{
    std::optional<encoding::CodePage> codePage;   ///< force the code page (text codecs); otherwise detected
    std::string dialect;                          ///< the dialect to record (generic text codec)
    std::string subversion;                       ///< read as this version (CodecInfo::subversions id); "" = detect
    CatalogHints catalog;                         ///< what the container says (helps the version detection)
};

struct DecodeResult
{
    SourceDocument document;
    /// Every version of the format the bytes are consistent with, oldest first; document.subversion is one of them
    /// (the newest unless DecodeOptions::subversion chose). Empty when the format has one version
    std::vector<std::string> subversions;
    Diagnostics diagnostics;
    bool ok = false;
};

struct EncodeOptions
{
    std::optional<encoding::CodePage> codePage;   ///< override the document's code page (text codecs)
    std::optional<encoding::LineEnd> lineEnd;     ///< override the document's line end (text codecs)
    std::string subversion;                       ///< write this version; "" = the document's own (same format), else the newest
};

struct EncodeResult
{
    std::vector<uint8_t> bytes;
    Diagnostics diagnostics;
    bool ok = false;
};

class ISourceCodec
{
public:
    virtual ~ISourceCodec() = default;
    virtual const CodecInfo& Info() const = 0;
    /// 0..100: how sure the codec is that it can read these bytes
    virtual int Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const = 0;
    virtual DecodeResult Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const = 0;
    virtual EncodeResult Encode(const SourceDocument& document, const EncodeOptions& options) const = 0;
};
}  // namespace unrealasm
