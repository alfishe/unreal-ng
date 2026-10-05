// Host folder volumes (HostFolderFat, multi-source tdd.md §5, NFR-P1): what a guest
// sector read costs on a folder presented as FAT16 / FAT32, for data sectors read
// sequentially (a DOS loading a file), at random, and for metadata (boot + FAT),
// plus the build time. The A/B baseline of the multi-source refactor (phase C0).
// The fixture folder is generated once per process in the system temp folder.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"

namespace
{
    /// 200 small files in the root, a deep folder, and a 1 MiB file laid out last
    const std::filesystem::path& Fixture()
    {
        static const std::filesystem::path dir = [] {
            const auto path = std::filesystem::temp_directory_path() / "unreal-ng-hff-bench";
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            std::filesystem::create_directories(path / "deep" / "er");
            std::filesystem::create_directories(path / "zz");
            std::string small(4096, 's');
            for (int i = 0; i < 200; i++)
                std::ofstream(path / ("file" + std::to_string(i) + ".bin"), std::ios::binary) << small;
            for (int i = 0; i < 20; i++)
                std::ofstream(path / "deep" / "er" / ("d" + std::to_string(i) + ".txt"), std::ios::binary) << small;
            std::string big(1024 * 1024, 'b');
            std::ofstream(path / "zz" / "big.bin", std::ios::binary) << big;
            return path;
        }();
        return dir;
    }

    std::unique_ptr<HostFolderFat> Volume(FatType fs)
    {
        FolderSnapshot snapshot;
        if (!FolderSnapshot::Scan(Fixture(), {}, snapshot))
            return nullptr;
        FatVolumeOptions options;
        options.fs = fs;
        options.freeBytes = 16ull * 1024 * 1024;
        std::string error;
        return HostFolderFat::Build(snapshot, options, &error, nullptr);
    }

    /// The last MiB of the used area: big.bin, read sector after sector
    void HostFolderFatSeqRead(benchmark::State& state, FatType fs)
    {
        auto volume = Volume(fs);
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

    /// Any sector of the used area, at random (fixed seed)
    void HostFolderFatRandRead(benchmark::State& state, FatType fs)
    {
        auto volume = Volume(fs);
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

    /// The boot record and the first FAT sectors (synthesized on every read)
    void HostFolderFatMetaRead(benchmark::State& state, FatType fs)
    {
        auto volume = Volume(fs);
        if (!volume)
        {
            state.SkipWithError("fixture");
            return;
        }
        const uint64_t begin = volume->VolumeStart();
        uint8_t sector[512];
        uint64_t lba = begin;
        for (auto _ : state)
        {
            volume->ReadSector(lba, sector);
            benchmark::DoNotOptimize(sector[0]);
            if (++lba == begin + 64)
                lba = begin;
        }
    }

    void HostFolderFatBuild(benchmark::State& state, FatType fs)
    {
        FolderSnapshot snapshot;
        if (!FolderSnapshot::Scan(Fixture(), {}, snapshot))
        {
            state.SkipWithError("fixture");
            return;
        }
        FatVolumeOptions options;
        options.fs = fs;
        for (auto _ : state)
        {
            std::string error;
            auto volume = HostFolderFat::Build(snapshot, options, &error, nullptr);
            benchmark::DoNotOptimize(volume.get());
        }
    }
}  // namespace

BENCHMARK_CAPTURE(HostFolderFatSeqRead, fat16, FatType::Fat16);
BENCHMARK_CAPTURE(HostFolderFatSeqRead, fat32, FatType::Fat32);
BENCHMARK_CAPTURE(HostFolderFatRandRead, fat16, FatType::Fat16);
BENCHMARK_CAPTURE(HostFolderFatRandRead, fat32, FatType::Fat32);
BENCHMARK_CAPTURE(HostFolderFatMetaRead, fat16, FatType::Fat16);
BENCHMARK_CAPTURE(HostFolderFatMetaRead, fat32, FatType::Fat32);
BENCHMARK_CAPTURE(HostFolderFatBuild, fat16, FatType::Fat16)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(HostFolderFatBuild, fat32, FatType::Fat32)->Unit(benchmark::kMicrosecond);
