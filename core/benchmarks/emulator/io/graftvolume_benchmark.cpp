// Graft (multi-source phases/c4-graft.md §6, NFR-P6 / NFR-P1): building a composite on a FAT image base
// with one host file grafted, against rebuilding the same union; and reading the grafted file sector
// after sector against reading a base file. The base holds 2 000 small files in 20 directories, or
// 20 000 in 200 (C4b: a graft reads only the base directories the upper layers touch); the fixture
// (folders, image, descriptors) is generated once per process and size in the system temp folder.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "emulator/io/storage/compose/graftvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"
#include "emulator/media/composedescriptor.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    std::filesystem::path MakeFixture(int directories)
    {
        const auto root = std::filesystem::temp_directory_path() / ("unreal-ng-graft-bench-" + std::to_string(directories));
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        const std::string small(1024, 's');
        for (int d = 0; d < directories; d++)
        {
            const auto sub = root / "base" / ("D" + std::to_string(d));
            std::filesystem::create_directories(sub);
            for (int f = 0; f < 100; f++)
                std::ofstream(sub / ("f" + std::to_string(f) + ".bin"), std::ios::binary) << small;
        }
        std::ofstream(root / "base" / "base.bin", std::ios::binary) << std::string(1024 * 1024, 'b');
        std::filesystem::create_directories(root / "up");
        std::ofstream(root / "up" / "grafted.bin", std::ios::binary) << std::string(1024 * 1024, 'g');

        FolderSnapshot snapshot;
        FolderSnapshot::Scan(root / "base", {}, snapshot);
        FatVolumeOptions options;
        options.freeBytes = 8ull * 1024 * 1024;
        std::string error;
        auto volume = HostFolderFat::Build(snapshot, options, &error, nullptr);
        {
            std::ofstream image(root / "base.img", std::ios::binary);
            uint8_t sector[512];
            for (uint64_t lba = 0; lba < volume->SectorCount(); lba++)
            {
                volume->ReadSector(lba, sector);
                image.write(reinterpret_cast<const char*>(sector), sizeof sector);
            }
        }
        for (const char* build : {"graft", "rebuild"})
            std::ofstream(root / (std::string(build) + ".ucompose.yaml"))
                << "version: 1\ntarget: {build: " << build << ", fs: fat16, free: 8MiB}\n"
                << "layers:\n  - {source: {image: base.img}}\n  - {source: {folder: up}}\n";
        return root;
    }

    /// The base of 2 000 files (20 directories), or of 20 000 (200 directories)
    const std::filesystem::path& Fixture(bool large = false)
    {
        static const std::filesystem::path small = MakeFixture(20);
        if (!large)
            return small;
        static const std::filesystem::path big = MakeFixture(200);
        return big;
    }

    std::unique_ptr<IBlockDevice> Build(const char* build)
    {
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        CompositeMediumFactory::Build(ComposeDescriptor::Load(Fixture() / (std::string(build) + ".ucompose.yaml")), {}, volume, info);
        return volume;
    }

    /// state.range(0): the base's files (2 000 or 20 000). `graft-full` reads the whole base first (before C4b),
    /// `graft-counted` also counts the unread base directories (the first `layers` reply)
    void ComposeBuild(benchmark::State& state, const char* build)
    {
        const bool large = state.range(0) > 2000;
        const std::string variant = build;
        const std::string file = variant == "rebuild" ? "rebuild" : "graft";
        const ComposeDescriptor descriptor = ComposeDescriptor::Load(Fixture(large) / (file + ".ucompose.yaml"));
        CompositeBuildOptions options;
        options.lazyBase = variant != "graft-full";
        for (auto _ : state)
        {
            std::unique_ptr<IBlockDevice> volume;
            CompositeInfo info;
            benchmark::DoNotOptimize(CompositeMediumFactory::Build(descriptor, options, volume, info).Ok());
            if (variant == "graft-counted")
                info.CompleteCounts();
        }
    }

    /// The sectors of one file of the graft volume, in order
    void GraftSeqRead(benchmark::State& state, const char* file)
    {
        auto volume = Build("graft");
        FatVolumeReader reader;
        FatDirEntryInfo entry;
        std::vector<FatChainExtent> extents;
        if (!volume || !reader.Open(*volume) || !reader.Stat(file, entry) || !reader.ChainExtents(entry.firstCluster, entry.size, extents) ||
            extents.size() != 1)
        {
            state.SkipWithError("fixture");
            return;
        }
        const uint64_t begin = extents[0].lba;
        const uint64_t end = begin + extents[0].sectors;
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
}  // namespace

BENCHMARK_CAPTURE(ComposeBuild, graft, "graft")->Arg(2000)->Arg(20000)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(ComposeBuild, graftCounted, "graft-counted")->Arg(2000)->Arg(20000)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(ComposeBuild, graftFull, "graft-full")->Arg(2000)->Arg(20000)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(ComposeBuild, rebuild, "rebuild")->Arg(2000)->Arg(20000)->Unit(benchmark::kMicrosecond);
BENCHMARK_CAPTURE(GraftSeqRead, grafted, "/grafted.bin");
BENCHMARK_CAPTURE(GraftSeqRead, base, "/base.bin");
