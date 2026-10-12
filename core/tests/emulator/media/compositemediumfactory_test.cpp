// CompositeMediumFactory: descriptors into FAT volumes (multi-source tdd.md §11, DT-6)

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "common/filemtime.h"
#include "emulator/io/storage/fat/fatsynthvolume.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/media/compositemediumfactory.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/medium.h"

namespace
{
    std::string Read(FatVolumeReader& reader, const std::string& path)
    {
        std::vector<uint8_t> data;
        std::string error;
        EXPECT_TRUE(reader.ReadFile(path, data, &error)) << path << ": " << error;
        return std::string(data.begin(), data.end());
    }

    std::vector<std::string> Names(FatVolumeReader& reader, const std::string& path)
    {
        std::vector<FatDirEntryInfo> entries;
        EXPECT_TRUE(reader.List(path, entries)) << path;
        std::vector<std::string> names;
        for (const auto& e : entries)
            names.push_back(e.name);
        return names;
    }

    struct Fixture
    {
        ScratchFolder root{"compose-factory"};
        ComposeDescriptor Descriptor(const std::string& yaml)
        {
            root.File("disk.ucompose.yaml", yaml);
            return ComposeDescriptor::Load(root.Path() / "disk.ucompose.yaml");
        }
    };

    const char* kTwoFolders = R"(
version: 1
target: {fixedTime: 1767268800, free: 1MiB}
layers:
  - {name: base, source: {folder: base}}
  - {name: builds, source: {folder: out}, mount: /BIN, include: ["*.com"]}
  - {name: patch, source: {folder: patch}, whiteout: [/old.txt]}
)";
}  // namespace

