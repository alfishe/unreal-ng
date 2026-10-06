// Sparse and in-memory media (multi-source phases/c10-sparse-memory.md §5, §6):
// - RawImage reads (sequential, random) against the same reads from a memory buffer: whether images should be read
//   into memory at insert (§5);
// - the export of a big, mostly empty composite with and without known-zero runs (C10a);
// - a blank card: SparseMemoryDisk against MemoryDisk.
// Fixtures are generated once per process in the system temp folder.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/io/storage/sparsememorydisk.h"
#include "emulator/media/blockformats.h"
#include "emulator/media/composedescriptor.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    constexpr uint64_t kImageSectors = 64ull * 1024 * 2;  // 64 MiB

    const std::filesystem::path& Folder()
    {
        static const std::filesystem::path dir = [] {
            const auto d = std::filesystem::temp_directory_path() / "unreal-ng-sparse-bench";
            std::error_code ec;
            std::filesystem::remove_all(d, ec);
            std::filesystem::create_directories(d / "files");
            // A 64 MiB image of non-zero data (no holes: every read reaches the file)
            std::ofstream image(d / "data.img", std::ios::binary);
            std::vector<uint8_t> chunk(1024 * 1024);
            std::mt19937 random(7);
            for (int m = 0; m < 64; m++)
            {
                for (uint8_t& b : chunk)
                    b = static_cast<uint8_t>(random() | 1);
                image.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
            }
            std::ofstream(d / "files" / "A.BIN", std::ios::binary) << std::string(1024 * 1024, 'a');
            std::ofstream(d / "big.ucompose.yaml") << "version: 1\ntarget: {fs: fat32, size: 4GiB, build: rebuild}\n"
                                                      "layers: [{source: {folder: files}}]\n";
            return d;
        }();
        return dir;
    }

    std::vector<uint64_t> RandomLbas()
    {
        std::mt19937_64 random(12345);
        std::vector<uint64_t> lbas(4096);
        for (uint64_t& lba : lbas)
            lba = random() % kImageSectors;
        return lbas;
    }

    void RawImageSeqRead(benchmark::State& state)
    {
        auto image = RawImage::Open((Folder() / "data.img").string(), RawImage::Access::ReadOnly);
        if (!image)
        {
            state.SkipWithError("fixture");
            return;
        }
        uint8_t sector[512];
        uint64_t lba = 0;
        for (auto _ : state)
        {
            image->ReadSector(lba, sector);
            benchmark::DoNotOptimize(sector[0]);
            lba = (lba + 1) % kImageSectors;
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 512);
    }

    void RawImageRandRead(benchmark::State& state)
    {
        auto image = RawImage::Open((Folder() / "data.img").string(), RawImage::Access::ReadOnly);
        if (!image)
        {
            state.SkipWithError("fixture");
            return;
        }
        const std::vector<uint64_t> lbas = RandomLbas();
        uint8_t sector[512];
        size_t i = 0;
        for (auto _ : state)
        {
            image->ReadSector(lbas[i], sector);
            benchmark::DoNotOptimize(sector[0]);
            i = (i + 1) & 4095;
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 512);
    }

    /// The reference: the same file read into memory once, sectors copied out of it
    void MemoryImageRandRead(benchmark::State& state)
    {
        std::vector<uint8_t> data(kImageSectors * 512);
        std::ifstream((Folder() / "data.img"), std::ios::binary).read(reinterpret_cast<char*>(data.data()),
                                                                       static_cast<std::streamsize>(data.size()));
        const std::vector<uint64_t> lbas = RandomLbas();
        uint8_t sector[512];
        size_t i = 0;
        for (auto _ : state)
        {
            std::memcpy(sector, data.data() + lbas[i] * 512, 512);
            benchmark::DoNotOptimize(sector[0]);
            i = (i + 1) & 4095;
        }
        state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 512);
    }

    /// Hides the inner device's known-zero runs: the export as it was before C10a
    class NoZeroRuns : public IBlockDevice
    {
    public:
        explicit NoZeroRuns(IBlockDevice& inner) : _inner(inner) {}
        uint64_t SectorCount() const override { return _inner.SectorCount(); }
        bool ReadSector(uint64_t lba, uint8_t* dst) override { return _inner.ReadSector(lba, dst); }
        bool WriteSector(uint64_t, const uint8_t*) override { return false; }
        bool IsWritable() const override { return false; }
        std::string Describe() const override { return _inner.Describe(); }
        uint64_t ContentId() const override { return _inner.ContentId(); }

    private:
        IBlockDevice& _inner;
    };

    /// A 4 GiB FAT32 composite holding 1 MiB, exported to a raw image (arg 1) or a dynamic VHD (arg 2); arg 0 of the
    /// second range: 1 = known-zero runs used, 0 = hidden
    void BigCompositeExport(benchmark::State& state)
    {
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        if (!CompositeMediumFactory::Build(ComposeDescriptor::Load(Folder() / "big.ucompose.yaml"), {}, volume, info).Ok())
        {
            state.SkipWithError("fixture");
            return;
        }
        NoZeroRuns hidden(*volume);
        IBlockDevice& device = state.range(1) ? *volume : static_cast<IBlockDevice&>(hidden);
        BlockWriteOptions options;
        std::string target = (Folder() / "out.img").string();
        if (state.range(0) == 2)
        {
            options.vhd = "dynamic";
            target = (Folder() / "out.vhd").string();
        }
        for (auto _ : state)
            benchmark::DoNotOptimize(BlockFormats::Write(device, target, options).Ok());
        std::error_code ec;
        std::filesystem::remove(target, ec);
    }

    /// A blank 512 MiB card: created, formatted-like (the first 1 MiB written), dropped
    void BlankCard(benchmark::State& state)
    {
        const uint64_t sectors = 512ull * 1024 * 2;
        const std::vector<uint8_t> data(512, 0xE5);
        size_t held = 0;
        for (auto _ : state)
        {
            std::unique_ptr<IBlockDevice> card;
            if (state.range(0))
                card = std::make_unique<SparseMemoryDisk>(sectors);
            else
                card = std::make_unique<MemoryDisk>(sectors);
            for (uint64_t lba = 0; lba < 2048; lba++)
                card->WriteSector(lba, data.data());
            if (auto* sparse = dynamic_cast<SparseMemoryDisk*>(card.get()))
                held = sparse->StoredBytes();
            else
                held = sectors * 512;
            benchmark::DoNotOptimize(card.get());
        }
        state.counters["heldMiB"] = static_cast<double>(held) / (1024 * 1024);
    }

    /// A 256 MiB card written full (every sector non-zero): arg 1 SparseMemoryDisk, 0 MemoryDisk, 2 a session over an
    /// empty SparseMemoryDisk (what a blank card from `media create` is: the guest's writes land in the session)
    constexpr uint64_t kFullSectors = 256ull * 1024 * 2;

    std::unique_ptr<IBlockDevice> FullCard(int64_t kind)
    {
        std::unique_ptr<IBlockDevice> card;
        if (kind == 0)
            card = std::make_unique<MemoryDisk>(kFullSectors);
        else if (kind == 1)
            card = std::make_unique<SparseMemoryDisk>(kFullSectors);
        else
            card = std::make_unique<SessionWriteMap>(std::make_unique<SparseMemoryDisk>(kFullSectors));
        std::vector<uint8_t> data(512, 0x5A);
        for (uint64_t lba = 0; lba < kFullSectors; lba++)
        {
            data[0] = static_cast<uint8_t>(lba | 1);
            card->WriteSector(lba, data.data());
        }
        return card;
    }

    void FullCardRandRead(benchmark::State& state)
    {
        auto card = FullCard(state.range(0));
        std::mt19937_64 random(99);
        std::vector<uint64_t> lbas(4096);
        for (uint64_t& lba : lbas)
            lba = random() % kFullSectors;
        uint8_t sector[512];
        size_t i = 0;
        for (auto _ : state)
        {
            card->ReadSector(lbas[i], sector);
            benchmark::DoNotOptimize(sector[0]);
            i = (i + 1) & 4095;
        }
    }

    /// Rewrites of a full card: non-zero data (arg 1 of the second range) or zeros (0: the chunk is scanned for
    /// freeing)
    void FullCardRewrite(benchmark::State& state)
    {
        auto card = FullCard(state.range(0));
        std::vector<uint8_t> data(512, state.range(1) ? 0x33 : 0);
        std::mt19937_64 random(98);
        std::vector<uint64_t> lbas(4096);
        for (uint64_t& lba : lbas)
            lba = random() % kFullSectors;
        size_t i = 0;
        for (auto _ : state)
        {
            card->WriteSector(lbas[i], data.data());
            i = (i + 1) & 4095;
        }
    }

    /// Filling the card: the time to write all of it (first writes allocate)
    void FullCardFill(benchmark::State& state)
    {
        for (auto _ : state)
            benchmark::DoNotOptimize(FullCard(state.range(0)).get());
    }
}  // namespace

BENCHMARK(FullCardRandRead)->Arg(0)->Arg(1)->Arg(2);
BENCHMARK(FullCardRewrite)->Args({0, 1})->Args({1, 1})->Args({1, 0})->Args({2, 1});
BENCHMARK(FullCardFill)->Arg(0)->Arg(1)->Arg(2)->Unit(benchmark::kMillisecond)->Iterations(2);
BENCHMARK(RawImageSeqRead);
BENCHMARK(RawImageRandRead);
BENCHMARK(MemoryImageRandRead);
BENCHMARK(BigCompositeExport)->Args({1, 1})->Args({1, 0})->Args({2, 1})->Unit(benchmark::kMillisecond)->Iterations(3);
BENCHMARK(BlankCard)->Arg(1)->Arg(0)->Unit(benchmark::kMillisecond);
