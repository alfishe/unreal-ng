// Composite media with ISO 9660: ISO images as layers, ISO targets for CD slots
// (multi-source phases/c5-iso.md; fs-compatibility.md S-7, S-8)

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/isoimagebuilder.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/io/storage/cd/iso9660reader.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/media/compositemediumfactory.h"
#include "emulator/media/mediaformatregistry.h"
#include "emulator/media/medium.h"

namespace
{
    std::string IsoRead(Iso9660Reader& reader, const std::string& path)
    {
        std::vector<uint8_t> data;
        std::string error;
        EXPECT_TRUE(reader.ReadFile(path, data, &error)) << path << ": " << error;
        return std::string(data.begin(), data.end());
    }

    std::string FatRead(FatVolumeReader& reader, const std::string& path)
    {
        std::vector<uint8_t> data;
        std::string error;
        EXPECT_TRUE(reader.ReadFile(path, data, &error)) << path << ": " << error;
        return std::string(data.begin(), data.end());
    }

    struct Sources
    {
        ScratchFolder root{"compose-iso"};

        void SaveIso(const char* leaf, IsoImageBuilder& iso)
        {
            const std::vector<uint8_t> bytes = iso.Build();
            std::ofstream(root.Path() / leaf, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                                       static_cast<std::streamsize>(bytes.size()));
        }

        MediaResult Build(const std::string& yaml, std::unique_ptr<IBlockDevice>& volume, CompositeInfo& info,
                          const CompositeBuildOptions& options = {})
        {
            root.File("cd.ucompose.yaml", yaml);
            return CompositeMediumFactory::Build(ComposeDescriptor::Load(root.Path() / "cd.ucompose.yaml"), options, volume, info);
        }
    };
}  // namespace

