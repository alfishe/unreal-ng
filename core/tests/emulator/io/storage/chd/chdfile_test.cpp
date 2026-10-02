// CHD reading (docs/inprogress/2026-10-02-media-chd/design.md §3): every hard-disk codec chdman writes, parents,
// v3 / v4 headers, checksums, and damaged files. The fixtures are chdman's own output
// (testdata/media/chd/, tools/chd/make-test-fixtures.py); hand-built v3 / v4 files cover the older headers chdman no
// longer writes.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <map>

#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/chd/chdcodec.h"
#include "emulator/io/storage/chd/chdfile.h"
#include "chdtesthelper.h"
#include "emulator/io/storage/chd/chdwriter.h"

using namespace chd;
using namespace chdtest;

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    /// How many hunks of each kind: "zlib" / "lzma" / ... for codec hunks, else "none", "self", "parent", "zero"
    std::map<std::string, int> Census(const ChdFile& file)
    {
        std::map<std::string, int> census;
        for (uint32_t h = 0; h < file.HunkCount(); h++)
        {
            const HunkEntry& e = file.Entry(h);
            switch (e.type)
            {
                case HunkEntry::Type::Codec: census[CodecName(file.Codecs()[e.codec])]++; break;
                case HunkEntry::Type::Uncompressed: census["none"]++; break;
                case HunkEntry::Type::Self: census["self"]++; break;
                case HunkEntry::Type::Parent: census["parent"]++; break;
                case HunkEntry::Type::Zero: census["zero"]++; break;
                case HunkEntry::Type::Mini: census["mini"]++; break;
            }
        }
        return census;
    }

    void ExpectSameAsImage(ChdFile& file, const std::vector<uint8_t>& image, const std::string& name)
    {
        ASSERT_EQ(file.LogicalBytes(), image.size()) << name;
        std::vector<uint8_t> hunk(file.HunkBytes());
        for (uint32_t h = 0; h < file.HunkCount(); h++)
        {
            std::string error;
            ASSERT_TRUE(file.ReadHunk(h, hunk.data(), &error)) << name << ": " << error;
            ASSERT_EQ(0, std::memcmp(hunk.data(), image.data() + static_cast<size_t>(h) * file.HunkBytes(), file.HunkBytes()))
                << name << ": hunk " << h << " differs from the source image";
        }
    }

    /// region <Hand-built v3 / v4 files>

    void Put(std::vector<uint8_t>& v, size_t at, uint64_t value, int bytes)
    {
        for (int i = 0; i < bytes; i++)
            v[at + i] = static_cast<uint8_t>(value >> (8 * (bytes - 1 - i)));
    }

    /// Four 1 KB hunks: zlib-compressed text, a stored block, a "mini" (8 bytes repeated), a copy of hunk 1; GDDD metadata
    std::vector<uint8_t> BuildV34(int version, std::vector<uint8_t>& image)
    {
        const uint32_t hunk = 1024;
        image.assign(4 * hunk, 0);
        for (uint32_t i = 0; i < hunk; i++)
            image[i] = static_cast<uint8_t>("the disk of an old chdman "[i % 26]);
        for (uint32_t i = 0; i < hunk; i++)
            image[hunk + i] = static_cast<uint8_t>((i * 7919u) >> 3);
        const uint8_t mini[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        for (uint32_t i = 0; i < hunk; i++)
            image[2 * hunk + i] = mini[i % 8];
        std::memcpy(image.data() + 3 * hunk, image.data() + hunk, hunk);

        const size_t headerBytes = version == 3 ? 120 : 108;
        const size_t mapOffset = headerBytes;
        const size_t metaOffset = mapOffset + 4 * 16;
        MetadataEntry gddd = HardDiskMetadata(BlockGeometry{4, 2, 1});
        std::vector<uint8_t> file(metaOffset + 16 + gddd.data.size(), 0);
        Put(file, metaOffset, gddd.tag, 4);
        file[metaOffset + 4] = gddd.flags;
        Put(file, metaOffset + 5, gddd.data.size(), 3);
        std::memcpy(file.data() + metaOffset + 16, gddd.data.data(), gddd.data.size());

        auto zlib = CreateCodec(kCodecZlib, hunk);
        std::vector<uint8_t> packed(hunk);
        uint32_t packedBytes = 0;
        EXPECT_TRUE(zlib->Compress(image.data(), hunk, packed.data(), packedBytes));

        auto entry = [&](uint32_t index, uint64_t offset, uint32_t length, uint8_t type) {
            const size_t e = mapOffset + index * 16;
            Put(file, e, offset, 8);
            Put(file, e + 8, Crc32(image.data() + index * hunk, hunk), 4);
            Put(file, e + 12, length & 0xFFFF, 2);
            file[e + 14] = static_cast<uint8_t>(length >> 16);
            file[e + 15] = type;
        };
        entry(0, file.size(), packedBytes, 1);
        file.insert(file.end(), packed.begin(), packed.begin() + packedBytes);
        entry(1, file.size(), hunk, 2);
        file.insert(file.end(), image.begin() + hunk, image.begin() + 2 * hunk);
        uint64_t miniValue = 0;
        for (uint8_t b : mini)
            miniValue = (miniValue << 8) | b;
        entry(2, miniValue, 0, 3);
        entry(3, 1, 0, 4);

        const Sha1 raw = Sha1Of(image.data(), image.size());
        std::memcpy(file.data(), "MComprHD", 8);
        Put(file, 8, headerBytes, 4);
        Put(file, 12, static_cast<uint64_t>(version), 4);
        Put(file, 16, 0, 4);  // flags: no parent, writable
        Put(file, 20, 1, 4);  // zlib
        Put(file, 24, 4, 4);  // hunks
        Put(file, 28, image.size(), 8);
        Put(file, 36, metaOffset, 8);
        if (version == 3)
        {
            Put(file, 76, hunk, 4);
            std::memcpy(file.data() + 80, raw.data(), 20);
        }
        else
        {
            Put(file, 44, hunk, 4);
            const Sha1 overall = ChdFile::OverallSha1(raw, {gddd});
            std::memcpy(file.data() + 48, overall.data(), 20);
            std::memcpy(file.data() + 88, raw.data(), 20);
        }
        return file;
    }

    /// endregion </Hand-built v3 / v4 files>
}  // namespace

