#include "unrealasm/registry.h"

#include <algorithm>

#include "codecs/alasm/alasmcodec.h"
#include "codecs/gens/genscodec.h"
#include "codecs/masm/masmcodec.h"
#include "codecs/sjasmplus/sjasmpluscodec.h"
#include "codecs/tasm/tasmcodec.h"
#include "codecs/xas/xascodec.h"
#include "codecs/storm/stormcodec.h"
#include "codecs/text/textcodec.h"
#include "codecs/zeus/zeuscodec.h"
#include "codecs/zxasm/zxasmcodec.h"

namespace unrealasm
{
const CodecRegistry& CodecRegistry::Builtin()
{
    static const CodecRegistry registry = [] {
        CodecRegistry r;
        // One line per codec (decision D-6)
        r.Add(std::make_unique<codecs::TextCodec>());
        r.Add(std::make_unique<codecs::SjasmplusCodec>());
        r.Add(std::make_unique<codecs::TasmCodec>());
        r.Add(std::make_unique<codecs::AlasmCodec>());
        r.Add(std::make_unique<codecs::ZxasmCodec>());
        r.Add(std::make_unique<codecs::StormCodec>());
        r.Add(std::make_unique<codecs::MasmCodec>());
        r.Add(std::make_unique<codecs::GensCodec>());
        r.Add(std::make_unique<codecs::ZeusCodec>());
        r.Add(std::make_unique<codecs::XasCodec>());
        return r;
    }();
    return registry;
}

void CodecRegistry::Add(std::unique_ptr<ISourceCodec> codec)
{
    _codecs.push_back(std::move(codec));
}

const ISourceCodec* CodecRegistry::Find(std::string_view id) const
{
    for (const auto& codec : _codecs)
        if (codec->Info().id == id)
            return codec.get();
    return nullptr;
}

DetectResult CodecRegistry::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    DetectResult result;
    for (const auto& codec : _codecs)
    {
        const int score = std::clamp(codec->Detect(bytes, hints), 0, 100);
        if (score > 0)
            result.candidates.push_back({codec.get(), score});
    }
    std::stable_sort(result.candidates.begin(), result.candidates.end(),
                     [](const DetectCandidate& a, const DetectCandidate& b) { return a.score > b.score; });
    if (result.candidates.empty())
    {
        result.reason = "no codec recognizes these bytes";
        return result;
    }
    const int best = result.candidates[0].score;
    const int next = result.candidates.size() > 1 ? result.candidates[1].score : 0;
    if (best < kMinScore)
        result.reason = "no codec is sure enough (best " + std::to_string(best) + " < " + std::to_string(kMinScore) + ")";
    else if (best - next < kMinLead)
        result.reason = "ambiguous: " + result.candidates[0].codec->Info().id + " " + std::to_string(best) + " vs " +
                        result.candidates[1].codec->Info().id + " " + std::to_string(next);
    else
        result.chosen = result.candidates[0].codec;
    return result;
}
}  // namespace unrealasm
