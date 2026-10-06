#pragma once

// Every codec of the library (decision D-6: compiled in, one line each) and format detection (architecture.md DT-1).

#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "unrealasm/codec.h"

namespace unrealasm
{
struct DetectCandidate
{
    const ISourceCodec* codec = nullptr;
    int score = 0;
};

struct DetectResult
{
    std::vector<DetectCandidate> candidates;   ///< by score, highest first (score > 0 only)
    const ISourceCodec* chosen = nullptr;      ///< null when ambiguous or unknown
    std::string reason;                        ///< why nothing was chosen
};

class CodecRegistry
{
public:
    /// Every built-in codec
    static const CodecRegistry& Builtin();

    const std::vector<std::unique_ptr<ISourceCodec>>& All() const { return _codecs; }
    const ISourceCodec* Find(std::string_view id) const;
    /// The best codec when it scores at least 60 and at least 15 above the next; otherwise none, with the candidates
    DetectResult Detect(std::span<const uint8_t> bytes, const CatalogHints& hints = {}) const;

    void Add(std::unique_ptr<ISourceCodec> codec);

    static constexpr int kMinScore = 60;
    static constexpr int kMinLead = 15;

private:
    std::vector<std::unique_ptr<ISourceCodec>> _codecs;
};
}  // namespace unrealasm