TEST(ChdFile_Test, EveryChdmanCodecReadsTheSourceDisk)
{
    const std::vector<uint8_t> image = MixedImage();
    ASSERT_EQ(image.size(), kHunk * kHunks) << "testdata/media/chd/mixed.img is missing";

    // fixture -> the codec it must have used for some hunks
    const std::vector<std::pair<std::string, std::string>> variants = {
        {"mixed-none.chd", "none"}, {"mixed-zlib.chd", "zlib"}, {"mixed-lzma.chd", "lzma"}, {"mixed-huff.chd", "huff"},
        {"mixed-flac.chd", "flac"}, {"mixed-zstd.chd", "zstd"}, {"mixed-default.chd", "lzma"}};
    for (const auto& [name, codec] : variants)
    {
        std::string error;
        auto file = ChdFile::Open(Fixture(name), &error);
        ASSERT_NE(file, nullptr) << error;
        EXPECT_EQ(file->Version(), 5u);
        EXPECT_EQ(file->HunkBytes(), kHunk);
        EXPECT_EQ(file->UnitBytes(), 512u);
        EXPECT_EQ(file->MetadataText(kTagHardDisk).value_or(""), "CYLS:8,HEADS:4,SECS:16,BPS:512") << name;
        ExpectSameAsImage(*file, image, name);
        const auto census = Census(*file);
        EXPECT_GT(census.count(codec) ? census.at(codec) : 0, 0) << name << " stores no " << codec << " hunk";
        EXPECT_TRUE(file->Verify(&error)) << error;
        if (codec != "none")
        {
            EXPECT_GT(census.count("self") ? census.at("self") : 0, 0) << name << ": the repeated text is a self reference";
            EXPECT_FALSE(IsNull(file->RawSha1())) << name;
        }
    }

    // chdman's default set picks a codec per hunk: the sine wave is FLAC, the skewed bytes Huffman
    auto mixed = ChdFile::Open(Fixture("mixed-default.chd"));
    ASSERT_NE(mixed, nullptr);
    const auto census = Census(*mixed);
    EXPECT_GT(census.count("flac") ? census.at("flac") : 0, 0);
    EXPECT_GT(census.count("huff") ? census.at("huff") : 0, 0);
    EXPECT_EQ(FormatCodecList(mixed->Codecs()), "lzma,zlib,huff,flac");
}

TEST(ChdFile_Test, ChildReadsItsChangesAndTheParentRest)
{
    std::string error;
    auto child = ChdFile::Open(Fixture("child-of-default.chd"), &error);
    ASSERT_NE(child, nullptr) << error;
    ASSERT_NE(child->Parent(), nullptr);
    EXPECT_EQ(child->Parent()->OverallSha1(), child->ParentSha1());
    ExpectSameAsImage(*child, ChildImage(), "child-of-default.chd");
    const auto census = Census(*child);
    EXPECT_EQ(census.at("parent"), 63) << "every hunk but the changed one comes from the parent";
    EXPECT_TRUE(child->Verify(&error)) << error;
}

