// Composite media, the benchmark families of the multi-source plan (test-and-benchmark-plan.md §5): the numbers behind
// charts C1-C8 and the NFR table of §5.5. tools/bench/plot-media-compose.py turns a JSON run into the charts.
//
//   ComposeScaleBuild/entries/layers C1 build time, C4 memory held (rebuild; ISO and graft rows in their families)
//   ComposeLayersRead/layers/kind    C2 a sector read against the layer count (0 seq, 1 random, 2 metadata)
//   ComposeFragmented/extents/kind   C3 a sector read against the extents of a source file (0 seq, 1 random)
//   ComposeMode/mode/kind/session    C5 every kind of medium the same files can be (NFR-P1)
//   ComposeGraftVsRebuild/base/build C6 graft against rebuild as the base grows (NFR-P6)
//   ComposeAttribute/entries/changed C7 `media changes` (NFR-P9);  ComposeFlatten/format: C7 S1 (NFR-P8)
//   ComposeSessionRead/changed/hit   C8 the change layer's cost
//
// Fixtures (folders, images, descriptors) are generated once per process under the system temp folder,
// unreal-ng-compose-bm; big files are hard links of one file, so the layer copies cost directory entries only.

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "../../../tests/_helpers/processmemory.h"
#include "emulator/io/storage/cd/iso9660reader.h"
#include "emulator/io/storage/compose/changeattributor.h"
#include "emulator/io/storage/compose/composedlayout.h"
#include "emulator/io/storage/compose/filetree.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/io/storage/sparsememorydisk.h"
#include "emulator/io/storage/subrangedevice.h"
#include "emulator/media/blockformats.h"
#include "emulator/media/composedescriptor.h"
#include "emulator/media/compositemediumfactory.h"

namespace fs = std::filesystem;

namespace
{
    const fs::path& Root()
    {
        static const fs::path root = [] {
            const fs::path dir = fs::temp_directory_path() / "unreal-ng-compose-bm";
            std::error_code ec;
            fs::remove_all(dir, ec);
            fs::create_directories(dir);
            return dir;
        }();
        return root;
    }

