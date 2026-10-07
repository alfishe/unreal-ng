// IsoSynthVolume: the ISO 9660 target of composite media, checked with the
// independent Iso9660Reader and by its ECMA-119 structure (multi-source phases/c5-iso.md §3)

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/io/storage/cd/iso9660reader.h"
#include "emulator/io/storage/cd/isosynthvolume.h"
#include "emulator/io/storage/compose/hostfoldersource.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"

namespace
{
    /// A folder as a FileTree, built into an ISO under a CdImage
    struct IsoCase
    {
        ScratchFolder folder{"iso-synth"};
        std::shared_ptr<SourcePool> pool = std::make_shared<SourcePool>();
        std::unique_ptr<CdImage> disc;
        uint32_t metadataBlocks = 0;

        bool Build(IsoTargetOptions options = {}, std::string* error = nullptr, std::vector<std::string>* report = nullptr)
        {
            FolderSnapshot snapshot;
            FolderScanOptions scan;
            scan.maxFileSize = UINT64_MAX;  // ISO 9660 has no 4 GiB limit (multi-extent files)
            EXPECT_TRUE(FolderSnapshot::Scan(folder.Path(), scan, snapshot));
            auto tree = std::make_shared<FileTree>();
            EXPECT_TRUE(HostFolderSource::Enumerate(snapshot, {}, *pool, *tree, nullptr, nullptr));
            options.fixedTimeUtc = 1767268800;
            auto volume = IsoSynthVolume::Build(tree, pool, options, error, report);
            if (!volume)
                return false;
            metadataBlocks = volume->MetadataBlocks();
            disc = IsoSynthVolume::MakeDisc(std::move(volume), "test", 1);
            return true;
        }

        std::vector<uint8_t> Block(uint32_t b)
        {
            std::vector<uint8_t> data(2048);
            EXPECT_EQ(disc->ReadUser(b, data.data()), CdImage::ReadResult::Ok);
            return data;
        }
    };

    std::vector<std::string> Names(Iso9660Reader& reader, const std::string& path)
    {
        IsoDirEntry dir;
        EXPECT_TRUE(reader.Stat(path, dir)) << path;
        std::vector<IsoDirEntry> entries;
        EXPECT_TRUE(reader.List(dir, entries));
        std::vector<std::string> names;
        for (const IsoDirEntry& e : entries)
            names.push_back(e.name);
        return names;
    }

    std::string Read(Iso9660Reader& reader, const std::string& path)
    {
        std::vector<uint8_t> data;
        std::string error;
        EXPECT_TRUE(reader.ReadFile(path, data, &error)) << path << ": " << error;
        return std::string(data.begin(), data.end());
    }
}  // namespace

/// Every file's bytes and names, through both trees
TEST(IsoSynthVolume_Test, ReadBackByReader)
{
    IsoCase c;
    c.folder.File("readme.txt", "hello");
    c.folder.File("Long File Name.dat", std::string(10000, 'L'));
    c.folder.File("games/elite.trd", std::string(5000, 'e'));
    c.folder.File("games/deep/er/x.bin", "x");
    c.folder.File("empty.txt", "");
    ASSERT_TRUE(c.Build());

    Iso9660Reader joliet;
    ASSERT_TRUE(joliet.Open(*c.disc));
    ASSERT_TRUE(joliet.HasJoliet());
    EXPECT_EQ(Names(joliet, "/"), (std::vector<std::string>{"Long File Name.dat", "empty.txt", "games", "readme.txt"}));
    EXPECT_EQ(Read(joliet, "/Long File Name.dat"), std::string(10000, 'L'));
    EXPECT_EQ(Read(joliet, "/games/elite.trd"), std::string(5000, 'e'));
    EXPECT_EQ(Read(joliet, "/games/deep/er/x.bin"), "x");
    EXPECT_EQ(Read(joliet, "/empty.txt"), "");

    Iso9660Reader iso;
    ASSERT_TRUE(iso.Open(*c.disc, nullptr, false));
    EXPECT_EQ(Names(iso, "/"), (std::vector<std::string>{"EMPTY.TXT", "GAMES", "LONG_FIL.DAT", "README.TXT"}));
    EXPECT_EQ(Read(iso, "/LONG_FIL.DAT"), std::string(10000, 'L'));
    IsoDirEntry entry;
    ASSERT_TRUE(iso.Stat("/README.TXT", entry));
    EXPECT_EQ(entry.isoName, "README.TXT;1");
    EXPECT_EQ(entry.mtimeUtc, 1767268800);
}

