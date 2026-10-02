// Writing CHD v5 (chdwriter.h): every codec, uncompressed, children, reuse of stored hunks (save), geometry, and
// the round trip CHD -> raw image -> CHD. With UNREAL_CHDMAN set to a chdman binary, chdman itself checks every
// variant we write: `verify` (SHA-1s), `info` and `extracthd` byte for byte.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

#include "_helpers/scratchfolder.h"
#include "common/filehelper.h"
#include "emulator/io/storage/chd/chdimage.h"
#include "chdtesthelper.h"
#include "emulator/io/storage/chd/chdwriter.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/media/medium.h"

using namespace chd;
using namespace chdtest;

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    HunkReader FromImage(const std::vector<uint8_t>& image)
    {
        return [&image](uint32_t hunk, uint8_t* dst, std::string*) {
            std::memcpy(dst, image.data() + static_cast<size_t>(hunk) * kHunk, kHunk);
            return true;
        };
    }

    WriteOptions WithCodecs(const std::string& names)
    {
        WriteOptions options;
        EXPECT_TRUE(ParseCodecList(names, options.codecs));
        options.metadata = {HardDiskMetadata(BlockGeometry{8, 4, 16})};
        return options;
    }

    int Count(const ChdFile& file, HunkEntry::Type type, uint32_t codec = 0)
    {
        int n = 0;
        for (uint32_t h = 0; h < file.HunkCount(); h++)
        {
            const HunkEntry& e = file.Entry(h);
            if (e.type == type && (type != HunkEntry::Type::Codec || file.Codecs()[e.codec] == codec))
                n++;
        }
        return n;
    }

    void ExpectReadsAs(const std::string& path, const std::vector<uint8_t>& image)
    {
        std::string error;
        auto file = ChdFile::Open(path, &error);
        ASSERT_NE(file, nullptr) << error;
        ASSERT_EQ(file->LogicalBytes(), image.size());
        std::vector<uint8_t> all(image.size());
        ASSERT_TRUE(file->ReadBytes(0, all.data(), static_cast<uint32_t>(all.size()), &error)) << error;
        EXPECT_TRUE(all == image) << path;
        EXPECT_TRUE(file->Verify(&error)) << error;
    }

    std::string Quote(const std::string& path)
    {
        return "\"" + path + "\"";
    }
}  // namespace

// Seven CHDs of 64 hunks through the real encoders (LZMA, FLAC, zstd level 19): ~150 ms, over the 50 ms budget
TEST(ChdWriter_Test, EveryCodecWritesAndReadsBack)
{
    const std::vector<uint8_t> image = MixedImage();
    ASSERT_EQ(image.size(), kHunk * kHunks);
    ScratchFolder folder("chd-write");

    const std::vector<std::pair<std::string, uint32_t>> variants = {{"zlib", kCodecZlib}, {"lzma", kCodecLzma},
                                                                    {"huff", kCodecHuffman}, {"flac", kCodecFlac},
                                                                    {"zstd", kCodecZstd}, {"default", kCodecFlac}};
    for (const auto& [names, codec] : variants)
    {
        const std::string path = Utf8(folder.Path() / ("w-" + names + ".chd"));
        std::string error;
        ASSERT_TRUE(WriteChd(path, image.size(), FromImage(image), WithCodecs(names), &error)) << error;
        ExpectReadsAs(path, image);
        auto file = ChdFile::Open(path);
        ASSERT_NE(file, nullptr);
        EXPECT_GT(Count(*file, HunkEntry::Type::Codec, codec), 0) << names << " stores no " << CodecName(codec) << " hunk";
        EXPECT_GT(Count(*file, HunkEntry::Type::Self), 0) << names << ": repeated hunks are self references";
        EXPECT_GT(Count(*file, HunkEntry::Type::Uncompressed), 0) << names << ": noise is stored as is";
        EXPECT_FALSE(IsNull(file->RawSha1()));
        EXPECT_EQ(file->MetadataText(kTagHardDisk).value_or(""), "CYLS:8,HEADS:4,SECS:16,BPS:512");
    }

    // Uncompressed: zero hunks are not stored, the rest sit on hunk boundaries, no checksums (as MAME)
    const std::string plain = Utf8(folder.Path() / "w-none.chd");
    std::string error;
    ASSERT_TRUE(WriteChd(plain, image.size(), FromImage(image), WithCodecs("none"), &error)) << error;
    ExpectReadsAs(plain, image);
    auto file = ChdFile::Open(plain);
    ASSERT_NE(file, nullptr);
    EXPECT_EQ(Count(*file, HunkEntry::Type::Zero), 4);
    EXPECT_EQ(file->Entry(4).offset % kHunk, 0u);
    EXPECT_TRUE(IsNull(file->RawSha1()));
    EXPECT_EQ(FileHelper::GetFileSize(plain), ReadFile(Fixture("mixed-none.chd")).size()) << "chdman's layout, byte count for byte count";
}