    void WriteFile(const fs::path& path, size_t bytes, char fill)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << std::string(bytes, fill);
    }

    /// `to` as a hard link of `from` (a copy where links are not possible)
    void Link(const fs::path& from, const fs::path& to)
    {
        fs::create_directories(to.parent_path());
        std::error_code ec;
        fs::create_hard_link(from, to, ec);
        if (ec)
            fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    }

    /// A folder as a FAT image file (zero sectors skipped: a sparse file where the host can)
    bool MakeImage(const fs::path& folder, const fs::path& image, FatType type, uint64_t freeBytes)
    {
        FolderSnapshot snapshot;
        if (!FolderSnapshot::Scan(folder, {}, snapshot))
            return false;
        FatVolumeOptions options;
        options.fs = type;
        options.freeBytes = freeBytes;
        options.fixedTimeUtc = 1767268800;
        std::string error;
        auto volume = HostFolderFat::Build(snapshot, options, &error, nullptr);
        if (!volume)
            return false;
        std::ofstream out(image, std::ios::binary | std::ios::trunc);
        uint8_t sector[512];
        static const uint8_t zero[512] = {};
        for (uint64_t lba = 0; lba < volume->SectorCount(); lba++)
        {
            volume->ReadSector(lba, sector);
            if (std::equal(sector, sector + 512, zero))
                out.seekp(512, std::ios::cur);
            else
                out.write(reinterpret_cast<const char*>(sector), 512);
        }
        out.seekp(-1, std::ios::cur);
        out.put('\0');
        return static_cast<bool>(out);
    }

    std::unique_ptr<IBlockDevice> Build(const fs::path& descriptor, CompositeInfo* info = nullptr, CompositeBuildOptions options = {})
    {
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo local;
        if (!CompositeMediumFactory::Build(ComposeDescriptor::Load(descriptor), options, volume, info ? *info : local).Ok())
            return nullptr;
        return volume;
    }

    fs::path Descriptor(const fs::path& file, const std::string& yaml)
    {
        std::ofstream(file) << yaml;
        return file;
    }

    /// Every sector of a file, in order (LBAs of `device`)
    std::vector<uint64_t> FileSectors(IBlockDevice& device, const std::string& path)
    {
        FatVolumeReader reader;
        FatDirEntryInfo entry;
        std::vector<FatChainExtent> extents;
        std::vector<uint64_t> lbas;
        if (!reader.Open(device) || !reader.Stat(path, entry) || !reader.ChainExtents(entry.firstCluster, entry.size, extents))
            return lbas;
        for (const FatChainExtent& x : extents)
            for (uint32_t i = 0; i < x.sectors; i++)
                lbas.push_back(x.lba + i);
        return lbas;
    }

    /// Every data sector of every file, and the metadata sectors (boot, FATs, root): what the random and metadata
    /// reads pick from
    void VolumeSectors(IBlockDevice& device, std::vector<uint64_t>& data, std::vector<uint64_t>& meta)
    {
        FatVolumeReader reader;
        if (!reader.Open(device))
            return;
        for (uint64_t lba = reader.VolumeStart(); lba < reader.VolumeStart() + reader.DataStart(); lba++)
            meta.push_back(lba);
        std::vector<std::pair<uint32_t, int>> dirs{{0, 0}};
        std::vector<FatDirEntryInfo> entries;
        std::vector<FatChainExtent> extents;
        while (!dirs.empty())
        {
            const auto [cluster, depth] = dirs.back();
            dirs.pop_back();
            if (depth > 16 || !reader.ListDirectory(cluster, entries))
                continue;
            for (const FatDirEntryInfo& e : entries)
            {
                if (e.isDirectory)
                {
                    if (e.firstCluster >= 2)
                        dirs.push_back({e.firstCluster, depth + 1});
                }
                else if (e.size && reader.ChainExtents(e.firstCluster, e.size, extents))
                {
                    for (const FatChainExtent& x : extents)
                        for (uint32_t i = 0; i < x.sectors; i++)
                            data.push_back(x.lba + i);
                }
            }
        }
    }

    std::vector<uint64_t> Shuffled(std::vector<uint64_t> lbas, size_t count)
    {
        std::mt19937_64 random(12345);
        std::vector<uint64_t> picked(count);
        if (lbas.empty())
            return {};
        std::uniform_int_distribution<size_t> pick(0, lbas.size() - 1);
        for (uint64_t& lba : picked)
            lba = lbas[pick(random)];
        return picked;
    }

    /// Read `lbas` round and round, one sector per iteration
    void ReadLoop(benchmark::State& state, IBlockDevice& device, const std::vector<uint64_t>& lbas)
    {
        if (lbas.empty())
        {
            state.SkipWithError("fixture: no sectors");
            return;
        }
        uint8_t sector[512];
        size_t i = 0;
        for (auto _ : state)
        {
            device.ReadSector(lbas[i], sector);
            benchmark::DoNotOptimize(sector[0]);
            if (++i == lbas.size())
                i = 0;
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 512);
    }

    // --- The read data set: big.bin (1 MiB), big16.bin (16 MiB), 1 000 x 4 KiB in d0..d9; spread over L folder layers

    const fs::path& Master()
    {
        static const fs::path dir = [] {
            const fs::path m = Root() / "master";
            WriteFile(m / "big.bin", 1024 * 1024, 'b');
            WriteFile(m / "big16.bin", 16 * 1024 * 1024, 'B');
            WriteFile(m / "small.bin", 4096, 's');
            return m;
        }();
        return dir;
    }

    /// The data set over `layers` folders: the big files in the first, small file i in layer i % layers
    const fs::path& LayersDescriptor(int layers)
    {
        static std::map<int, fs::path> made;
        auto it = made.find(layers);
        if (it != made.end())
            return it->second;
        const fs::path dir = Root() / ("layers" + std::to_string(layers));
        Link(Master() / "big.bin", dir / "l0" / "big.bin");
        Link(Master() / "big16.bin", dir / "l0" / "big16.bin");
        for (int i = 0; i < 1000; i++)
            Link(Master() / "small.bin", dir / ("l" + std::to_string(i % layers)) / ("d" + std::to_string(i / 100)) /
                                             ("f" + std::to_string(i) + ".bin"));
        std::string yaml = "version: 1\ntarget: {build: rebuild, fs: fat16, free: 8MiB, fixedTime: 1767268800}\nlayers:\n";
        for (int j = 0; j < layers; j++)
        {
            fs::create_directories(dir / ("l" + std::to_string(j)));
            yaml += "  - {source: {folder: l" + std::to_string(j) + "}}\n";
        }
        return made[layers] = Descriptor(dir / "disk.ucompose.yaml", yaml);
    }

    /// C2: kind 0 big16.bin in order, 1 random data sectors, 2 boot / FAT / root sectors
    void ComposeLayersRead(benchmark::State& state)
    {
        auto volume = Build(LayersDescriptor(static_cast<int>(state.range(0))));
        if (!volume)
        {
            state.SkipWithError("fixture");
            return;
        }
        std::vector<uint64_t> data, meta;
        VolumeSectors(*volume, data, meta);
        const int kind = static_cast<int>(state.range(1));
        ReadLoop(state, *volume, kind == 0 ? FileSectors(*volume, "/big16.bin") : kind == 1 ? Shuffled(data, 65536) : meta);
    }

    // --- C1 / C4: build time and memory against entries and layers (synthetic trees, 100 files per directory)

    fs::path BuildDescriptor(int entries, int layers, bool optical = false)
    {
        const fs::path dir = Root() / ("build-" + std::to_string(entries) + "-" + std::to_string(layers));
        const fs::path file = dir / (optical ? "cd.ucompose.yaml" : "disk.ucompose.yaml");
        if (fs::exists(file))
            return file;
        WriteFile(Root() / "tiny.bin", 100, 't');
        if (!fs::exists(dir))
        {
            for (int i = 0; i < entries; i++)
                Link(Root() / "tiny.bin", dir / ("l" + std::to_string(i % layers)) / ("d" + std::to_string(i / 100)) /
                                              ("f" + std::to_string(i) + ".txt"));
        }
        std::string yaml = std::string("version: 1\ntarget: {") + (optical ? "kind: optical" : "build: rebuild, fs: fat32, free: 1MiB") +
                           ", fixedTime: 1767268800}\nlayers:\n";
        for (int j = 0; j < layers; j++)
            yaml += "  - {source: {folder: l" + std::to_string(j) + "}}\n";
        return Descriptor(file, yaml);
    }

    /// Heap bytes in use (glibc), else 0
    uint64_t HeapInUse()
    {
#if defined(__linux__) && defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
        return mallinfo2().uordblks;
#else
        return 0;
#endif
    }

    /// What a built volume holds: heap in use (glibc) and resident growth, both after the build's temporaries went
    void HeldBytes(benchmark::State& state, const fs::path& descriptor, CompositeBuildOptions options = {})
    {
        ProcessMemory::Settle();
        const uint64_t heap = HeapInUse();
        ProcessMemory::Meter meter;
        auto volume = Build(descriptor, nullptr, options);
        ProcessMemory::Settle();
        const uint64_t heapNow = HeapInUse();
        state.counters["heldBytes"] = static_cast<double>(heapNow > heap ? heapNow - heap : meter.Growth());
        state.counters["residentBytes"] = static_cast<double>(meter.Growth());
        benchmark::DoNotOptimize(volume.get());
    }

    void ComposeScaleBuild(benchmark::State& state)
    {
        const int entries = static_cast<int>(state.range(0));
        const int layers = static_cast<int>(state.range(1));
        const fs::path file = BuildDescriptor(entries, layers);
        const ComposeDescriptor descriptor = ComposeDescriptor::Load(file);
        for (auto _ : state)
        {
            std::unique_ptr<IBlockDevice> volume;
            CompositeInfo info;
            benchmark::DoNotOptimize(CompositeMediumFactory::Build(descriptor, {}, volume, info).Ok());
        }
        HeldBytes(state, file);
        state.counters["entries"] = entries;
    }

    /// C4: the same trees as an ISO 9660 CD (one layer)
    void ComposeScaleBuildIso(benchmark::State& state)
    {
        const int entries = static_cast<int>(state.range(0));
        const fs::path file = BuildDescriptor(entries, 1, /*optical*/ true);
        const ComposeDescriptor descriptor = ComposeDescriptor::Load(file);
        for (auto _ : state)
        {
            std::unique_ptr<IBlockDevice> volume;
            CompositeInfo info;
            benchmark::DoNotOptimize(CompositeMediumFactory::Build(descriptor, {}, volume, info).Ok());
        }
        HeldBytes(state, file);
        state.counters["entries"] = entries;
    }

    // --- C3: one 4 MiB file whose source is split into k extents (in reverse order on a memory disk)

    void ComposeFragmented(benchmark::State& state)
    {
        const uint32_t extents = static_cast<uint32_t>(state.range(0));
        constexpr uint32_t kSectors = 8192;
        auto pool = std::make_shared<SourcePool>();
        const uint16_t device = pool->AddDevice(std::make_shared<MemoryDisk>(kSectors));
        auto tree = std::make_shared<FileTree>();
        TreeNode node;
        node.name = "FRAG.BIN";
        node.data.storage = FileData::Storage::DeviceExtents;
        node.data.source = device;
        node.data.bytes = kSectors * 512ull;
        node.data.firstExtent = 0;
        node.data.extentCount = extents;
        const uint32_t per = kSectors / extents;
        for (uint32_t e = 0; e < extents; e++)
            tree->Extents().push_back(Extent{static_cast<uint64_t>(extents - 1 - e) * per, per, e * per});
        tree->Add(FileTree::kRoot, node);
        FatVolumeOptions options;
        options.freeBytes = 1024 * 1024;
        std::string error;
        auto volume = FatSynthVolume::Build(tree, pool, options, 1, "fragmented", &error, nullptr);
        if (!volume)
        {
            state.SkipWithError(error.c_str());
            return;
        }
        const std::vector<uint64_t> sectors = FileSectors(*volume, "/FRAG.BIN");
        ReadLoop(state, *volume, state.range(1) == 0 ? sectors : Shuffled(sectors, 65536));
    }

    // --- C5: modes. The data set as a FAT16 image too, and the media made from it

    enum Mode : int
    {
        kRaw,     ///< the FAT16 image file itself
        kHff,     ///< HostFolderFat over the folder
        kC1f,     ///< composite rebuild, one folder layer
        kC8f,     ///< composite rebuild, the files over 8 folder layers
        kCfat16,  ///< composite rebuild over the FAT16 image
        kGraft,   ///< the image as a graft base, big16.bin grafted from a folder
        kPart2,   ///< partitioned: the image's partition passthrough, a FAT32 composite of the folder
        kIsot,    ///< ISO 9660 target over the folder (512-byte sector reads)
    };

    const char* const kModeNames[] = {"raw", "hff", "c1f", "c8f", "cfat16", "graft", "part2", "isot"};

    const fs::path& ModeFolder()
    {
        static const fs::path dir = [] {
            const fs::path d = Root() / "modes";
            LayersDescriptor(1);  // the one-layer copy is the folder
            fs::create_directories(d / "up");
            Link(Master() / "big16.bin", d / "up" / "big16g.bin");
            MakeImage(Root() / "layers1" / "l0", d / "files16.img", FatType::Fat16, 32ull * 1024 * 1024);
            Descriptor(d / "cfat16.ucompose.yaml",
                       "version: 1\ntarget: {build: rebuild, fs: fat16, free: 8MiB, fixedTime: 1767268800}\n"
                       "layers: [{source: {image: files16.img}}]\n");
            Descriptor(d / "graft.ucompose.yaml", "version: 1\ntarget: {build: graft, fixedTime: 1767268800}\n"
                                                  "layers: [{source: {image: files16.img}}, {source: {folder: up}}]\n");
            Descriptor(d / "part2.ucompose.yaml",
                       "version: 1\ntarget: {fixedTime: 1767268800}\npartitions:\n"
                       "  - {name: dos, source: {image: files16.img, partition: 1}}\n"
                       "  - {name: data, fs: fat32, compose: {build: rebuild, free: 1MiB, layers: [{source: {folder: ../layers1/l0}}]}}\n");
            Descriptor(d / "isot.ucompose.yaml", "version: 1\ntarget: {kind: optical, fixedTime: 1767268800}\n"
                                                 "layers: [{source: {folder: ../layers1/l0}}]\n");
            return d;
        }();
        return dir;
    }

    /// The medium of a mode, the sectors of its 16 MiB file, its random data sectors and its metadata sectors
    std::unique_ptr<IBlockDevice> ModeMedium(int mode, std::vector<uint64_t>& seq, std::vector<uint64_t>& data, std::vector<uint64_t>& meta)
    {
        const fs::path& d = ModeFolder();
        std::unique_ptr<IBlockDevice> volume;
        std::string big = "/big16.bin";
        switch (mode)
        {
            case kRaw:
                volume = HddImageFormats::OpenBlock((d / "files16.img").string(), "raw", RawImage::Access::ReadOnly);
                break;
            case kHff:
            {
                FolderSnapshot snapshot;
                FolderSnapshot::Scan(Root() / "layers1" / "l0", {}, snapshot);
                FatVolumeOptions options;
                options.freeBytes = 8 * 1024 * 1024;
                std::string error;
                volume = HostFolderFat::Build(snapshot, options, &error, nullptr);
                break;
            }
            case kC1f: volume = Build(LayersDescriptor(1)); break;
            case kC8f: volume = Build(LayersDescriptor(8)); break;
            case kCfat16: volume = Build(d / "cfat16.ucompose.yaml"); break;
            case kGraft:
                volume = Build(d / "graft.ucompose.yaml");
                big = "/big16g.bin";
                break;
            case kPart2:
            {
                volume = Build(d / "part2.ucompose.yaml");
                if (!volume)
                    return nullptr;
                // The composed partition: its own sectors, then moved to the disk's LBAs
                std::shared_ptr<IBlockDevice> disk(volume.get(), [](IBlockDevice*) {});
                FatPartition p;
                if (!FatVolumeReader::FindPartition(*disk, 2, p))
                    return nullptr;
                SubRangeDevice window(disk, p.first, p.count);
                seq = FileSectors(window, big);
                VolumeSectors(window, data, meta);
                for (auto* list : {&seq, &data, &meta})
                    for (uint64_t& lba : *list)
                        lba += p.first;
                data = Shuffled(data, 65536);
                return volume;
            }
            case kIsot:
            {
                volume = Build(d / "isot.ucompose.yaml");
                Iso9660Reader reader;
                IsoDirEntry entry;
                if (!volume || !reader.Open(*volume) || !reader.Stat(big, entry))
                    return nullptr;
                for (uint64_t lba = entry.Block() * 4ull; lba < entry.Block() * 4ull + entry.size / 512; lba++)
                    seq.push_back(lba);
                for (uint64_t lba = 64; lba < 128; lba++)  // block 16 on: the volume descriptors and path tables
                    meta.push_back(lba);
                data = Shuffled(seq, 65536);
                return volume;
            }
            default: return nullptr;
        }
        if (!volume)
            return nullptr;
        seq = FileSectors(*volume, big);
        VolumeSectors(*volume, data, meta);
        data = Shuffled(data, 65536);
        return volume;
    }

    /// C5: range(0) the mode, range(1) 0 seq / 1 random / 2 metadata, range(2) changed sectors in a session over it
    void ComposeMode(benchmark::State& state)
    {
        std::vector<uint64_t> seq, data, meta;
        std::unique_ptr<IBlockDevice> volume = ModeMedium(static_cast<int>(state.range(0)), seq, data, meta);
        if (!volume)
        {
            state.SkipWithError("fixture");
            return;
        }
        state.SetLabel(kModeNames[state.range(0)]);
        const int kind = static_cast<int>(state.range(1));
        const std::vector<uint64_t>& lbas = kind == 0 ? seq : kind == 1 ? data : meta;
        const uint64_t changed = static_cast<uint64_t>(state.range(2));
        if (!changed)
        {
            ReadLoop(state, *volume, lbas);
            return;
        }
        // The guest wrote `changed` sectors elsewhere on the disk: every read here also asks the change layer
        const uint64_t total = volume->SectorCount();
        SessionWriteMap session(std::move(volume));
        std::mt19937_64 random(777);
        std::uniform_int_distribution<uint64_t> pick(total / 2, total - 1);
        uint8_t sector[512] = {1};
        for (uint64_t i = 0; i < changed; i++)
            session.WriteSector(pick(random), sector);
        ReadLoop(state, session, lbas);
    }

    // --- C6: graft against rebuild as the base grows (100 upper files)

    const fs::path& GraftDescriptor(int baseFiles, bool graft)
    {
        static std::map<std::pair<int, bool>, fs::path> made;
        auto it = made.find({baseFiles, graft});
        if (it != made.end())
            return it->second;
        const fs::path dir = Root() / ("graft-" + std::to_string(baseFiles));
        if (!fs::exists(dir / "base.img"))
        {
            WriteFile(Root() / "tiny.bin", 100, 't');
            for (int i = 0; i < baseFiles; i++)
                Link(Root() / "tiny.bin", dir / "base" / ("d" + std::to_string(i / 100)) / ("f" + std::to_string(i) + ".txt"));
            for (int i = 0; i < 100; i++)
                Link(Root() / "tiny.bin", dir / "up" / "NEW" / ("n" + std::to_string(i) + ".txt"));
            MakeImage(dir / "base", dir / "base.img", FatType::Fat32, 8ull * 1024 * 1024);
        }
        const std::string build = graft ? "graft" : "rebuild";
        return made[{baseFiles, graft}] =
                   Descriptor(dir / (build + ".ucompose.yaml"),
                              "version: 1\ntarget: {build: " + build + ", fs: fat32, free: 8MiB, fixedTime: 1767268800}\n"
                              "layers: [{source: {image: base.img}}, {source: {folder: up}}]\n");
    }

    void ComposeGraftVsRebuild(benchmark::State& state)
    {
        const int baseFiles = static_cast<int>(state.range(0));
        const bool graft = state.range(1) == 0;
        const fs::path& file = GraftDescriptor(baseFiles, graft);
        const ComposeDescriptor descriptor = ComposeDescriptor::Load(file);
        for (auto _ : state)
        {
            std::unique_ptr<IBlockDevice> volume;
            CompositeInfo info;
            benchmark::DoNotOptimize(CompositeMediumFactory::Build(descriptor, {}, volume, info).Ok());
        }
        state.SetLabel(graft ? "graft" : "rebuild");
        HeldBytes(state, file);
        state.counters["entries"] = baseFiles;
    }

    // --- C7: attribution of the guest's writes, and S1 flatten

    /// range(0) entries of the volume (a one-layer rebuild), range(1) data sectors the guest rewrote
    void ComposeAttribute(benchmark::State& state)
    {
        auto volume = Build(BuildDescriptor(static_cast<int>(state.range(0)), 1));
        if (!volume)
        {
            state.SkipWithError("fixture");
            return;
        }
        std::vector<uint64_t> data, meta;
        VolumeSectors(*volume, data, meta);
        SessionWriteMap session(std::move(volume));
        const std::vector<uint64_t> changed = Shuffled(data, static_cast<size_t>(state.range(1)));
        uint8_t sector[512];
        for (uint64_t lba : changed)
        {
            session.ReadSector(lba, sector);
            sector[0] ^= 0xFF;
            session.WriteSector(lba, sector);
        }
        const auto* layout = dynamic_cast<const IComposedLayout*>(&session.Base());
        size_t files = 0;
        for (auto _ : state)
        {
            ChangeSet out;
            ChangeAttributor::Attribute(session.Base(), session, session, layout, out);
            files = out.changes.size();
        }
        state.counters["changedFiles"] = static_cast<double>(files);
    }

    /// S1 of the data set's one-layer composite (and, as the reference, of its own image written as a raw image)
    void ComposeFlatten(benchmark::State& state)
    {
        static const char* const kFormats[] = {"img", "vhd", "chd", "compact", "raw-img"};
        const int format = static_cast<int>(state.range(0));
        std::unique_ptr<IBlockDevice> source;
        if (format == 4)
        {
            // The reference: the same composite exported once, then that raw image written again
            const fs::path once = Root() / "flatten-source.img";
            if (!fs::exists(once))
            {
                auto composite = Build(LayersDescriptor(1));
                if (!composite || !BlockFormats::Write(*composite, once.string(), {}).Ok())
                {
                    state.SkipWithError("fixture");
                    return;
                }
            }
            source = HddImageFormats::OpenBlock(once.string(), "raw", RawImage::Access::ReadOnly);
        }
        else
            source = Build(LayersDescriptor(1));
        if (!source)
        {
            state.SkipWithError("fixture");
            return;
        }
        BlockWriteOptions options;
        std::string extension = ".img";
        if (format == 1)
            extension = ".vhd";
        if (format == 2)
        {
            extension = ".chd";
            options.compression = "zlib";
        }
        options.compact = format == 3;
        const std::string out = (Root() / (std::string("flatten-out") + extension)).string();
        for (auto _ : state)
        {
            const MediaResult r = BlockFormats::Write(*source, out, options);
            if (!r.Ok())
            {
                state.SkipWithError(r.message.c_str());
                return;
            }
        }
        state.SetLabel(kFormats[format]);
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(source->SectorCount()) * 512);
    }

    // --- C8: the change layer: a read of a changed sector (hit) or an unchanged one (miss) with N sectors changed

    void ComposeSessionRead(benchmark::State& state)
    {
        const uint64_t changed = static_cast<uint64_t>(state.range(0));
        constexpr uint64_t kSectors = 4ull * 1024 * 1024;  // a 2 GiB card
        SessionWriteMap session(std::make_unique<SparseMemoryDisk>(kSectors));
        session.SetMemoryLimit(0);  // all in memory: the lookup is measured, not the journal
        std::mt19937_64 random(99);
        std::uniform_int_distribution<uint64_t> pick(0, kSectors / 2 - 1);
        std::vector<uint64_t> hits;
        uint8_t sector[512] = {7};
        for (uint64_t i = 0; i < changed; i++)
        {
            const uint64_t lba = pick(random) * 2;  // even sectors change, odd ones never do
            session.WriteSector(lba, sector);
            hits.push_back(lba);
        }
        std::vector<uint64_t> lbas;
        if (state.range(1) == 1)
            lbas = Shuffled(hits, 65536);
        else
        {
            for (uint64_t i = 0; i < 65536; i++)
                lbas.push_back(pick(random) * 2 + 1);
        }
        state.SetLabel(state.range(1) == 1 ? "hit" : "miss");
        ReadLoop(state, session, lbas);
    }
}  // namespace

