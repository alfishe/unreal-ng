// Composite media with a FAT image layer (multi-source phases/c3-image-sources.md §7,
// NFR-P1): the same files as the HostFolderFat benchmarks, but read from a FAT16 disk
// image through FatImageSource extents instead of host files. Compare with
// HostFolderFatSeqRead / RandRead / Build (budget: within 10%).
// The fixture (folder, image, descriptor) is generated once per process in the
// system temp folder.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"
#include "emulator/media/composedescriptor.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    /// The HostFolderFat benchmark's file set, as a FAT16 image plus a descriptor naming it
    const std::filesystem::path& Descriptor()
    {
        static const std::filesystem::path file = [] {
            const auto dir = std::filesystem::temp_directory_path() / "unreal-ng-compose-bench";
            const auto folder = dir / "files";
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::create_directories(folder / "deep" / "er");
            std::filesystem::create_directories(folder / "zz");
            std::string small(4096, 's');
            for (int i = 0; i < 200; i++)
                std::ofstream(folder / ("file" + std::to_string(i) + ".bin"), std::ios::binary) << small;
            for (int i = 0; i < 20; i++)
                std::ofstream(folder / "deep" / "er" / ("d" + std::to_string(i) + ".txt"), std::ios::binary) << small;
            std::string big(1024 * 1024, 'b');
            std::ofstream(folder / "zz" / "big.bin", std::ios::binary) << big;

            FolderSnapshot snapshot;
            FolderSnapshot::Scan(folder, {}, snapshot);
            FatVolumeOptions options;
            options.freeBytes = 1024 * 1024;
            std::string error;
            auto volume = HostFolderFat::Build(snapshot, options, &error, nullptr);
            {
                std::ofstream image(dir / "files.img", std::ios::binary);
                uint8_t sector[512];
                for (uint64_t lba = 0; lba < volume->SectorCount(); lba++)
                {
                    volume->ReadSector(lba, sector);
                    image.write(reinterpret_cast<const char*>(sector), sizeof sector);
                }
            }
            std::ofstream(dir / "bench.ucompose.yaml")
                << "version: 1\ntarget: {build: rebuild, fs: fat16, free: 16MiB}\nlayers: [{source: {image: files.img}}]\n";
            return dir / "bench.ucompose.yaml";
        }();
        return file;
    }

    std::unique_ptr<FatSynthVolume> Volume()
    {
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        if (!CompositeMediumFactory::Build(ComposeDescriptor::Load(Descriptor()), {}, volume, info).Ok())
            return nullptr;
        // The descriptor asks for a rebuild: the layout under test is a FatSynthVolume
        return std::unique_ptr<FatSynthVolume>(dynamic_cast<FatSynthVolume*>(volume.release()));
    }

    /// The last MiB of the used area: big.bin, read sector after sector
    void ComposeImageSeqRead(benchmark::State& state)
    {
        auto volume = Volume();
        if (!volume)
        {
            state.SkipWithError("fixture");
            return;
        }
        const uint64_t end = volume->UsedSectorEnd();
        const uint64_t begin = end - 2048;
        uint8_t sector[512];
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

    /// The reference: the image itself read the same way, without a composite over it
    /// (what inserting files.img directly costs per sector)
    void ComposeImageDirectSeqRead(benchmark::State& state)
    {
        const std::string path = (Descriptor().parent_path() / "files.img").string();
        auto image = HddImageFormats::OpenBlock(path, "raw", RawImage::Access::ReadOnly);
        auto volume = Volume();
        if (!image || !volume)
        {
            state.SkipWithError("fixture");
            return;
        }
        const uint64_t end = volume->UsedSectorEnd();
        const uint64_t begin = end - 2048;
        uint8_t sector[512];
        uint64_t lba = begin;
        for (auto _ : state)
        {
            image->ReadSector(lba, sector);
            benchmark::DoNotOptimize(sector[0]);
            if (++lba == end)
                lba = begin;
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 512);
    }

    /// Any sector of the used area, at random (fixed seed)
    void ComposeImageRandRead(benchmark::State& state)
    {
        auto volume = Volume();
        if (!volume)
        {
            state.SkipWithError("fixture");
            return;
        }
        std::mt19937_64 random(12345);
        std::vector<uint64_t> lbas(4096);
        std::uniform_int_distribution<uint64_t> pick(volume->VolumeStart(), volume->UsedSectorEnd() - 1);
        for (uint64_t& lba : lbas)
            lba = pick(random);
        uint8_t sector[512];
        size_t i = 0;
        for (auto _ : state)
        {
            volume->ReadSector(lbas[i], sector);
            benchmark::DoNotOptimize(sector[0]);
            i = (i + 1) & 4095;
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 512);
    }

    /// Open the image, walk its tree, lay out the volume
    void ComposeImageBuild(benchmark::State& state)
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

BENCHMARK(ComposeImageSeqRead);
BENCHMARK(ComposeImageDirectSeqRead);
BENCHMARK(ComposeImageRandRead);
BENCHMARK(ComposeImageBuild)->Unit(benchmark::kMicrosecond);
