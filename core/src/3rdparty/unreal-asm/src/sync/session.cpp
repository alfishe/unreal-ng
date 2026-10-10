#include "unrealasm/sync/session.h"

#include "unrealasm/registry.h"
#include "unrealasm/symbols/fromsource.h"

namespace unrealasm::sync
{
namespace
{
uint64_t Fnv1a(const std::vector<uint8_t>& bytes)
{
    uint64_t hash = 0xCBF29CE484222325ull;
    for (const uint8_t b : bytes)
    {
        hash ^= b;
        hash *= 0x100000001B3ull;
    }
    return hash;
}
}  // namespace

SyncSession::SyncSession(SessionOptions options) : _options(std::move(options)) {}

TickResult SyncSession::Tick(const MachineView& machine, uint64_t nowMs)
{
    TickResult result;
    const bool known = _descriptor != nullptr;
    if (!_options.assembler.empty())
        _descriptor = FindDescriptor(_options.assembler);
    else
    {
        // Who is there: the one identified before stays while its signatures match, else the probe decides
        const auto identifies = [&](const SyncDescriptor& d) {
            std::vector<uint8_t> bytes;
            for (const Signature& s : d.identify)
                if (!machine.Read(s.address, s.bytes.size(), bytes) || std::string(bytes.begin(), bytes.end()) != s.bytes)
                    return false;
            return !d.identify.empty();
        };
        if (!_descriptor || !identifies(*_descriptor))
        {
            const std::vector<ProbeCandidate> candidates = Probe(machine);
            _descriptor = nullptr;
            if (candidates.size() > 1 && candidates[1].score == candidates[0].score)
            {
                result.event = TickEvent::Ambiguous;
                _haveHash = false;
                return result;
            }
            if (!candidates.empty())
                _descriptor = candidates[0].descriptor;
        }
    }
    if (!_descriptor)
    {
        result.event = known ? TickEvent::Lost : TickEvent::None;
        _haveHash = false;
        _pending = false;
        _last = SyncText{};
        return result;
    }
    result.text = ReadText(machine, *_descriptor);
    if (!result.text.ok)
    {
        result.event = TickEvent::Unreadable;   // the last good text and any pending change stay
        return result;
    }
    const uint64_t hash = Fnv1a(result.text.file);
    if (!known || !_haveHash)
        result.event = TickEvent::Found;
    else if (hash != _hash)
        result.event = TickEvent::Changed;
    if (result.event != TickEvent::None)
    {
        _pending = true;   // a first sight is built too, after the same quiet period
        _changedAt = nowMs;
    }
    _hash = hash;
    _haveHash = true;
    _last = result.text;
    return result;
}

std::optional<BuildInput> SyncSession::TakeBuild(uint64_t nowMs)
{
    if (!_pending || !_descriptor || !_last.ok || nowMs < _changedAt + _options.quietMs)
        return std::nullopt;
    _pending = false;
    BuildInput input;
    input.descriptor = _descriptor;
    input.file = _last.file;
    input.name = _last.name;
    input.generation = ++_generation;
    return input;
}

BuildResult SyncSession::Build(const BuildInput& input)
{
    BuildResult result;
    result.generation = input.generation;
    const ISourceCodec* codec = input.descriptor ? CodecRegistry::Builtin().Find(input.descriptor->codec) : nullptr;
    if (!codec)
    {
        result.hints.push_back({Severity::Error, 0, 0, "no codec for the assembler"});
        return result;
    }
    DecodeOptions options;
    options.subversion = input.descriptor->version;
    DecodeResult decoded = codec->Decode(input.file, options);
    result.hints = decoded.diagnostics;
    if (!decoded.ok)
        return result;
    result.decoded = true;
    result.document = std::move(decoded.document);
    result.document.name = input.name.empty() ? std::string("live") : input.name;
    symbols::SourceSymbolsResult symbols = symbols::SymbolsFromSource(result.document);
    result.labels = std::move(symbols.set);
    result.hints.insert(result.hints.end(), symbols.diagnostics.begin(), symbols.diagnostics.end());
    result.complete = symbols.ok;
    return result;
}

bool SyncSession::NeedsAllPages() const
{
    return !_descriptor || !_last.ok;
}

std::vector<int> SyncSession::TextPages() const
{
    if (_last.ok && _last.page >= 0)
        return {_last.page};
    return {};
}
}  // namespace unrealasm::sync
