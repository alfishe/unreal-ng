// unreal-asm benchmarks for the symbol module (symbols/test-and-benchmark-plan.md §5): text import end to end, the
// tokenizer, the index (build, exact and nearest lookup), export, the label-table scan, and the layout of sources.
// Targets: NFR-1 (lookup <= 100 ns at 100k), NFR-2 (import >= 1 M lines/s; 100k symbols < 300 ms end to end).

#include <benchmark/benchmark.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "symbols/codecs/text/tokenizer.h"
#include "unrealasm/dialect.h"
#include "unrealasm/layout.h"
#include "unrealasm/symbols/codec.h"
#include "unrealasm/symbols/index.h"
#include "unrealasm/symbols/live.h"

using namespace unrealasm;
using namespace unrealasm::symbols;

namespace
{
std::vector<uint8_t> ReadTestData(const std::string& relative)
{
    std::ifstream in(std::filesystem::path(UNREAL_ASM_TESTDATA_DIR) / relative, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string Hex4(uint32_t v)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string s(4, '0');
    for (int k = 3; k >= 0; --k, v >>= 4)
        s[static_cast<size_t>(k)] = kDigits[v & 0xF];
    return s;
}

/// A simple-sym file of n labels: "HHHH NAME" (the format LabelManager reads most)
std::vector<uint8_t> SymText(int64_t n)
{
    std::string text;
    text.reserve(static_cast<size_t>(n) * 16);
    for (int64_t k = 0; k < n; ++k)
        text += Hex4(static_cast<uint32_t>((k * 7) & 0xFFFF)) + " LABEL_" + std::to_string(k) + "\n";
    return std::vector<uint8_t>(text.begin(), text.end());
}

/// n symbols spread over the CPU view and eight RAM pages
std::shared_ptr<std::vector<SymbolSet>> Sets(int64_t n)
{
    auto sets = std::make_shared<std::vector<SymbolSet>>(1);
    std::mt19937 random(7);
    for (int64_t k = 0; k < n; ++k)
    {
        Symbol s;
        s.name = "L" + std::to_string(k);
        if (k % 2)
        {
            s.location.space.kind = SpaceKind::Ram;
            s.location.space.page = static_cast<uint16_t>(random() % 8);
            s.location.offset = random() % 0x4000;
        }
        else
            s.location.offset = random() % 0x10000;
        (*sets)[0].symbols.push_back(std::move(s));
    }
    return sets;
}

void BM_Symbols_ImportSym(benchmark::State& state)
{
    const std::vector<uint8_t> bytes = SymText(state.range(0));
    const ISymbolCodec* codec = SymbolCodecRegistry::Builtin().Find("simple-sym");
    for (auto _ : state)
    {
        SymbolDecodeResult decoded = codec->Decode(bytes);
        auto sets = std::make_shared<std::vector<SymbolSet>>(std::move(decoded.file.sets));
        SymbolIndex index(sets);
        benchmark::DoNotOptimize(index.Size());
    }
    state.counters["lines/s"] = benchmark::Counter(static_cast<double>(state.range(0)) * static_cast<double>(state.iterations()),
                                                   benchmark::Counter::kIsRate);
}
BENCHMARK(BM_Symbols_ImportSym)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000)->Unit(benchmark::kMillisecond);

void BM_Symbols_Tokenize(benchmark::State& state)
{
    const std::vector<std::string> lines = {"PLAYMUS: EQU 0x0000C000", "        LD   A,(IX+5) ; comment", "al C:4000 .SCREEN",
                                            "DEFC start = $8000 ; code", "ROM0:0010  PRINT-A-1  (CODE)"};
    int64_t n = 0;
    for (auto _ : state)
        for (const std::string& l : lines)
        {
            benchmark::DoNotOptimize(text::Tokenize(l));
            ++n;
        }
    state.counters["lines/s"] = benchmark::Counter(static_cast<double>(n), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_Symbols_Tokenize);

void BM_Symbols_IndexBuild(benchmark::State& state)
{
    const auto sets = Sets(state.range(0));
    for (auto _ : state)
    {
        SymbolIndex index(sets);
        benchmark::DoNotOptimize(index.Size());
    }
}
BENCHMARK(BM_Symbols_IndexBuild)->Arg(1000)->Arg(100000)->Unit(benchmark::kMillisecond);

void BM_Symbols_LookupAt(benchmark::State& state)
{
    const auto sets = Sets(state.range(0));
    const SymbolIndex index(sets);
    std::vector<Location> probes;
    for (size_t k = 0; k < 1024; ++k)
        probes.push_back((*sets)[0].symbols[(k * 977) % (*sets)[0].symbols.size()].location);
    size_t k = 0;
    for (auto _ : state)
        benchmark::DoNotOptimize(index.At(probes[k++ & 1023]));
}
BENCHMARK(BM_Symbols_LookupAt)->Arg(1000)->Arg(100000);

void BM_Symbols_LookupNearest(benchmark::State& state)
{
    const auto sets = Sets(state.range(0));
    const SymbolIndex index(sets);
    std::vector<Location> probes;
    for (size_t k = 0; k < 1024; ++k)
    {
        Location l = (*sets)[0].symbols[(k * 977) % (*sets)[0].symbols.size()].location;
        l.offset += 3;
        probes.push_back(l);
    }
    size_t k = 0;
    for (auto _ : state)
        benchmark::DoNotOptimize(index.NearestBelow(probes[k++ & 1023], 256));
}
BENCHMARK(BM_Symbols_LookupNearest)->Arg(1000)->Arg(100000);

void BM_Symbols_Export(benchmark::State& state, const char* id)
{
    SymbolFile file;
    file.sets = *Sets(state.range(0));
    const ISymbolCodec* codec = SymbolCodecRegistry::Builtin().Find(id);
    for (auto _ : state)
        benchmark::DoNotOptimize(codec->Encode(file, {}).bytes.size());
}
BENCHMARK_CAPTURE(BM_Symbols_Export, unreal_map, "unreal-map")->Arg(100000)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_Symbols_Export, sjasmplus_sym, "sjasmplus-sym")->Arg(100000)->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_Symbols_Export, native, "native")->Arg(100000)->Unit(benchmark::kMillisecond);

