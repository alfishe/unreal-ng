// HostFolderFat parity: the folder volumes built by the multi-source refactor
// must stay byte-identical to the volumes master built before it (multi-source
// design, tdd.md §5 "Parity gate", FR-34). The expected hashes below were
// recorded from master (phase C0); a changed hash means a changed volume.
//
// Every volume is hashed from LBA 0 to the end of its used clusters, plus every
// 1021st sector of the free space and the last sector

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/hostfolderfat.h"

namespace
{
    constexpr int64_t kTime = 1767268800;  // 2026-01-01 12:00:00 UTC

    std::string Pattern(size_t size, uint8_t seed)
    {
        std::string bytes(size, '\0');
        for (size_t i = 0; i < size; i++)
            bytes[i] = static_cast<char>((i * 31 + seed) & 0xFF);
        return bytes;
    }

    /// The worked example of the storage design plus corner cases
    void Mixed(ScratchFolder& f)
    {
        f.File("SD_BOOT.$C", Pattern(1297, 1));
        f.File("games/EYEACHE.TRD", Pattern(70000, 2));
        f.File("games/deep/er/than/that.bin", Pattern(5000, 3));
        f.File("Длинное имя.txt", "0123456789");
        f.File("empty.bin", "");
        f.File(".profile", "hidden on the host");
        f.File("LongFileName1.txt", "one");
        f.File("LongFileName2.txt", "two");
        f.Folder("empty-folder");
    }

    /// A root that outgrows 512 entries (FAT16 root region growth)
    void BigRoot(ScratchFolder& f)
    {
        for (int i = 0; i < 90; i++)
            f.File("a longer file name that needs five LFN entries " + std::to_string(1000 + i) + ".dat", Pattern(i * 7, 4));
    }

    /// Deep nesting, a directory spanning several clusters, names with spaces and dots
    void Deep(ScratchFolder& f)
    {
        std::string path;
        for (int depth = 0; depth < 8; depth++)
        {
            path += "level " + std::to_string(depth) + ".dir/";
            f.File(path + "one.bin", Pattern(1, 5));
            f.File(path + "two.data.bin", Pattern(513, 6));
            f.File(path + "Три.txt", Pattern(4097, 7));
        }
        for (int i = 0; i < 300; i++)
            f.File("many/file" + std::to_string(i) + ".c", Pattern(i % 37, 8));
    }

    /// Every size edge around sectors and clusters, and names that collide as 8.3
    void Sizes(ScratchFolder& f)
    {
        const size_t sizes[] = {0, 1, 511, 512, 513, 4095, 4096, 4097, 65536, 200000};
        for (size_t size : sizes)
            f.File("size-" + std::to_string(size) + ".bin", Pattern(size, static_cast<uint8_t>(size)));
        f.File("README.TXT", "upper");
        f.File("ReadMe.txt.bak", "mixed");
        f.File("read me.txt", "space");
        f.File("rEaDmE~1.TXT", "tilde");
        f.File("x+y=z;[1].txt", "illegal short-name characters");
        f.File("Ünïcödé.dat", "latin");
    }

    struct Variant
    {
        const char* name;
        FatType fs;
        bool mbr;
        CodePage page;
    };
    const Variant kVariants[] = {
        {"fat16-mbr-866", FatType::Fat16, true, CodePage::Cp866},
        {"fat16-mbr-1251", FatType::Fat16, true, CodePage::Cp1251},
        {"fat16-super-866", FatType::Fat16, false, CodePage::Cp866},
        {"fat32-mbr-866", FatType::Fat32, true, CodePage::Cp866},
        {"fat32-mbr-1251", FatType::Fat32, true, CodePage::Cp1251},
        {"fat32-super-866", FatType::Fat32, false, CodePage::Cp866},
    };

    uint64_t HashVolume(HostFolderFat& volume)
    {
        uint64_t hash = 0xcbf29ce484222325ULL;
        uint8_t sector[512];
        auto add = [&](uint64_t lba) {
            EXPECT_TRUE(volume.ReadSector(lba, sector)) << "LBA " << lba;
            for (int i = 0; i < 8; i++)
            {
                hash ^= static_cast<uint8_t>(lba >> (8 * i));
                hash *= 0x100000001b3ULL;
            }
            for (uint8_t b : sector)
            {
                hash ^= b;
                hash *= 0x100000001b3ULL;
            }
        };
        const uint64_t used = volume.UsedSectorEnd();
        const uint64_t total = volume.SectorCount();
        for (uint64_t lba = 0; lba < used; lba++)
            add(lba);
        for (uint64_t lba = used; lba < total; lba += 1021)
            add(lba);
        add(total - 1);
        return hash;
    }

