// D-6 boot carry-over: El Torito on ISO targets, MBR / volume boot code and
// reserved sectors on FAT targets, from the bottom layer or the boot section
// (multi-source phases/c5-iso.md §6; test-and-benchmark-plan.md §3.5)

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/scratchfolder.h"
#include "_helpers/testpathhelper.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/io/storage/cd/iso9660reader.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/hddimageformats.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    std::string Pattern(size_t size, uint8_t seed)
    {
        std::string s(size, '\0');
        for (size_t i = 0; i < size; i++)
            s[i] = static_cast<char>(seed + i * 3 + 1);
        return s;
    }

    bool Has(const std::vector<std::string>& lines, const std::string& text)
    {
        return std::any_of(lines.begin(), lines.end(), [&text](const std::string& l) { return l.find(text) != std::string::npos; });
    }

    struct Case
    {
        ScratchFolder root{"compose-boot"};

        MediaResult Build(const std::string& yaml, std::unique_ptr<IBlockDevice>& volume, CompositeInfo& info,
                          const CompositeBuildOptions& options = {})
        {
            root.File("disk.ucompose.yaml", yaml);
            return CompositeMediumFactory::Build(ComposeDescriptor::Load(root.Path() / "disk.ucompose.yaml"), options, volume, info);
        }

        /// The CD's user data as an .iso file
        void SaveIso(IBlockDevice& cd, const char* leaf)
        {
            std::ofstream out(root.Path() / leaf, std::ios::binary);
            uint8_t s[512];
            for (uint64_t lba = 0; lba < cd.SectorCount(); lba++)
            {
                cd.ReadSector(lba, s);
                out.write(reinterpret_cast<const char*>(s), 512);
            }
        }

        std::vector<uint8_t> Blocks(IBlockDevice& cd, uint32_t block, size_t bytes)
        {
            std::vector<uint8_t> out(bytes);
            uint8_t s[512];
            for (size_t at = 0; at < bytes; at += 512)
            {
                cd.ReadSector(static_cast<uint64_t>(block) * 4 + at / 512, s);
                std::memcpy(out.data() + at, s, std::min<size_t>(512, bytes - at));
            }
            return out;
        }
    };

    std::vector<uint8_t> Bytes(const std::string& s)
    {
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    const char* kBootableCd = "version: 1\ntarget: {kind: optical, fixedTime: 1767268800}\n"
                              "layers: [{source: {folder: cd}}]\n"
                              "boot:\n  eltorito:\n"
                              "    - {image: {host: floppy.img}, emulation: floppy}\n"
                              "    - {image: /BOOT/loader.bin, emulation: none, platform: efi, loadSegment: 0x07C0, sectors: 4}\n";
}  // namespace

/// A non-bootable source plus boot.eltorito: a host-file image (hidden) and a union file image (visible, shared)
TEST(ComposeBoot_Test, BootLayerAddsElTorito)
{
    Case c;
    c.root.File("floppy.img", Pattern(1474560, 7));
    c.root.File("cd/BOOT/loader.bin", Pattern(2048, 9));
    c.root.File("cd/readme.txt", "r");
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult r = c.Build(kBootableCd, volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;

    Iso9660Reader reader;
    ASSERT_TRUE(reader.Open(*volume));
    std::vector<IsoBootEntry> entries;
    std::string error;
    ASSERT_TRUE(reader.ReadBootCatalog(entries, &error)) << error;
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].emulation, 2);
    EXPECT_EQ(entries[0].platform, 0);
    EXPECT_EQ(c.Blocks(*volume, entries[0].loadBlock, 1474560), Bytes(Pattern(1474560, 7)));
    EXPECT_EQ(entries[1].platform, 0xEF);
    EXPECT_EQ(entries[1].emulation, 0);
    EXPECT_EQ(entries[1].loadSegment, 0x07C0);
    EXPECT_EQ(entries[1].sectorCount, 4);
    IsoDirEntry loader;
    ASSERT_TRUE(reader.Stat("/BOOT/loader.bin", loader));
    EXPECT_EQ(entries[1].loadBlock, loader.Block()) << "the visible file and the boot image share one extent";
}