/// The label-table scan over 128K / 1M / 4M of RAM: random pages and one page with an ALASM 5.09 table
void BM_Symbols_LiveScan(benchmark::State& state)
{
    const int64_t pages = state.range(0);
    std::vector<std::vector<uint8_t>> storage(static_cast<size_t>(pages), std::vector<uint8_t>(0x4000));
    std::mt19937 random(11);
    for (auto& page : storage)
        for (uint8_t& b : page)
            b = static_cast<uint8_t>(random());
    storage[3] = ReadTestData("symbols/live/alasm509-lta-ram3.bin");
    MemoryView view;
    for (size_t k = 0; k < storage.size(); ++k)
        view.pages.push_back({static_cast<uint16_t>(k), storage[k]});
    for (auto _ : state)
        benchmark::DoNotOptimize(FindLabelTables(view));
    state.SetBytesProcessed(state.iterations() * pages * 0x4000);
}
BENCHMARK(BM_Symbols_LiveScan)->Arg(8)->Arg(64)->Arg(256)->Unit(benchmark::kMillisecond);

/// The layout of a sjasmplus source (every instruction form, 600 lines) - labels from sources
void BM_Symbols_Layout(benchmark::State& state)
{
    const std::vector<uint8_t> bytes = ReadTestData("symbols/fromsource/instructions.asm");
    const SourceDocument document = SourceDocument::FromText(std::string(bytes.begin(), bytes.end()), "sjasmplus");
    int64_t lines = 0;
    for (auto _ : state)
    {
        benchmark::DoNotOptimize(layout::Layout({{"instructions", document}}, 0));
        lines += static_cast<int64_t>(document.lines.size());
    }
    state.counters["lines/s"] = benchmark::Counter(static_cast<double>(lines), benchmark::Counter::kIsRate);
}
BENCHMARK(BM_Symbols_Layout)->Unit(benchmark::kMicrosecond);
}  // namespace
