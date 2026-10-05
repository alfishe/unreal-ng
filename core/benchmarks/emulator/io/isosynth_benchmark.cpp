// ISO 9660 targets (multi-source phases/c5-iso.md §7, NFR-P1): a file of a composite CD read through
// its CdImage, block by block (what the ATAPI drive does) and as 512-byte sectors (the block view the
// other benchmarks use), and the build. The fixture folder (200 small files, a 1 MiB file) is
// generated once per process in the system temp folder.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/io/storage/cd/iso9660reader.h"
#include "emulator/media/composedescriptor.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    const std::filesystem::path& Descriptor()
    {
        static const std::filesystem::path file = [] {
            const auto dir = std::filesystem::temp_directory_path() / "unreal-ng-iso-bench";
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::create_directories(dir / "files" / "zz");
            const std::string small(4096, 's');
            for (int i = 0; i < 200; i++)
                std::ofstream(dir / "files" / ("file" + std::to_string(i) + ".bin"), std::ios::binary) << small;
            std::ofstream(dir / "files" / "zz" / "big.bin", std::ios::binary) << std::string(1024 * 1024, 'b');
            std::ofstream(dir / "cd.ucompose.yaml") << "version: 1\ntarget: {kind: optical}\nlayers: [{source: {folder: files}}]\n";
            return dir / "cd.ucompose.yaml";
        }();
        return file;
    }

    /// The disc and big.bin's first block
    std::unique_ptr<IBlockDevice> Disc(uint32_t& first, uint32_t& blocks)
    {
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        if (!CompositeMediumFactory::Build(ComposeDescriptor::Load(Descriptor()), {}, volume, info).Ok())
            return nullptr;
        Iso9660Reader reader;
        IsoDirEntry entry;
        if (!reader.Open(*volume) || !reader.Stat("/zz/big.bin", entry))
            return nullptr;
        first = entry.Block();
        blocks = static_cast<uint32_t>(entry.size / 2048);
        return volume;
    }

    void IsoSynthSeqReadBlock(benchmark::State& state)
    {
        uint32_t first = 0, blocks = 0;
        auto volume = Disc(first, blocks);
        auto* disc = dynamic_cast<CdImage*>(volume.get());
        if (!disc)
        {
            state.SkipWithError("fixture");
            return;
        }
        uint8_t block[2048];
        uint32_t b = first;
        for (auto _ : state)
        {
            disc->ReadUser(b, block);
            benchmark::DoNotOptimize(block[0]);
            if (++b == first + blocks)
                b = first;
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 2048);
    }

    void IsoSynthSeqReadSector(benchmark::State& state)
    {
        uint32_t first = 0, blocks = 0;
        auto volume = Disc(first, blocks);
        if (!volume)
        {
            state.SkipWithError("fixture");
            return;
        }
        uint8_t sector[512];
        const uint64_t begin = static_cast<uint64_t>(first) * 4;
        const uint64_t end = begin + static_cast<uint64_t>(blocks) * 4;
        uint64_t lba = begin;
        for (auto _ : state)
        {
            volume->ReadSector(lba, sector);
            benchmark::DoNotOptimize(sector[0]);
            if (++lba == end)
                lba = begin;
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 512);
    }

    void IsoSynthBuild(benchmark::State& state)
    {
        const ComposeDescriptor descriptor = ComposeDescriptor::Load(Descriptor());
        for (auto _ : state)
        {
            std::unique_ptr<IBlockDevice> volume;
            CompositeInfo info;
            benchmark::DoNotOptimize(CompositeMediumFactory::Build(descriptor, {}, volume, info).Ok());
        }
    }
}  // namespace

BENCHMARK(IsoSynthSeqReadBlock);
BENCHMARK(IsoSynthSeqReadSector);
BENCHMARK(IsoSynthBuild)->Unit(benchmark::kMicrosecond);