TEST(ChdFile_Test, AChildWithoutItsParentIsRefused)
{
    ScratchFolder folder("chd-orphan");
    const std::string path = Utf8(folder.Path() / "child.chd");
    std::filesystem::copy_file(FileHelper::ToFsPath(Fixture("child-of-default.chd")), FileHelper::ToFsPath(path));
    std::string error;
    EXPECT_EQ(ChdFile::Open(path, &error), nullptr);
    EXPECT_NE(error.find("parent"), std::string::npos) << error;

    // A parent handed in that is not the one: refused by its SHA-1 (data and metadata; any other CHD of the
    // same disk, whatever its codecs, would have the same SHA-1 and be a valid parent)
    auto wrongParent = [](const Sha1&, const std::string&, std::string*) { return ChdFile::Open(Fixture("child-of-default.chd")); };
    error.clear();
    EXPECT_EQ(ChdFile::Open(path, &error, wrongParent), nullptr);
    EXPECT_NE(error.find("SHA-1"), std::string::npos) << error;

    // The parent copied next to it: found by its SHA-1, whatever its name
    std::filesystem::copy_file(FileHelper::ToFsPath(Fixture("mixed-default.chd")), folder.Path() / "any-name.chd");
    auto child = ChdFile::Open(path, &error);
    ASSERT_NE(child, nullptr) << error;
    ExpectSameAsImage(*child, ChildImage(), "child next to its parent");
}

TEST(ChdFile_Test, ADamagedHunkFailsItsCheckAndTheRestStillReads)
{
    ScratchFolder folder("chd-damaged");
    std::vector<uint8_t> bytes = ReadFile(Fixture("mixed-lzma.chd"));
    auto original = ChdFile::Open(Fixture("mixed-lzma.chd"));
    ASSERT_NE(original, nullptr);
    uint32_t victim = 0;
    while (victim < original->HunkCount() && original->Entry(victim).type != HunkEntry::Type::Codec)
        victim++;
    ASSERT_LT(victim, original->HunkCount());
    const HunkEntry entry = original->Entry(victim);
    bytes[static_cast<size_t>(entry.offset + entry.length / 2)] ^= 0x5A;
    const std::string path = Utf8(folder.Path() / "damaged.chd");
    WriteFile(path, bytes);

    std::string error;
    auto file = ChdFile::Open(path, &error);
    ASSERT_NE(file, nullptr) << error;
    std::vector<uint8_t> hunk(kHunk);
    EXPECT_FALSE(file->ReadHunk(victim, hunk.data(), &error));
    EXPECT_NE(error.find("hunk " + std::to_string(victim)), std::string::npos) << error;
    uint32_t other = victim + 1;
    while (other < file->HunkCount() && file->Entry(other).type != HunkEntry::Type::Codec)
        other++;
    ASSERT_LT(other, file->HunkCount());
    EXPECT_TRUE(file->ReadHunk(other, hunk.data(), &error)) << error;
    EXPECT_FALSE(file->Verify(&error));
}

TEST(ChdFile_Test, ADamagedOrCutShortFileIsRefused)
{
    ScratchFolder folder("chd-broken");
    std::string error;

    // A flipped bit in the compressed map: its CRC-16 fails
    std::vector<uint8_t> bytes = ReadFile(Fixture("mixed-default.chd"));
    ASSERT_GT(bytes.size(), 124u);
    const uint64_t mapOffset = Be64(bytes.data() + 40);
    const uint32_t mapBytes = Be32(bytes.data() + mapOffset);
    bytes[static_cast<size_t>(mapOffset + 16 + mapBytes / 2)] ^= 0x10;
    const std::string badMap = Utf8(folder.Path() / "bad-map.chd");
    WriteFile(badMap, bytes);
    EXPECT_EQ(ChdFile::Open(badMap, &error), nullptr);
    EXPECT_FALSE(error.empty());

    // A compressed file cut short loses its map (at the end)
    bytes = ReadFile(Fixture("mixed-default.chd"));
    bytes.resize(bytes.size() / 2);
    const std::string cut = Utf8(folder.Path() / "cut.chd");
    WriteFile(cut, bytes);
    error.clear();
    EXPECT_EQ(ChdFile::Open(cut, &error), nullptr);
    EXPECT_NE(error.find("map"), std::string::npos) << error;

    // An uncompressed one cut short opens (its map comes first) and fails the reads past the cut
    bytes = ReadFile(Fixture("mixed-none.chd"));
    bytes.resize(bytes.size() / 2);
    const std::string cutPlain = Utf8(folder.Path() / "cut-plain.chd");
    WriteFile(cutPlain, bytes);
    auto plain = ChdFile::Open(cutPlain, &error);
    ASSERT_NE(plain, nullptr) << error;
    std::vector<uint8_t> hunk(kHunk);
    EXPECT_TRUE(plain->ReadHunk(4, hunk.data(), &error)) << error;
    EXPECT_FALSE(plain->ReadHunk(kHunks - 1, hunk.data(), &error));
    EXPECT_NE(error.find("cut short"), std::string::npos) << error;

    // Not a CHD; a version MAME no longer reads
    const std::string notChd = Utf8(folder.File("plain.chd", std::string(4096, 'x')));
    EXPECT_EQ(ChdFile::Open(notChd, &error), nullptr);
    EXPECT_NE(error.find("MComprHD"), std::string::npos) << error;
    bytes = ReadFile(Fixture("mixed-none.chd"));
    bytes[15] = 2;  // version 2
    const std::string v2 = Utf8(folder.Path() / "v2.chd");
    WriteFile(v2, bytes);
    EXPECT_EQ(ChdFile::Open(v2, &error), nullptr);
    EXPECT_NE(error.find("version 2"), std::string::npos) << error;
}

