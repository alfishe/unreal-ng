// unreal-asm benchmarks for the source side (test-and-benchmark-plan.md §5): decode and encode per codec, parse per
// dialect, conversion to sjasmplus, on the real files of testdata/ (decision D-14). Targets: NFR-1 (decode / encode
// >= 50 MB/s), NFR-2 (parse / convert >= 200 000 lines/s).

#include <benchmark/benchmark.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "unrealasm/containers.h"
#include "unrealasm/dialect.h"
#include "unrealasm/registry.h"

using namespace unrealasm;

namespace
{
struct Sample
{
    containers::TrdosFile file;
    SourceDocument document;
};

/// The hobeta sources of testdata/<folder> the codec reads (decoded once)
const std::vector<Sample>& Samples(const std::string& codecId, const std::string& folder)
{
    static std::map<std::string, std::vector<Sample>> cache;
    auto found = cache.find(codecId);
    if (found != cache.end())
        return found->second;
    std::vector<Sample>& out = cache[codecId];
    const ISourceCodec* codec = CodecRegistry::Builtin().Find(codecId);
    const std::filesystem::path dir = std::filesystem::path(UNREAL_ASM_TESTDATA_DIR) / folder;
    if (!codec || !std::filesystem::is_directory(dir))
        return out;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.path().filename().string().find(".$") == std::string::npos)
            continue;
        std::ifstream in(entry.path(), std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Sample s;
        std::string error;
        if (!containers::ReadHobeta(bytes, s.file, error))
            continue;
        DecodeOptions options;
        options.catalog = s.file.Hints();
        DecodeResult decoded = codec->Decode(s.file.data, options);
        if (!decoded.ok || decoded.document.lines.empty())
            continue;
        s.document = std::move(decoded.document);
        out.push_back(std::move(s));
    }
    return out;
}

struct CodecCase
{
    const char* id;
    const char* folder;
};

constexpr CodecCase kCodecs[] = {{"tasm", "tasm"}, {"alasm", "alasm"}, {"zxasm", "zxasm"}, {"storm", "storm"}, {"masm", "masm"},
                                 {"gens", "gens"}, {"zeus", "zeus"},   {"xas", "xas"}};

void BmDecode(benchmark::State& state, const CodecCase c)
{
    const ISourceCodec* codec = CodecRegistry::Builtin().Find(c.id);
    const std::vector<Sample>& samples = Samples(c.id, c.folder);
    if (!codec || samples.empty())
    {
        state.SkipWithError("no samples");
        return;
    }
    int64_t bytes = 0;
    for (auto _ : state)
        for (const Sample& s : samples)
        {
            DecodeOptions options;
            options.catalog = s.file.Hints();
            benchmark::DoNotOptimize(codec->Decode(s.file.data, options));
            bytes += static_cast<int64_t>(s.file.data.size() + options.catalog.slack.size());   // XAS reads whole sectors
        }
    state.SetBytesProcessed(bytes);
}

/// The same with the version given (as a caller that knows it passes DecodeOptions::subversion): no version detection
void BmDecodeKnown(benchmark::State& state, const CodecCase c)
{
    const ISourceCodec* codec = CodecRegistry::Builtin().Find(c.id);
    const std::vector<Sample>& samples = Samples(c.id, c.folder);
    if (!codec || samples.empty())
    {
        state.SkipWithError("no samples");
        return;
    }
    int64_t bytes = 0;
    for (auto _ : state)
        for (const Sample& s : samples)
        {
            DecodeOptions options;
            options.catalog = s.file.Hints();
            options.subversion = s.document.subversion;
            benchmark::DoNotOptimize(codec->Decode(s.file.data, options));
            bytes += static_cast<int64_t>(s.file.data.size() + options.catalog.slack.size());
        }
    state.SetBytesProcessed(bytes);
}

void BmEncode(benchmark::State& state, const CodecCase c)
{
    const ISourceCodec* codec = CodecRegistry::Builtin().Find(c.id);
    const std::vector<Sample>& samples = Samples(c.id, c.folder);
    if (!codec || samples.empty())
    {
        state.SkipWithError("no samples");
        return;
    }
    int64_t bytes = 0;
    for (auto _ : state)
        for (const Sample& s : samples)
        {
            const EncodeResult r = codec->Encode(s.document, {});
            benchmark::DoNotOptimize(r.bytes.data());
            bytes += static_cast<int64_t>(r.bytes.size());
        }
    state.SetBytesProcessed(bytes);
}

/// Parse (frontend) and convert (frontend + sjasmplus backend) of the codec's samples, counted in source lines
void BmParse(benchmark::State& state, const CodecCase c)
{
    const std::vector<Sample>& samples = Samples(c.id, c.folder);
    const IFrontend* frontend = samples.empty() ? nullptr : DialectRegistry::Builtin().Frontend(samples.front().document.dialect);
    if (!frontend)
    {
        state.SkipWithError("no frontend");
        return;
    }
    int64_t lines = 0;
    for (auto _ : state)
        for (const Sample& s : samples)
        {
            benchmark::DoNotOptimize(frontend->Parse(s.document));
            lines += static_cast<int64_t>(s.document.lines.size());
        }
    state.counters["lines/s"] = benchmark::Counter(static_cast<double>(lines), benchmark::Counter::kIsRate);
}

void BmConvert(benchmark::State& state, const CodecCase c)
{
    const std::vector<Sample>& samples = Samples(c.id, c.folder);
    if (samples.empty() || !DialectRegistry::Builtin().Frontend(samples.front().document.dialect))
    {
        state.SkipWithError("no frontend");
        return;
    }
    int64_t lines = 0;
    for (auto _ : state)
        for (const Sample& s : samples)
        {
            benchmark::DoNotOptimize(Convert(s.document, "sjasmplus"));
            lines += static_cast<int64_t>(s.document.lines.size());
        }
    state.counters["lines/s"] = benchmark::Counter(static_cast<double>(lines), benchmark::Counter::kIsRate);
}

const bool kRegistered = [] {
    for (const CodecCase& c : kCodecs)
    {
        benchmark::RegisterBenchmark((std::string("BM_Asm_Decode/") + c.id).c_str(), BmDecode, c);
        benchmark::RegisterBenchmark((std::string("BM_Asm_DecodeKnown/") + c.id).c_str(), BmDecodeKnown, c);
        benchmark::RegisterBenchmark((std::string("BM_Asm_Encode/") + c.id).c_str(), BmEncode, c);
        benchmark::RegisterBenchmark((std::string("BM_Asm_Parse/") + c.id).c_str(), BmParse, c);
        benchmark::RegisterBenchmark((std::string("BM_Asm_Convert/") + c.id + "-sjasmplus").c_str(), BmConvert, c);
    }
    return true;
}();
}  // namespace