TEST(CompositeMediumFactory_Test, LayersMergedAndReadByOracle)
{
    Fixture f;
    f.root.File("base/boot.$C", "boot");
    f.root.File("base/old.txt", "old");
    f.root.File("base/readme.txt", "base readme");
    f.root.File("base/.unreal-media.yaml", "exclude: [\"*.tmp\"]\n");
    f.root.File("base/junk.tmp", "excluded by the folder's manifest");
    f.root.File("out/tool.com", "tool");
    f.root.File("out/tool.o", "not included");
    f.root.File("patch/README.TXT", "patched readme");

    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = CompositeMediumFactory::Build(f.Descriptor(kTwoFolders), {}, volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    ASSERT_NE(volume, nullptr);

    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(Names(reader, "/"), (std::vector<std::string>{"BIN", "README.TXT", "boot.$C"})) << "the manifest itself is a service file";
    EXPECT_EQ(Read(reader, "/README.TXT"), "patched readme");
    EXPECT_EQ(Read(reader, "/BIN/tool.com"), "tool");
    EXPECT_EQ(info.fs, FatType::Fat16) << "auto: FAT16 when it fits";
    ASSERT_EQ(info.layers.size(), 3u);
    EXPECT_EQ(info.layers[1].files, 1u) << "tool.com only";
    EXPECT_EQ(info.files, 3u) << "boot.$C, README.TXT, BIN/tool.com: old.txt is whited out";

    auto has = [&result](const std::string& text) {
        for (const std::string& line : result.report)
            if (line.find(text) != std::string::npos)
                return true;
        return false;
    };
    EXPECT_TRUE(has("tool.o: not included"));
    EXPECT_TRUE(has("/old.txt: whiteout"));
    EXPECT_TRUE(has("shadows"));
    EXPECT_TRUE(has("junk.tmp: skipped"));
}

TEST(CompositeMediumFactory_Test, ContentIdStableAndFollowsSources)
{
    Fixture f;
    f.root.File("base/a.bin", "a");
    f.root.File("out/x.com", "x");
    f.root.File("patch/p.txt", "p");
    std::unique_ptr<IBlockDevice> v1, v2, v3;
    CompositeInfo i1, i2, i3;
    ASSERT_TRUE(CompositeMediumFactory::Build(f.Descriptor(kTwoFolders), {}, v1, i1).Ok());
    ASSERT_TRUE(CompositeMediumFactory::Build(f.Descriptor(kTwoFolders), {}, v2, i2).Ok());
    EXPECT_EQ(i1.contentId, i2.contentId);
    f.root.File("out/y.com", "y");
    ASSERT_TRUE(CompositeMediumFactory::Build(f.Descriptor(kTwoFolders), {}, v3, i3).Ok());
    EXPECT_NE(i1.contentId, i3.contentId) << "a new file in a layer is another medium";
}

TEST(CompositeMediumFactory_Test, FsCandidatesFollowTheSlot)
{
    std::string error;
    using V = std::vector<FatType>;
    EXPECT_EQ(CompositeMediumFactory::FsCandidates(std::nullopt, {}, FatType::Fat16, &error), (V{FatType::Fat16, FatType::Fat32}));
    EXPECT_EQ(CompositeMediumFactory::FsCandidates(std::nullopt, {FatType::Fat32}, FatType::Fat16, &error), (V{FatType::Fat32}))
        << "TS-Conf: FAT32 only";
    EXPECT_EQ(CompositeMediumFactory::FsCandidates(std::nullopt, {FatType::Fat16}, FatType::Fat16, &error), (V{FatType::Fat16}))
        << "Sprinter: FAT16 only";
    EXPECT_EQ(CompositeMediumFactory::FsCandidates(FatType::Fat32, {}, FatType::Fat16, &error), (V{FatType::Fat32}));
    EXPECT_TRUE(CompositeMediumFactory::FsCandidates(FatType::Fat16, {FatType::Fat32}, FatType::Fat32, &error).empty());
    EXPECT_NE(error.find("fat32"), std::string::npos) << error;
}

TEST(CompositeMediumFactory_Test, SlotThatReadsOnlyFat32BuildsFat32)
{
    Fixture f;
    f.root.File("base/a.bin", "a");
    f.root.File("out/x.com", "x");
    f.root.File("patch/p.txt", "p");
    CompositeBuildOptions slot;
    slot.allowedFs = {FatType::Fat32};
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    ASSERT_TRUE(CompositeMediumFactory::Build(f.Descriptor(kTwoFolders), slot, volume, info).Ok());
    EXPECT_EQ(info.fs, FatType::Fat32);

    std::unique_ptr<IBlockDevice> refused;
    const MediaResult wrong = CompositeMediumFactory::Build(
        f.Descriptor("version: 1\ntarget: {fs: fat16}\nlayers: [{source: {folder: base}}]\n"), slot, refused, info);
    EXPECT_EQ(wrong.error, MediaError::BadRequest);
}

TEST(CompositeMediumFactory_Test, FixedSize)
{
    Fixture f;
    f.root.File("base/a.bin", std::string(10000, 'a'));
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    ASSERT_TRUE(CompositeMediumFactory::Build(f.Descriptor("version: 1\ntarget: {size: 64MiB}\nlayers: [{source: {folder: base}}]\n"),
                                              {}, volume, info).Ok());
    const uint64_t bytes = volume->SectorCount() * 512;
    EXPECT_GE(bytes, 63ull * 1024 * 1024);
    EXPECT_LE(bytes, 65ull * 1024 * 1024) << "about the size asked for (cluster rounding)";

    const MediaResult tooSmall = CompositeMediumFactory::Build(
        f.Descriptor("version: 1\ntarget: {size: 1MiB, fs: fat32}\nlayers: [{source: {folder: base}}]\n"), {}, volume, info);
    EXPECT_EQ(tooSmall.error, MediaError::DoesNotFit) << "FAT32 needs at least 65 526 clusters";
}

/// Partitions came with phase C7: an empty list is a bad descriptor, not a later phase
TEST(CompositeMediumFactory_Test, EmptyPartitionListRefused)
{
    Fixture f;
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult r = CompositeMediumFactory::Build(f.Descriptor("version: 1\npartitions: []\n"), {}, volume, info);
    EXPECT_EQ(r.error, MediaError::BadRequest) << r.message;
    EXPECT_NE(r.message.find("partitions"), std::string::npos) << r.message;
}

TEST(CompositeMediumFactory_Test, MissingFolderAndBadDescriptor)
{
    Fixture f;
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    EXPECT_EQ(CompositeMediumFactory::Build(f.Descriptor("version: 1\nlayers: [{source: {folder: nope}}]\n"), {}, volume, info).error,
              MediaError::UnreadableSource);
    EXPECT_EQ(CompositeMediumFactory::Build(f.Descriptor("layers: []\n"), {}, volume, info).error, MediaError::BadRequest);
}

TEST(CompositeMediumFactory_Test, RegistryOpensADescriptorAsABlockMedium)
{
    Fixture f;
    f.root.File("base/a.bin", "a");
    f.root.File("disk.ucompose.yaml", "version: 1\nlayers: [{source: {folder: base}}]\n");
    OpenRequest request;
    request.source.path = (f.root.Path() / "disk.ucompose.yaml").string();
    request.kind = MediaKind::Block;
    std::unique_ptr<Medium> medium;
    const MediaResult r = MediaFormatRegistry::Open(request, medium);
    ASSERT_TRUE(r.Ok()) << r.message;
    ASSERT_NE(medium, nullptr);
    EXPECT_EQ(medium->Source().type, MediaSourceType::Composite);
    EXPECT_EQ(medium->Format(), "compose-fat16");
    ASSERT_NE(medium->Composite(), nullptr);
    EXPECT_EQ(medium->Composite()->layers.size(), 1u);

    request.access = AccessMode::WriteThrough;
    EXPECT_EQ(MediaFormatRegistry::Open(request, medium).error, MediaError::KindMismatch);

    OpenRequest inlineRequest;
    inlineRequest.kind = MediaKind::Block;
    inlineRequest.source.inlineBody = "{\"version\": 1, \"layers\": [{\"source\": {\"folder\": \"" +
                                      (f.root.Path() / "base").generic_string() + "\"}}]}";
    std::unique_ptr<Medium> inlined;
    const MediaResult ri = MediaFormatRegistry::Open(inlineRequest, inlined);
    ASSERT_TRUE(ri.Ok()) << ri.message;
    EXPECT_EQ(inlined->Composite()->descriptor, "(inline)");
}

/// A mount point made for a folder layer takes the folder's time (not 1980-01-01)
TEST(CompositeMediumFactory_Test, MountPointCarriesTheLayerFolderTime)
{
    Fixture f;
    f.root.File("util/tool.com", "tool");
    ASSERT_TRUE(SetMTimeUnixSeconds(f.root.Path() / "util", 1767268800));  // 2026-01-01 12:00:00 UTC
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = CompositeMediumFactory::Build(
        f.Descriptor("version: 1\ntarget: {free: 1MiB}\nlayers: [{source: {folder: util}, mount: /UTIL}]\n"), {}, volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    FatDirEntryInfo util;
    ASSERT_TRUE(reader.Stat("/UTIL", util));
    EXPECT_EQ(FatVolumeReader::DosToUnix(util.date, util.time), 1767268800);
}

// FR-21: onBadName: skip leaves a host name FAT cannot hold out, replace renames it ('_'); both are reported.
// A ':' in a file name is a POSIX host's (Windows cannot make one)
#ifndef _WIN32
TEST(CompositeMediumFactory_Test, OnBadNameSkipsOrReplaces)
{
    Fixture f;
    f.root.File("files/a:b.txt", "colon");
    f.root.File("files/plain.txt", "plain");
    const auto build = [&f](const char* policy, std::vector<std::string>& report) {
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        const MediaResult built = CompositeMediumFactory::Build(
            f.Descriptor(std::string("version: 1\ntarget: {free: 1MiB, onBadName: ") + policy +
                         "}\nlayers: [{source: {folder: files}}]\n"),
            {}, volume, info);
        EXPECT_TRUE(built.Ok()) << built.message;
        report = built.report;
        return volume;
    };
    const auto has = [](const std::vector<std::string>& lines, const std::string& text) {
        return std::any_of(lines.begin(), lines.end(), [&text](const std::string& l) { return l.find(text) != std::string::npos; });
    };

    std::vector<std::string> report;
    std::unique_ptr<IBlockDevice> skipped = build("skip", report);
    ASSERT_NE(skipped, nullptr);
    EXPECT_TRUE(has(report, "a character FAT cannot store"));
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*skipped));
    EXPECT_EQ(Names(reader, "/").size(), 1u) << "only plain.txt";

    std::unique_ptr<IBlockDevice> replaced = build("replace", report);
    ASSERT_NE(replaced, nullptr);
    EXPECT_TRUE(has(report, "a:b.txt: renamed to a_b.txt (onBadName: replace)"));
    ASSERT_TRUE(reader.Open(*replaced));
    EXPECT_EQ(Read(reader, "/a_b.txt"), "colon");
    EXPECT_NE(skipped->ContentId(), replaced->ContentId());
}
#endif

