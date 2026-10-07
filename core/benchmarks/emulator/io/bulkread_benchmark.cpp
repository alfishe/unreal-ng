// C9 research (multi-source phases/c9-bulk-read.md): what a bulk `ReadSectors` could save.
// - one sector at a time through the device stacks a guest reads from (RawImage, a session over it, a composite
//   built from a host folder, a session over that);
// - the same file read N sectors per call (what RawImage::ReadSectors would do), per sector;
// The guest's own cost per sector (512 port reads) comes from BM_PortIn (portin_benchmark.cpp).
// Fixtures are generated once per process in the system temp folder.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "emulator/io/storage/rawimage.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/composedescriptor.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    constexpr uint64_t kFileSectors = 32ull * 1024 * 2;  // 32 MiB

    const std::filesystem::path& Folder()
    {
        static const std::filesystem::path dir = [] {
            const auto d = std::filesystem::temp_directory_path() / "unreal-ng-bulk-bench";
            std::error_code ec;
            std::filesystem::remove_all(d, ec);
            std::filesystem::create_directories(d / "files");
            std::vector<uint8_t> data(kFileSectors * 512);
            std::mt19937 random(3);
            for (uint8_t& b : data)
                b = static_cast<uint8_t>(random() | 1);
            std::ofstream(d / "data.img", std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                                                   static_cast<std::streamsize>(data.size()));
            // The same bytes as one host file inside a composite
            std::ofstream(d / "files" / "BIG.BIN", std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                                                            static_cast<std::streamsize>(data.size()));
            std::ofstream(d / "folder.ucompose.yaml") << "version: 1\ntarget: {fs: fat32, free: 1MiB, build: rebuild}\n"
                                                         "layers: [{source: {folder: files}}]\n";
            return d;
        }();
        return dir;
    }

    std::unique_ptr<IBlockDevice> Composite()
    {
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        if (!CompositeMediumFactory::Build(ComposeDescriptor::Load(Folder() / "folder.ucompose.yaml"), {}, volume, info).Ok())
            return nullptr;
        return volume;
    }

    /// The first sector of BIG.BIN in the composite: the first sector that is not metadata and reads non-zero
    /// data, past the FATs and the root
    uint64_t DataStart(IBlockDevice& device)
    {
        uint8_t sector[512];
        for (uint64_t lba = 0; lba < device.SectorCount(); lba++)
        {
            device.ReadSector(lba, sector);
            bool all = true;  // BIG.BIN's bytes are all odd
            for (uint8_t b : sector)
                all = all && (b & 1);
            if (all)
                return lba;
        }
        return 0;
    }

    /// Sequential single-sector reads over a window of `count` sectors from `first`
    void Sequential(benchmark::State& state, IBlockDevice& device, uint64_t first, uint64_t count)
    {
        uint8_t sector[512];
        uint64_t lba = first;
        for (auto _ : state)
        {
            device.ReadSector(lba, sector);
            benchmark::DoNotOptimize(sector[0]);
            if (++lba == first + count)
                lba = first;
        }
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    }

    // --- one sector per call, through each stack ---

    void SingleRawImage(benchmark::State& state)
    {
        auto image = RawImage::Open((Folder() / "data.img").string(), RawImage::Access::ReadOnly);
        Sequential(state, *image, 0, kFileSectors);
    }

    void SingleSessionOverRawImage(benchmark::State& state)
    {
        SessionWriteMap session(RawImage::Open((Folder() / "data.img").string(), RawImage::Access::ReadOnly));
        Sequential(state, session, 0, kFileSectors);
    }

    void SingleComposite(benchmark::State& state)
    {
        auto volume = Composite();
        if (!volume)
        {
            state.SkipWithError("fixture");
            return;
        }
        const uint64_t first = DataStart(*volume);
        Sequential(state, *volume, first, kFileSectors);
    }

    void SingleSessionOverComposite(benchmark::State& state)
    {
        auto volume = Composite();
        if (!volume)
        {
            state.SkipWithError("fixture");
            return;
        }
        const uint64_t first = DataStart(*volume);
        SessionWriteMap session(std::move(volume));
        Sequential(state, session, first, kFileSectors);
    }

    // --- N sectors per call from the same file (what a RawImage::ReadSectors would do), per sector ---

    void BulkFile(benchmark::State& state)
    {
        const uint64_t n = static_cast<uint64_t>(state.range(0));
        std::ifstream file(Folder() / "data.img", std::ios::binary);
        std::vector<uint8_t> buffer(n * 512);
        uint64_t lba = 0;
        for (auto _ : state)
        {
            file.clear();
            file.seekg(static_cast<std::streamoff>(lba * 512));
            file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            benchmark::DoNotOptimize(buffer[0]);
            lba += n;
            if (lba + n > kFileSectors)
                lba = 0;
        }
        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(n));
    }
}  // namespace

BENCHMARK(SingleRawImage);
BENCHMARK(SingleSessionOverRawImage);
BENCHMARK(SingleComposite);
BENCHMARK(SingleSessionOverComposite);
BENCHMARK(BulkFile)->Arg(1)->Arg(8)->Arg(128)->Arg(256);