TEST(ChdWriter_Test, ChildStoresOnlyWhatDiffersFromItsParent)
{
    const std::vector<uint8_t> child = ChildImage();
    ScratchFolder folder("chd-child");
    std::string error;
    auto parent = ChdFile::Open(Fixture("mixed-default.chd"), &error);
    ASSERT_NE(parent, nullptr) << error;

    for (const std::string names : {"default", "none"})
    {
        WriteOptions options = WithCodecs(names);
        options.parent = parent.get();
        const std::string path = Utf8(folder.Path() / ("child-" + names + ".chd"));
        ASSERT_TRUE(WriteChd(path, child.size(), FromImage(child), options, &error)) << error;

        // Read with the parent handed in (it lives in testdata, not next to the child)
        auto finder = [](const Sha1&, const std::string&, std::string* e) { return ChdFile::Open(Fixture("mixed-default.chd"), e); };
        auto file = ChdFile::Open(path, &error, finder);
        ASSERT_NE(file, nullptr) << error;
        std::vector<uint8_t> all(child.size());
        ASSERT_TRUE(file->ReadBytes(0, all.data(), static_cast<uint32_t>(all.size()), &error)) << error;
        EXPECT_TRUE(all == child) << names;
        // Hunks 5 and 30 differ from the same hunks of the parent. Compressed, the zeroed hunk 30 still
        // points at a parent hunk of zeros (as chdman's child does); uncompressed, a parent hunk is only the same one
        EXPECT_EQ(Count(*file, HunkEntry::Type::Parent), names == "none" ? 62 : 63) << names;
        EXPECT_TRUE(file->Verify(&error)) << error;
        EXPECT_EQ(file->ParentSha1(), parent->OverallSha1());
    }
}

// Six CHDs of 64 hunks through the real encoders, chdman's default set among them: ~130 ms, over the 50 ms budget
TEST(ChdWriter_Test, ChdToImageToChdIsByteIdentical)
{
    ScratchFolder folder("chd-roundtrip");
    const std::vector<uint8_t> image = MixedImage();
    for (const std::string names : {"none", "default", "zstd"})
    {
        const std::string first = Utf8(folder.Path() / ("first-" + names + ".chd"));
        std::string error;
        ASSERT_TRUE(WriteChd(first, image.size(), FromImage(image), WithCodecs(names), &error)) << error;

        // CHD -> raw image through the block device
        auto chd = ChdImage::Open(first, &error);
        ASSERT_NE(chd, nullptr) << error;
        const std::string raw = Utf8(folder.Path() / ("back-" + names + ".img"));
        ASSERT_TRUE(ExportBlockDevice(*chd, raw, &error)) << error;
        EXPECT_TRUE(ReadFile(raw) == image) << names;

        // raw -> CHD again: the same bytes
        const std::vector<uint8_t> again = ReadFile(raw);
        const std::string second = Utf8(folder.Path() / ("second-" + names + ".chd"));
        ASSERT_TRUE(WriteChd(second, again.size(), FromImage(again), WithCodecs(names), &error)) << error;
        EXPECT_TRUE(ReadFile(first) == ReadFile(second)) << names;
    }
}

TEST(ChdWriter_Test, ASaveKeepsTheStoredHunksItDidNotChange)
{
    ScratchFolder folder("chd-reuse");
    std::string error;
    auto source = ChdFile::Open(Fixture("mixed-default.chd"), &error);
    ASSERT_NE(source, nullptr) << error;
    std::vector<uint8_t> image = MixedImage();
    image[40 * kHunk + 7] ^= 0xFF;  // hunk 40 is the guest's change

    WriteOptions options;
    options.codecs = source->Codecs();
    options.metadata = source->Metadata();
    options.reuse = source.get();
    options.unchanged = [](uint32_t hunk) { return hunk != 40; };
    const std::string path = Utf8(folder.Path() / "saved.chd");
    ASSERT_TRUE(WriteChd(path, image.size(), FromImage(image), options, &error)) << error;
    ExpectReadsAs(path, image);

    // chdman's FLAC hunks were copied, not encoded again by our (LPC-less) encoder: same stored bytes
    auto saved = ChdFile::Open(path);
    ASSERT_NE(saved, nullptr);
    for (uint32_t h = 0; h < kHunks; h++)
    {
        const HunkEntry& was = source->Entry(h);
        const HunkEntry& now = saved->Entry(h);
        if (h == 40 || was.type != HunkEntry::Type::Codec || now.type != HunkEntry::Type::Codec)
            continue;
        EXPECT_EQ(now.length, was.length) << "hunk " << h;
        EXPECT_EQ(now.codec, was.codec) << "hunk " << h;
    }
}