/// PVD at 16, the Joliet SVD at 17 with its escape, the terminator, path tables L and M agreeing
TEST(IsoSynthVolume_Test, PathTablesBothEndianAndDescriptorSet)
{
    IsoCase c;
    c.folder.File("A/B/f.txt", "f");
    c.folder.File("C/g.txt", "g");
    ASSERT_TRUE(c.Build());
    const std::vector<uint8_t> pvd = c.Block(16);
    const std::vector<uint8_t> svd = c.Block(17);
    const std::vector<uint8_t> end = c.Block(18);
    EXPECT_EQ(pvd[0], 1);
    EXPECT_EQ(std::string(pvd.begin() + 1, pvd.begin() + 6), "CD001");
    EXPECT_EQ(svd[0], 2);
    EXPECT_EQ(std::string(svd.begin() + 88, svd.begin() + 91), "%/E");
    EXPECT_EQ(end[0], 255);

    const uint32_t size = pvd[132] | (pvd[133] << 8) | (pvd[134] << 16) | (pvd[135] << 24);
    const uint32_t sizeBe = (pvd[136] << 24) | (pvd[137] << 16) | (pvd[138] << 8) | pvd[139];
    EXPECT_EQ(size, sizeBe);
    const uint32_t lBlock = pvd[140] | (pvd[141] << 8) | (pvd[142] << 16) | (pvd[143] << 24);
    const uint32_t mBlock = (pvd[148] << 24) | (pvd[149] << 16) | (pvd[150] << 8) | pvd[151];
    const std::vector<uint8_t> l = c.Block(lBlock);
    const std::vector<uint8_t> m = c.Block(mBlock);
    // Root, A, C, B: breadth first; each record's extent and parent in both byte orders
    size_t at = 0;
    int records = 0;
    while (at < size)
    {
        const uint8_t length = l[at];
        ASSERT_EQ(m[at], length);
        const uint32_t extentL = l[at + 2] | (l[at + 3] << 8) | (l[at + 4] << 16) | (l[at + 5] << 24);
        const uint32_t extentM = (m[at + 2] << 24) | (m[at + 3] << 16) | (m[at + 4] << 8) | m[at + 5];
        EXPECT_EQ(extentL, extentM);
        EXPECT_EQ(l[at + 6] | (l[at + 7] << 8), (m[at + 6] << 8) | m[at + 7]);
        at += 8 + length + (length % 2);
        records++;
    }
    EXPECT_EQ(records, 4);
}

/// The PVD and Joliet records of a file point at the same extent
TEST(IsoSynthVolume_Test, FilesSharedBetweenTrees)
{
    IsoCase c;
    c.folder.File("Some Long Name.bin", std::string(3000, 's'));
    ASSERT_TRUE(c.Build());
    Iso9660Reader joliet;
    Iso9660Reader iso;
    ASSERT_TRUE(joliet.Open(*c.disc));
    ASSERT_TRUE(iso.Open(*c.disc, nullptr, false));
    IsoDirEntry a;
    IsoDirEntry b;
    ASSERT_TRUE(joliet.Stat("/Some Long Name.bin", a));
    ASSERT_TRUE(iso.Stat("/SOME_LON.BIN", b));
    EXPECT_EQ(a.Block(), b.Block());
    EXPECT_GE(a.Block(), c.metadataBlocks) << "file data after the metadata";
}

