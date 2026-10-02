// CHD hard disks (docs/inprogress/2026-10-02-media-chd/design.md §6): what a guest sector read costs on a CHD
// compared with a raw image, the decode cost per codec when the hunk cache misses, and the writer per codec.
// Fixtures: testdata/media/chd/ (64 hunks of 4 KB, made by chdman).

#include <benchmark/benchmark.h>

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#include "emulator/io/storage/chd/chdimage.h"
#include "emulator/io/storage/chd/chdwriter.h"
#include "emulator/io/storage/rawimage.h"

namespace
{
    std::string FixturePath(const std::string& name)
    {
        std::filesystem::path dir = std::filesystem::current_path();
        for (int i = 0; i < 8; i++)
        {
            const std::filesystem::path candidate = dir / "testdata" / "media" / "chd" / name;
            if (std::filesystem::exists(candidate))
            {
                const auto u8 = candidate.u8string();
                return std::string(u8.begin(), u8.end());
            }
            if (!dir.has_parent_path() || dir.parent_path() == dir)
                break;
            dir = dir.parent_path();
        }
        return {};
    }

    std::unique_ptr<IBlockDevice> OpenDevice(const std::string& name)
    {
        const std::string path = FixturePath(name);
        if (path.empty())
            return nullptr;
        if (name.size() > 4 && name.substr(name.size() - 4) == ".img")
            return RawImage::Open(path, RawImage::Access::ReadOnly);
        return ChdImage::Open(path);
    }

    /// Sector reads as a DOS does them: runs of consecutive sectors over the whole disk
    void SequentialReads(benchmark::State& state, const std::string& name)
    {
        auto device = OpenDevice(name);
        if (!device)
        {
            state.SkipWithError("fixture missing (run from the repository)");
            return;
        }
        uint8_t sector[512];
        uint64_t lba = 0;
        for (auto _ : state)
        {
            device->ReadSector(lba, sector);
            benchmark::DoNotOptimize(sector[0]);
            lba = (lba + 1) % device->SectorCount();
        }
        state.SetItemsProcessed(state.iterations());
    }
}  // namespace

static void BM_SectorRead_RawImage(benchmark::State& state) { SequentialReads(state, "mixed.img"); }
static void BM_SectorRead_ChdUncompressed(benchmark::State& state) { SequentialReads(state, "mixed-none.chd"); }
static void BM_SectorRead_ChdDefaultCodecs(benchmark::State& state) { SequentialReads(state, "mixed-default.chd"); }
BENCHMARK(BM_SectorRead_RawImage);
BENCHMARK(BM_SectorRead_ChdUncompressed);
BENCHMARK(BM_SectorRead_ChdDefaultCodecs);

/// A hunk decoded from the file (a cache miss): one fixture per codec
static void BM_HunkDecode(benchmark::State& state, const char* name)
{
    const std::string path = FixturePath(name);
    auto file = path.empty() ? nullptr : chd::ChdFile::Open(path);
    if (!file)
    {
        state.SkipWithError("fixture missing (run from the repository)");
        return;
    }
    std::vector<uint8_t> hunk(file->HunkBytes());
    uint32_t h = 0;
    for (auto _ : state)
    {
        file->ReadHunk(h, hunk.data());
        benchmark::DoNotOptimize(hunk[0]);
        h = (h + 1) % file->HunkCount();
    }
    state.SetBytesProcessed(state.iterations() * file->HunkBytes());
}
BENCHMARK_CAPTURE(BM_HunkDecode, none, "mixed-none.chd");
BENCHMARK_CAPTURE(BM_HunkDecode, zlib, "mixed-zlib.chd");
BENCHMARK_CAPTURE(BM_HunkDecode, lzma, "mixed-lzma.chd");
BENCHMARK_CAPTURE(BM_HunkDecode, huff, "mixed-huff.chd");
BENCHMARK_CAPTURE(BM_HunkDecode, flac, "mixed-flac.chd");
BENCHMARK_CAPTURE(BM_HunkDecode, zstd, "mixed-zstd.chd");

/// Writing the 256 KB disk as a CHD with one codec set
static void BM_ChdWrite(benchmark::State& state, const char* codecs)
{
    auto device = OpenDevice("mixed.img");
    if (!device)
    {
        state.SkipWithError("fixture missing (run from the repository)");
        return;
    }
    chd::WriteOptions options;
    chd::ParseCodecList(codecs, options.codecs);
    const std::string out = (std::filesystem::temp_directory_path() / "unreal-chd-benchmark.chd").string();
    for (auto _ : state)
        chd::WriteChd(out, *device, options);
    std::filesystem::remove(out);
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(device->SectorCount() * 512));
}
BENCHMARK_CAPTURE(BM_ChdWrite, none, "none")->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ChdWrite, zlib, "zlib")->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ChdWrite, lzma, "lzma")->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ChdWrite, huff, "huff")->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ChdWrite, flac, "flac")->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ChdWrite, zstd, "zstd")->Unit(benchmark::kMillisecond);
BENCHMARK_CAPTURE(BM_ChdWrite, defaults, "default")->Unit(benchmark::kMillisecond);