TEST(ChdWriter_Test, GeometryMetadata)
{
    const MetadataEntry entry = HardDiskMetadata(BlockGeometry{4096, 16, 32});
    EXPECT_EQ(std::string(entry.data.begin(), entry.data.end()), std::string("CYLS:4096,HEADS:16,SECS:32,BPS:512") + '\0');
    EXPECT_EQ(entry.flags, kMetadataChecksum);
    BlockGeometry g;
    uint32_t bps = 0;
    ASSERT_TRUE(ParseHardDiskMetadata("CYLS:8,HEADS:4,SECS:16,BPS:512", g, bps));
    EXPECT_EQ(g.cylinders, 8u);
    EXPECT_EQ(bps, 512u);
    EXPECT_FALSE(ParseHardDiskMetadata("TRACK:1", g, bps));

    // chdman's guess: the most sectors per track, then heads, that divide the disk
    auto guess = GuessGeometry(32768);
    ASSERT_TRUE(guess.has_value());
    EXPECT_EQ(guess->sectors, 32u);
    EXPECT_EQ(guess->heads, 16u);
    EXPECT_EQ(guess->cylinders, 64u);
    EXPECT_FALSE(GuessGeometry(32771).has_value()) << "a prime sector count";
}

// chdman (MAME 0.289 here) reads what we write. Optional: UNREAL_CHDMAN names the binary.
// Not under 50 ms: chdman runs as a process per check
TEST(ChdWriter_Test, ChdmanAcceptsEveryVariant)
{
    const char* chdman = std::getenv("UNREAL_CHDMAN");
    if (!chdman || !FileHelper::FileExists(chdman))
        GTEST_SKIP() << "UNREAL_CHDMAN (a chdman binary) not set";

    const std::vector<uint8_t> image = MixedImage();
    ScratchFolder folder("chd-chdman");
    std::string error;
    const std::string parentPath = Utf8(folder.Path() / "parent.chd");
    ASSERT_TRUE(WriteChd(parentPath, image.size(), FromImage(image), WithCodecs("default"), &error)) << error;
    auto parent = ChdFile::Open(parentPath, &error);
    ASSERT_NE(parent, nullptr) << error;

    struct Variant
    {
        std::string name;
        std::string codecs;
        bool child;
    };
    const std::vector<Variant> variants = {{"none", "none", false},      {"zlib", "zlib", false},   {"lzma", "lzma", false},
                                           {"huff", "huff", false},      {"flac", "flac", false},   {"zstd", "zstd", false},
                                           {"default", "default", false}, {"all5", "lzma,zstd,huff,flac", false},
                                           {"child", "default", true},   {"child-none", "none", true}};
    for (const Variant& v : variants)
    {
        const std::vector<uint8_t> data = v.child ? ChildImage() : image;
        WriteOptions options = WithCodecs(v.codecs);
        if (v.child)
            options.parent = parent.get();
        const std::string path = Utf8(folder.Path() / (v.name + ".chd"));
        ASSERT_TRUE(WriteChd(path, data.size(), FromImage(data), options, &error)) << error;

        const std::string withParent = v.child ? " -ip " + Quote(parentPath) : "";
        const std::string log = Utf8(folder.Path() / (v.name + ".log"));
        const std::string verify = Quote(chdman) + " verify -i " + Quote(path) + withParent + " > " + Quote(log) + " 2>&1";
        EXPECT_EQ(std::system(verify.c_str()), 0) << v.name << ":\n" << std::string(ReadFile(log).begin(), ReadFile(log).end());
        if (v.codecs != "none")
        {
            const std::vector<uint8_t> text = ReadFile(log);
            EXPECT_NE(std::string(text.begin(), text.end()).find("Overall SHA1 verification successful"), std::string::npos)
                << v.name << ":\n" << std::string(text.begin(), text.end());
        }
        const std::string info = Quote(chdman) + " info -v -i " + Quote(path) + " > " + Quote(log) + " 2>&1";
        EXPECT_EQ(std::system(info.c_str()), 0) << v.name;
        const std::string extracted = Utf8(folder.Path() / (v.name + ".img"));
        const std::string extract =
            Quote(chdman) + " extracthd -f -i " + Quote(path) + withParent + " -o " + Quote(extracted) + " > " + Quote(log) + " 2>&1";
        EXPECT_EQ(std::system(extract.c_str()), 0) << v.name;
        EXPECT_TRUE(ReadFile(extracted) == data) << v.name << ": chdman extracts other bytes";
    }
}