/// A bootable ISO at the bottom: its catalog carried with relocated blocks, the images' bytes unchanged
TEST(ComposeBoot_Test, ElToritoCarriedFromBottomLayer)
{
    Case c;
    c.root.File("floppy.img", Pattern(1474560, 7));
    c.root.File("cd/BOOT/loader.bin", Pattern(2048, 9));
    std::unique_ptr<IBlockDevice> source;
    CompositeInfo info;
    ASSERT_TRUE(c.Build(kBootableCd, source, info).Ok());
    c.SaveIso(*source, "bootable.iso");
    c.root.File("more/added.txt", std::string(5000, 'a'));  // moves every block after it

    std::unique_ptr<IBlockDevice> volume;
    const MediaResult r = c.Build("version: 1\ntarget: {kind: optical}\nlayers:\n  - {name: base, source: {iso: bootable.iso}}\n"
                                  "  - {name: more, source: {folder: more}, mount: /AAA}\n",
                                  volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Has(r.report, "El Torito carried from layer 'base', 2 entries"));
    Iso9660Reader reader;
    ASSERT_TRUE(reader.Open(*volume));
    std::vector<IsoBootEntry> entries;
    ASSERT_TRUE(reader.ReadBootCatalog(entries));
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(c.Blocks(*volume, entries[0].loadBlock, 1474560), Bytes(Pattern(1474560, 7))) << "the hidden floppy image";
    IsoDirEntry loader;
    ASSERT_TRUE(reader.Stat("/BOOT/loader.bin", loader));
    EXPECT_EQ(entries[1].loadBlock, loader.Block());
    EXPECT_EQ(c.Blocks(*volume, entries[1].loadBlock, 2048), Bytes(Pattern(2048, 9)));
}

/// A bootable ISO above the bottom is not carried, and the report says so; a damaged catalog is reported
TEST(ComposeBoot_Test, BootableIsoNotBottomAndBadChecksumReported)
{
    Case c;
    c.root.File("floppy.img", Pattern(1474560, 7));
    c.root.File("cd/BOOT/loader.bin", Pattern(2048, 9));
    std::unique_ptr<IBlockDevice> source;
    CompositeInfo info;
    ASSERT_TRUE(c.Build(kBootableCd, source, info).Ok());
    c.SaveIso(*source, "bootable.iso");
    c.root.File("plain/a.txt", "a");

    std::unique_ptr<IBlockDevice> volume;
    MediaResult r = c.Build("version: 1\ntarget: {kind: optical}\nlayers:\n  - {name: plain, source: {folder: plain}}\n"
                            "  - {name: boot, source: {iso: bootable.iso}}\n",
                            volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Has(r.report, "boot data in layer 'boot' ignored"));
    Iso9660Reader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(reader.BootCatalogBlock(), 0u);

    // Break the catalog's checksum in the file
    Iso9660Reader original;
    ASSERT_TRUE(original.Open(*source));
    {
        std::fstream f(c.root.Path() / "bootable.iso", std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(static_cast<std::streamoff>(original.BootCatalogBlock()) * 2048 + 28);
        f.put('\x5A');
    }
    r = c.Build("version: 1\ntarget: {kind: optical}\nlayers: [{name: base, source: {iso: bootable.iso}}]\n", volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Has(r.report, "catalog is not carried"));
    Iso9660Reader broken;
    ASSERT_TRUE(broken.Open(*volume));
    EXPECT_EQ(broken.BootCatalogBlock(), 0u);
}

/// FAT rebuild from the DSS boot floppy: its volume boot code (to byte 510) and the loader in reserved sectors 1-3 are carried
TEST(ComposeBoot_Test, CarriesBaseBootCode)
{
    const std::filesystem::path floppy = TestPathHelper::FindProjectRoot() / "testdata/machines/sprinter/dss_1_62_92.img";
    if (!std::filesystem::exists(floppy))
        GTEST_SKIP() << "no DSS floppy";
    Case c;
    std::filesystem::copy_file(floppy, c.root.Path() / "dss.img");
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult r = c.Build("version: 1\ntarget: {build: rebuild, fs: fat16, free: 1MiB}\nlayers: [{name: dss, source: {image: dss.img}}]\n",
                                  volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_TRUE(Has(r.report, "boot: carried from layer 'dss': volume boot code, 3 reserved sector(s)"))
        << "the loader is LBA 1-3; 4-9 are zeros and not carried";

    auto base = HddImageFormats::OpenBlock((c.root.Path() / "dss.img").string(), "raw", RawImage::Access::ReadOnly);
    ASSERT_NE(base, nullptr);
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(reader.ReservedSectors(), 4u);
    const uint64_t start = reader.VolumeStart();
    uint8_t ours[512];
    uint8_t theirs[512];
    for (uint32_t lba = 1; lba < 4; lba++)
    {
        ASSERT_TRUE(volume->ReadSector(start + lba, ours));
        ASSERT_TRUE(base->ReadSector(lba, theirs));
        EXPECT_EQ(std::memcmp(ours, theirs, 512), 0) << "reserved sector " << lba;
    }
    ASSERT_TRUE(volume->ReadSector(start, ours));
    ASSERT_TRUE(base->ReadSector(0, theirs));
    EXPECT_EQ(std::memcmp(ours + 62, theirs + 62, 448), 0) << "the code area after the BPB";
    EXPECT_EQ(ours[13], reader.SectorsPerCluster()) << "the BPB is the builder's";
    std::vector<uint8_t> system;
    EXPECT_TRUE(reader.ReadFile("/SYSTEM.DOS", system));
}

/// The boot section's MBR code and volume code go into a rebuilt volume; the BPB and partition table stay the builder's
TEST(ComposeBoot_Test, BootLayerMbrAndVolumeCode)
{
    Case c;
    c.root.File("files/a.txt", "a");
    c.root.File("mbr.bin", Pattern(446, 3));
    c.root.File("vbr.bin", Pattern(100, 5));
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult r = c.Build("version: 1\ntarget: {free: 1MiB}\nlayers: [{source: {folder: files}}]\n"
                                  "boot: {mbrCode: {host: mbr.bin}, volumeCode: {host: vbr.bin}}\n",
                                  volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    uint8_t s[512];
    ASSERT_TRUE(volume->ReadSector(0, s));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 446), Pattern(446, 3));
    EXPECT_EQ(s[446 + 4], 0x04) << "the partition table is the builder's";
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    ASSERT_TRUE(volume->ReadSector(reader.VolumeStart(), s));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s + 62), 100), Pattern(100, 5));
    std::vector<uint8_t> data;
    EXPECT_TRUE(reader.ReadFile("/a.txt", data));
}

