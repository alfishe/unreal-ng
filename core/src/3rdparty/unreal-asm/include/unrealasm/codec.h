#pragma once

// A source codec (decision D-1): one per source format and sub-version, decoding bytes into a SourceDocument and
// encoding a document back into bytes. Decoding then encoding with the same codec gives the input bytes.

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
};

enum class CodecFamily : uint8_t
{
    Text,
    Tokenized,
};

struct CodecInfo
{
    std::string id;              ///< "text", "sjasmplus", "tasm3", ...
    std::string title;           ///< for lists and reports
    std::string dialect;         ///< the dialect it holds ("" for the generic text codec)
    CodecFamily family = CodecFamily::Text;
};

struct DecodeOptions
{
    std::optional<encoding::CodePage> codePage;   ///< force the code page (text codecs); otherwise detected
    std::string dialect;                          ///< the dialect to record (generic text codec)
};

struct DecodeResult
{
    SourceDocument document;
    Diagnostics diagnostics;
    bool ok = false;
};

struct EncodeOptions
{
    std::optional<encoding::CodePage> codePage;   ///< override the document's code page (text codecs)
    std::optional<encoding::LineEnd> lineEnd;     ///< override the document's line end (text codecs)
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