BENCHMARK(ComposeScaleBuild)
    ->ArgsProduct({{1000, 10000, 100000}, {1, 4, 16, 64}})
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);
BENCHMARK(ComposeScaleBuildIso)->Arg(1000)->Arg(10000)->Arg(100000)->Unit(benchmark::kMillisecond)->Iterations(3);
BENCHMARK(ComposeLayersRead)->ArgsProduct({{1, 2, 4, 8, 16, 32, 64}, {0, 1, 2}});
BENCHMARK(ComposeFragmented)->ArgsProduct({{1, 4, 16, 64, 256, 1024, 4096}, {0, 1}});
BENCHMARK(ComposeMode)->ArgsProduct({{kRaw, kHff, kC1f, kC8f, kCfat16, kGraft, kPart2, kIsot}, {0, 1, 2}, {0, 10000}});
BENCHMARK(ComposeGraftVsRebuild)
    ->ArgsProduct({{1000, 10000, 100000}, {0, 1}})
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);
BENCHMARK(ComposeAttribute)
    ->ArgsProduct({{10000, 100000}, {10, 100, 1000, 10000, 100000}})
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);
BENCHMARK(ComposeFlatten)->DenseRange(0, 4)->Unit(benchmark::kMillisecond)->Iterations(3);
BENCHMARK(ComposeSessionRead)->Args({0, 0})->ArgsProduct({{1000, 10000, 100000}, {0, 1}});