/// S-7: two ISOs and a host folder into one ISO CD; the upper ISO shadows the lower by Joliet name
TEST(ComposeIso_Test, IsoPlusFolderIntoIso)
{
    Sources s;
    IsoImageBuilder demos;
    demos.joliet = true;
    demos.Add("/DEMOS/EYEACHE.TRD", "eyeache", "EyeAche.trd");
    demos.Add("/README.TXT", "demos readme", "readme.txt");
    s.SaveIso("demos.iso", demos);
    IsoImageBuilder games;
    games.Add("/GAMES/ELITE.TRD", "elite");
    games.Add("/README.TXT", "games readme");
    s.SaveIso("games.iso", games);
    s.root.File("work/tool.com", "tool");

    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = s.Build("version: 1\ntarget: {kind: optical, fixedTime: 1767268800}\nlayers:\n"
                                       "  - {name: demos, source: {iso: demos.iso}}\n"
                                       "  - {name: games, source: {iso: games.iso}}\n"
                                       "  - {name: work, source: {folder: work}, mount: /WORK}\n",
                                       volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    EXPECT_EQ(info.fsName, "iso9660");
    EXPECT_EQ(info.sourceDevices, 2u);
    ASSERT_NE(dynamic_cast<CdImage*>(volume.get()), nullptr);

    Iso9660Reader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(IsoRead(reader, "/DEMOS/EyeAche.trd"), "eyeache");
    EXPECT_EQ(IsoRead(reader, "/GAMES/ELITE.TRD"), "elite");
    EXPECT_EQ(IsoRead(reader, "/WORK/tool.com"), "tool");
    // "readme.txt" (Joliet, demos) and "README.TXT" (games, no Joliet): two Joliet names, both kept
    std::vector<IsoDirEntry> entries;
    ASSERT_TRUE(reader.List(reader.Root(), entries));
    std::vector<std::string> names;
    for (const IsoDirEntry& e : entries)
        names.push_back(e.name);
    EXPECT_EQ(std::count(names.begin(), names.end(), "readme.txt"), 1);
    EXPECT_EQ(std::count(names.begin(), names.end(), "README.TXT"), 1);
}

/// S-8: a FAT image and a folder into an ISO CD
TEST(ComposeIso_Test, FatSourcesIntoIso)
{
    Sources s;
    ScratchFolder content("compose-iso-fat");
    content.File("DSS/command.com", "dss");
    content.File("Long Name.txt", "long");
    const FatSourceImage image = FolderToFatDisk(content.Path(), FatType::Fat16);
    ASSERT_TRUE(image.ok()) << image.error;
    ASSERT_TRUE(SaveSparse(image, s.root.Path() / "card.img"));
    s.root.File("extra/new.txt", "new");

    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = s.Build("version: 1\ntarget: {fs: iso9660}\nlayers:\n  - {source: {image: card.img}}\n"
                                       "  - {source: {folder: extra}}\n",
                                       volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    Iso9660Reader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(IsoRead(reader, "/DSS/command.com"), "dss");
    EXPECT_EQ(IsoRead(reader, "/Long Name.txt"), "long");
    EXPECT_EQ(IsoRead(reader, "/new.txt"), "new");
}

/// An ISO layer goes into a FAT target too
TEST(ComposeIso_Test, IsoLayerIntoFat)
{
    Sources s;
    IsoImageBuilder iso;
    iso.joliet = true;
    iso.Add("/GAMES/ELITE.TRD", std::string(7000, 'e'), "Elite.trd");
    s.SaveIso("games.iso", iso);
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result =
        s.Build("version: 1\ntarget: {free: 1MiB}\nlayers: [{source: {iso: games.iso}, from: /GAMES, mount: /G}]\n", volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    EXPECT_EQ(info.fsName, "fat16");
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(FatRead(reader, "/G/Elite.trd"), std::string(7000, 'e'));
}

/// An interleaved file is not one run of blocks per section, and an associated file shares its name with the
/// real one: both are left out with a report line rather than read wrong
TEST(ComposeIso_Test, InterleavedAndAssociatedFilesSkipped)
{
    Sources s;
    IsoImageBuilder iso;
    iso.joliet = false;  // the ISO 9660 records are the ones read (Joliet would be read instead)
    iso.Add("/GOOD.TXT", "good");
    iso.Add("/INTER.TXT", std::string(5000, 'i'));
    iso.Add("/ASSOC.TXT", "fork");
    std::vector<uint8_t> bytes = iso.Build();
    const auto mark = [&bytes](const std::string& isoName, size_t offset, uint8_t value) {
        const auto at = std::search(bytes.begin(), bytes.end(), isoName.begin(), isoName.end());
        ASSERT_NE(at, bytes.end()) << isoName;
        bytes[static_cast<size_t>(at - bytes.begin()) - 33 + offset] = value;  // the name starts at byte 33 of the record
    };
    mark("INTER.TXT;1", 26, 1);    // file unit size: interleaved
    mark("ASSOC.TXT;1", 25, 0x04); // flags: associated
    std::ofstream(s.root.Path() / "odd.iso", std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                                                    static_cast<std::streamsize>(bytes.size()));
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult result = s.Build("version: 1\ntarget: {free: 1MiB}\nlayers: [{source: {iso: odd.iso}}]\n", volume, info);
    ASSERT_TRUE(result.Ok()) << result.message;
    const auto has = [&result](const std::string& text) {
        return std::any_of(result.report.begin(), result.report.end(), [&text](const std::string& l) { return l.find(text) != std::string::npos; });
    };
    EXPECT_TRUE(has("INTER.TXT: skipped, recorded interleaved"));
    EXPECT_TRUE(has("ASSOC.TXT: skipped, an associated file"));
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(FatRead(reader, "/GOOD.TXT"), "good");
    FatDirEntryInfo entry;
    EXPECT_FALSE(reader.Stat("/INTER.TXT", entry));
}

/// The slot's kind and the target's must agree; graft is for FAT images
TEST(ComposeIso_Test, KindMismatches)
{
    Sources s;
    s.root.File("work/a.txt", "a");
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    CompositeBuildOptions hdd;
    hdd.slotKind = MediaKind::Block;
    CompositeBuildOptions cd;
    cd.slotKind = MediaKind::Optical;

    MediaResult r = s.Build("version: 1\ntarget: {fs: iso9660}\nlayers: [{source: {folder: work}}]\n", volume, info, hdd);
    EXPECT_EQ(r.error, MediaError::BadRequest);
    EXPECT_NE(r.message.find("needs a CD slot"), std::string::npos) << r.message;
    r = s.Build("version: 1\ntarget: {fs: fat32}\nlayers: [{source: {folder: work}}]\n", volume, info, cd);
    EXPECT_EQ(r.error, MediaError::BadRequest);
    r = s.Build("version: 1\ntarget: {build: graft}\nlayers: [{source: {folder: work}}]\n", volume, info, cd);
    EXPECT_EQ(r.error, MediaError::BadRequest);
    r = s.Build("version: 1\nlayers: [{source: {folder: work}}]\n", volume, info, cd);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(info.fsName, "iso9660") << "a CD slot makes the target ISO 9660";
    r = s.Build("version: 1\nlayers: [{source: {iso: missing.iso}}]\n", volume, info, cd);
    EXPECT_EQ(r.error, MediaError::UnreadableSource);
}

/// Through the registry into a CD slot: a read-only medium whose disc the drive gets
TEST(ComposeIso_Test, RegistryOpensADescriptorAsACd)
{
    Sources s;
    s.root.File("work/AUTORUN.ZX", "autorun");
    s.root.File("cd.ucompose.yaml", "version: 1\nlayers: [{source: {folder: work}}]\n");
    OpenRequest request;
    request.source.path = (s.root.Path() / "cd.ucompose.yaml").string();
    request.kind = MediaKind::Optical;
    request.access = AccessMode::ReadOnly;
    std::unique_ptr<Medium> medium;
    const MediaResult r = MediaFormatRegistry::Open(request, medium);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(medium->Format(), "compose-iso");
    EXPECT_EQ(medium->Access(), AccessMode::ReadOnly);
    ASSERT_NE(medium->Cd(), nullptr);
    EXPECT_EQ(medium->Cd()->TrackCount(), 1u);
    Iso9660Reader reader;
    ASSERT_TRUE(reader.Open(*medium->Cd()));
    EXPECT_EQ(IsoRead(reader, "/AUTORUN.ZX"), "autorun");
}