TEST(ChdFile_Test, V3AndV4FilesReadEveryHunkType)
{
    ScratchFolder folder("chd-v34");
    for (int version : {3, 4})
    {
        std::vector<uint8_t> image;
        const std::vector<uint8_t> bytes = BuildV34(version, image);
        const std::string path = Utf8(folder.Path() / ("v" + std::to_string(version) + ".chd"));
        WriteFile(path, bytes);
        std::string error;
        auto file = ChdFile::Open(path, &error);
        ASSERT_NE(file, nullptr) << error;
        EXPECT_EQ(file->Version(), static_cast<uint32_t>(version));
        EXPECT_EQ(file->HunkCount(), 4u);
        EXPECT_EQ(file->UnitBytes(), 512u) << "v3 / v4 take the unit from GDDD's BPS";
        EXPECT_EQ(file->Entry(0).type, HunkEntry::Type::Codec);
        EXPECT_EQ(file->Entry(2).type, HunkEntry::Type::Mini);
        EXPECT_EQ(file->Entry(3).type, HunkEntry::Type::Self);
        ExpectSameAsImage(*file, image, path);
        EXPECT_TRUE(file->Verify(&error)) << error;

        // A CRC-32 mismatch in the stored hunk
        std::vector<uint8_t> damaged = bytes;
        damaged[damaged.size() - 3] ^= 1;
        WriteFile(path, damaged);
        file = ChdFile::Open(path, &error);
        ASSERT_NE(file, nullptr) << error;
        std::vector<uint8_t> hunk(1024);
        EXPECT_FALSE(file->ReadHunk(1, hunk.data(), &error));
        EXPECT_NE(error.find("CRC"), std::string::npos) << error;
    }
}

TEST(ChdFile_Test, CdAndLaserDiscChdsAreRefusedWithAReason)
{
    ScratchFolder folder("chd-cd");
    WriteOptions options;
    MetadataEntry track;
    track.tag = MakeTag('C', 'H', 'T', '2');
    track.flags = kMetadataChecksum;
    const std::string text = "TRACK:1 TYPE:MODE1 SUBTYPE:NONE FRAMES:4";
    track.data.assign(text.begin(), text.end());
    options.metadata = {track};
    const std::vector<uint8_t> disk(8192, 0x41);
    const std::string path = Utf8(folder.Path() / "cd.chd");
    auto read = [&disk](uint32_t hunk, uint8_t* dst, std::string*) {
        std::memcpy(dst, disk.data() + static_cast<size_t>(hunk) * 4096, 4096);
        return true;
    };
    std::string error;
    ASSERT_TRUE(WriteChd(path, disk.size(), read, options, &error)) << error;
    EXPECT_EQ(ChdFile::Open(path, &error), nullptr);
    EXPECT_NE(error.find("CD-ROM"), std::string::npos) << error;

    // A CD codec in the header
    std::vector<uint8_t> bytes = ReadFile(Fixture("mixed-lzma.chd"));
    PutBe32(bytes.data() + 16, kCodecCdLzma);
    WriteFile(path, bytes);
    EXPECT_EQ(ChdFile::Open(path, &error), nullptr);
    EXPECT_NE(error.find("cdlz"), std::string::npos) << error;
}