// FR-6 / FR-14: a folder layer's manifest renames files, orders its root, and gives the volume its label and code
// page; the descriptor wins wherever it says the same (its layer `order`, target.label)
TEST(CompositeMediumFactory_Test, ManifestAndDescriptorOrderAndNames)
{
    Fixture f;
    f.root.File("files/aa.txt", "a");
    f.root.File("files/loader.bin", "loader");
    f.root.File("files/zz.txt", "z");
    f.root.File("files/.unreal-media.yaml",
                "label: MANIFEST\norder: [zz.txt]\nfiles:\n  loader.bin: {name: loader}\n  missing.bin: {name: gone}\n");
    const auto build = [&f](const std::string& yaml, std::vector<std::string>& report) {
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        const MediaResult built = CompositeMediumFactory::Build(f.Descriptor(yaml), {}, volume, info);
        EXPECT_TRUE(built.Ok()) << built.message;
        report = built.report;
        return volume;
    };
    const auto has = [](const std::vector<std::string>& lines, const std::string& text) {
        return std::any_of(lines.begin(), lines.end(), [&text](const std::string& l) { return l.find(text) != std::string::npos; });
    };

    std::vector<std::string> report;
    std::unique_ptr<IBlockDevice> manifestOnly = build("version: 1\ntarget: {free: 1MiB}\nlayers: [{source: {folder: files}}]\n", report);
    ASSERT_NE(manifestOnly, nullptr);
    EXPECT_TRUE(has(report, "manifest: files.missing.bin: no such file"));
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*manifestOnly));
    EXPECT_EQ(reader.Label(), "MANIFEST");
    std::vector<std::string> names = Names(reader, "/");
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names.front(), "zz.txt") << "the manifest's order";
    EXPECT_EQ(Read(reader, "/loader"), "loader") << "the manifest's name";
    EXPECT_TRUE(std::find(names.begin(), names.end(), "loader.bin") == names.end());

    std::unique_ptr<IBlockDevice> descriptor = build(
        "version: 1\ntarget: {free: 1MiB, label: DESCRIPTOR}\n"
        "layers: [{source: {folder: files}, order: {/: [aa.txt, nothere.txt]}}]\n",
        report);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_TRUE(has(report, "order: /: no entry 'nothere.txt'"));
    ASSERT_TRUE(reader.Open(*descriptor));
    EXPECT_EQ(reader.Label(), "DESCRIPTOR");
    names = Names(reader, "/");
    ASSERT_GE(names.size(), 2u);
    EXPECT_EQ(names[0], "aa.txt") << "the descriptor's order comes first";
    EXPECT_EQ(names[1], "zz.txt") << "then the manifest's";
    EXPECT_NE(manifestOnly->ContentId(), descriptor->ContentId());
}