    /// Recorded from master (C0, 2026-10-05) before the refactor
    const std::map<std::string, uint64_t> kExpected = {
        {"mixed/fat16-mbr-866", 0x3fa81d09d0794ad8ULL},
        {"mixed/fat16-mbr-1251", 0xd5f8cc1a7e63dce4ULL},
        {"mixed/fat16-super-866", 0xb142a7f9ec5dba9bULL},
        {"mixed/fat32-mbr-866", 0x7f269ad5997033dbULL},
        {"mixed/fat32-mbr-1251", 0x78da5b855ab7b36fULL},
        {"mixed/fat32-super-866", 0xa1d082b7222d2e1dULL},
        {"bigroot/fat16-mbr-866", 0x380dbf4702136912ULL},
        {"bigroot/fat16-mbr-1251", 0x61db8606fc40b812ULL},
        {"bigroot/fat16-super-866", 0x31ac1790f089f58dULL},
        {"bigroot/fat32-mbr-866", 0x4fb2ab210683aa92ULL},
        {"bigroot/fat32-mbr-1251", 0xd4614581a30195c2ULL},
        {"bigroot/fat32-super-866", 0xb43c4c486f2aa84cULL},
        {"deep/fat16-mbr-866", 0x3b92996a2057d037ULL},
        {"deep/fat16-mbr-1251", 0x9837924511306d57ULL},
        {"deep/fat16-super-866", 0xb3f2d4fc23f2a310ULL},
        {"deep/fat32-mbr-866", 0x8f574366e52cafa8ULL},
        {"deep/fat32-mbr-1251", 0x608f0529b0201228ULL},
        {"deep/fat32-super-866", 0x63a173be0eadf236ULL},
        {"sizes/fat16-mbr-866", 0x7b303f6aec4383a6ULL},
        {"sizes/fat16-mbr-1251", 0xc668c5d8d8915506ULL},
        {"sizes/fat16-super-866", 0xdca1338268bf2299ULL},
        {"sizes/fat32-mbr-866", 0xe4c82257e778df2aULL},
        {"sizes/fat32-mbr-1251", 0xf93361006d779e3aULL},
        {"sizes/fat32-super-866", 0x091388ad9f50170cULL},
    };
}  // namespace

/// Building and hashing 24 volumes takes ~0.1 s (over the 50 ms rule): the parity
/// gate of the refactor is worth it (it replaces 24 golden image files)
TEST(HostFolderFatParity_Test, CorpusHashesMatchMaster)
{
    struct Corpus
    {
        const char* name;
        void (*populate)(ScratchFolder&);
    };
    const Corpus corpora[] = {{"mixed", Mixed}, {"bigroot", BigRoot}, {"deep", Deep}, {"sizes", Sizes}};

    std::string recorded;
    for (const Corpus& corpus : corpora)
    {
        ScratchFolder folder(corpus.name);
        corpus.populate(folder);
        FolderSnapshot snapshot;
        ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, snapshot));
        for (const Variant& v : kVariants)
        {
            const std::string key = std::string(corpus.name) + "/" + v.name;
            SCOPED_TRACE(key);
            FatVolumeOptions options;
            options.fs = v.fs;
            options.mbr = v.mbr;
            options.codePage = v.page;
            options.freeBytes = 1024 * 1024;
            options.fixedTimeUtc = kTime;
            options.label = v.page == CodePage::Cp1251 ? "ТОМ 1251" : "PARITY";
            std::string error;
            std::vector<std::string> report;
            auto volume = HostFolderFat::Build(snapshot, options, &error, &report);
            ASSERT_NE(volume, nullptr) << error;

            const uint64_t hash = HashVolume(*volume);
            char line[96];
            std::snprintf(line, sizeof line, "        {\"%s\", 0x%016llxULL},\n", key.c_str(),
                          static_cast<unsigned long long>(hash));
            recorded += line;
            const auto it = kExpected.find(key);
            if (it != kExpected.end())
            {
                EXPECT_EQ(hash, it->second) << "the volume changed";
            }
        }
    }
    EXPECT_EQ(kExpected.size(), 24u) << "record the hashes (from master only):\n" << recorded;
}