/// Level 1 and 2 names, collisions get ~N tails (reported), Joliet names cut to 64
TEST(IsoSynthVolume_Test, NamesLevel1Level2AndCollisions)
{
    IsoCase c;
    c.folder.File("verylongname1.txt", "1");
    c.folder.File("verylongname2.txt", "2");
    c.folder.File(std::string(80, 'j') + ".txt", "j");
    std::vector<std::string> report;
    ASSERT_TRUE(c.Build({}, nullptr, &report));
    Iso9660Reader iso;
    ASSERT_TRUE(iso.Open(*c.disc, nullptr, false));
    std::vector<std::string> names = Names(iso, "/");
    std::set<std::string> unique(names.begin(), names.end());
    EXPECT_EQ(unique.size(), names.size()) << "ISO names unique";
    EXPECT_TRUE(unique.count("VERYLONG.TXT"));
    EXPECT_TRUE(unique.count("VERYLO~1.TXT"));
    EXPECT_FALSE(report.empty());
    Iso9660Reader joliet;
    ASSERT_TRUE(joliet.Open(*c.disc));
    for (const std::string& name : Names(joliet, "/"))
        EXPECT_LE(name.size(), 62u);

    IsoCase level2;
    level2.folder.File("a-rather-long-name.extension", "x");
    IsoTargetOptions options;
    options.level = 2;
    options.joliet = false;
    ASSERT_TRUE(level2.Build(options));
    Iso9660Reader l2;
    ASSERT_TRUE(l2.Open(*level2.disc));
    EXPECT_FALSE(l2.HasJoliet());
    EXPECT_EQ(Names(l2, "/"), (std::vector<std::string>{"A_RATHER_LONG_NAME.EXTENSIO"}));
}

/// Nine levels fail unless relaxed
TEST(IsoSynthVolume_Test, DepthLimit)
{
    IsoCase c;
    c.folder.File("1/2/3/4/5/6/7/8/deep.txt", "d");  // directory level 9 (the root is 1)
    std::string error;
    EXPECT_FALSE(c.Build({}, &error));
    EXPECT_NE(error.find("8 directory levels"), std::string::npos) << error;
    IsoTargetOptions relaxed;
    relaxed.relaxDepth = true;
    EXPECT_TRUE(c.Build(relaxed, &error)) << error;
    Iso9660Reader joliet;
    ASSERT_TRUE(joliet.Open(*c.disc));
    EXPECT_EQ(Read(joliet, "/1/2/3/4/5/6/7/8/deep.txt"), "d");
}

/// A sparse 4.1 GiB file is written as two sections of one multi-extent file
TEST(IsoSynthVolume_Test, MultiExtentFile)
{
    IsoCase c;
    const auto big = c.folder.File("big.bin", "");
    std::error_code ec;
    std::filesystem::resize_file(big, 4400ull * 1024 * 1024, ec);  // sparse: nothing is written
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_TRUE(c.Build());
    Iso9660Reader joliet;
    ASSERT_TRUE(joliet.Open(*c.disc));
    IsoDirEntry entry;
    ASSERT_TRUE(joliet.Stat("/big.bin", entry));
    ASSERT_EQ(entry.sections.size(), 2u);
    EXPECT_EQ(entry.size, 4400ull * 1024 * 1024);
    EXPECT_EQ(entry.sections[0].second, IsoSynthVolume::kMaxSection);
    EXPECT_EQ(entry.sections[1].first, entry.sections[0].first + IsoSynthVolume::kMaxSection / 2048) << "contiguous";
}

/// The CdImage over it: one Mode 1 data track ending at the volume's last block
TEST(IsoSynthVolume_Test, ThroughCdImageReadToc)
{
    IsoCase c;
    c.folder.File("a.txt", std::string(100000, 'a'));
    ASSERT_TRUE(c.Build());
    ASSERT_EQ(c.disc->TrackCount(), 1u);
    EXPECT_FALSE(c.disc->TrackAt(0).IsAudio());
    const std::vector<uint8_t> pvd = c.Block(16);
    const uint32_t blocks = pvd[80] | (pvd[81] << 8) | (pvd[82] << 16) | (pvd[83] << 24);
    EXPECT_EQ(c.disc->LeadOutLba(), blocks);
    EXPECT_EQ(c.disc->SectorCount(), static_cast<uint64_t>(blocks) * 4);
    uint8_t frame[2352];
    EXPECT_EQ(c.disc->ReadFrame(16, frame), CdImage::ReadResult::Ok) << "a whole Mode 1 frame (sync, header, EDC)";
}
