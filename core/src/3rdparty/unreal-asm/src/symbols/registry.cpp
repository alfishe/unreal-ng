#include <algorithm>

#include "symbols/codecs/crossasm/crossasmcodecs.h"
#include "symbols/codecs/labelfiles/labelfilecodecs.h"
#include "symbols/codecs/native/nativecodec.h"
#include "symbols/codecs/script/scriptcodecs.h"
#include "unrealasm/symbols/codec.h"

namespace unrealasm::symbols
{
const SymbolCodecRegistry& SymbolCodecRegistry::Builtin()
{
    static const SymbolCodecRegistry registry = [] {
        SymbolCodecRegistry r;
        // One line per codec
        r.Add(std::make_unique<codecs::NativeCodec>());
        r.Add(std::make_unique<codecs::UnrealMapCodec>());
        r.Add(std::make_unique<codecs::SimpleSymCodec>());
        r.Add(std::make_unique<codecs::UnrealLCodec>());
        r.Add(std::make_unique<codecs::ViceCodec>());
        r.Add(std::make_unique<codecs::SjasmEquCodec>());
        r.Add(std::make_unique<codecs::Z88dkDefcCodec>());
        r.Add(std::make_unique<codecs::SjasmplusSymCodec>());
        r.Add(std::make_unique<codecs::SjasmplusSldCodec>());
        r.Add(std::make_unique<codecs::SjasmplusLstCodec>());
        r.Add(std::make_unique<codecs::PasmoCodec>());
        r.Add(std::make_unique<codecs::Z88dkMapCodec>());
        r.Add(std::make_unique<codecs::CspectMapCodec>());
        r.Add(std::make_unique<codecs::IdaCodec>(false));
        r.Add(std::make_unique<codecs::IdaCodec>(true));
        r.Add(std::make_unique<codecs::GhidraCodec>());
        r.Add(std::make_unique<codecs::MameCodec>());
        return r;
    }();
    return registry;
}

void SymbolCodecRegistry::Add(std::unique_ptr<ISymbolCodec> codec)
{
    _codecs.push_back(std::move(codec));
}

const ISymbolCodec* SymbolCodecRegistry::Find(std::string_view id) const
{
    for (const auto& codec : _codecs)
        if (codec->Info().id == id)
            return codec.get();
    return nullptr;
}

SymbolDetectResult SymbolCodecRegistry::Detect(std::span<const uint8_t> bytes, std::string_view extension) const
{
    SymbolDetectResult result;
    Probe probe;
    probe.bytes = bytes.first(std::min(bytes.size(), kProbeBytes));
    probe.extension = std::string(extension);
    std::transform(probe.extension.begin(), probe.extension.end(), probe.extension.begin(),
                   [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    for (const auto& codec : _codecs)
        if (const int score = codec->Detect(probe); score > 0)
            result.candidates.push_back({codec.get(), score});
    std::stable_sort(result.candidates.begin(), result.candidates.end(),
                     [](const SymbolDetectCandidate& a, const SymbolDetectCandidate& b) { return a.score > b.score; });
    if (result.candidates.empty())
        result.reason = "no symbol format recognizes the file";
    else if (result.candidates[0].score < kMinScore)
        result.reason = "unknown symbol format (best guess " + result.candidates[0].codec->Info().id + ", score " +
                        std::to_string(result.candidates[0].score) + ")";
    else if (result.candidates.size() > 1 && result.candidates[0].score - result.candidates[1].score < kMinLead)
        result.reason = "ambiguous: " + result.candidates[0].codec->Info().id + " or " + result.candidates[1].codec->Info().id;
    else
        result.chosen = result.candidates[0].codec;
    return result;
}
}  // namespace unrealasm::symbols