/// A reserved-sector file of three sectors fills sectors 1-3; the volume moves to make room
TEST(ComposeBoot_Test, BootLayerReservedSectors)
{
    Case c;
    c.root.File("files/a.txt", "a");
    c.root.File("loader.bin", Pattern(1536, 11));
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    const MediaResult r = c.Build("version: 1\ntarget: {free: 1MiB}\nlayers: [{source: {folder: files}}]\n"
                                  "boot: {reserved: [{lba: 1, file: {host: loader.bin}}]}\n",
                                  volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    EXPECT_EQ(reader.ReservedSectors(), 4u);
    uint8_t s[512];
    for (uint32_t k = 0; k < 3; k++)
    {
        ASSERT_TRUE(volume->ReadSector(reader.VolumeStart() + 1 + k, s));
        EXPECT_EQ(std::string(reinterpret_cast<char*>(s), 512), Pattern(1536, 11).substr(k * 512, 512)) << "sector " << 1 + k;
    }
    std::vector<uint8_t> data;
    EXPECT_TRUE(reader.ReadFile("/a.txt", data));
}

/// Explicit boot code larger than its area fails; on FAT32, sector 1 (FSInfo) cannot be taken
TEST(ComposeBoot_Test, BootCodeTooLargeFails)
{
    Case c;
    c.root.File("files/a.txt", "a");
    c.root.File("vbr.bin", Pattern(500, 5));
    c.root.File("one.bin", Pattern(512, 1));
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    MediaResult r = c.Build("version: 1\nlayers: [{source: {folder: files}}]\nboot: {volumeCode: {host: vbr.bin}}\n", volume, info);
    EXPECT_EQ(r.error, MediaError::DoesNotFit);
    EXPECT_NE(r.message.find("boot sector holds"), std::string::npos) << r.message;
    r = c.Build("version: 1\ntarget: {fs: fat32}\nlayers: [{source: {folder: files}}]\nboot: {reserved: [{lba: 1, file: {host: one.bin}}]}\n",
                volume, info);
    EXPECT_EQ(r.error, MediaError::DoesNotFit);
    EXPECT_NE(r.message.find("FSInfo"), std::string::npos) << r.message;
}

/// A graft patches the base's own boot sectors from the boot section; a reserved sector beyond the base's falls back
TEST(ComposeBoot_Test, GraftPatchesBootSection)
{
    Case c;
    ScratchFolder content("compose-boot-graft");
    content.File("a.txt", "a");
    const FatSourceImage image = FolderToFatDisk(content.Path(), FatType::Fat16, CodePage::Cp866, true);
    ASSERT_TRUE(image.ok()) << image.error;
    ASSERT_TRUE(SaveSparse(image, c.root.Path() / "base.img"));
    c.root.File("vbr.bin", Pattern(100, 5));
    c.root.File("one.bin", Pattern(512, 1));
    std::unique_ptr<IBlockDevice> volume;
    CompositeInfo info;
    MediaResult r = c.Build("version: 1\nlayers: [{source: {image: base.img}}]\nboot: {volumeCode: {host: vbr.bin}}\n", volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(info.build, "graft");
    FatVolumeReader reader;
    ASSERT_TRUE(reader.Open(*volume));
    uint8_t s[512];
    ASSERT_TRUE(volume->ReadSector(reader.VolumeStart(), s));
    EXPECT_EQ(std::string(reinterpret_cast<char*>(s + 62), 100), Pattern(100, 5));

    r = c.Build("version: 1\nlayers: [{source: {image: base.img}}]\nboot: {reserved: [{lba: 5, file: {host: one.bin}}]}\n", volume, info);
    ASSERT_TRUE(r.Ok()) << r.message;
    EXPECT_EQ(info.build, "rebuild");
    EXPECT_TRUE(Has(r.report, "past the base's 1 reserved sectors"));
}
