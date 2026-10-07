// ComposeDescriptor: the *.ucompose.yaml / .json descriptor (multi-source tdd.md §1)

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "_helpers/scratchfolder.h"
#include "emulator/media/composedescriptor.h"

namespace
{
    const char* kFull = R"(
version: 1
target:
  kind: block
  fs: fat32
  build: rebuild
  size: 2GiB
  free: 256MiB
  label: GAMES
  codepage: cp1251
  partition: none
  fixedTime: 1767268800
layers:
  - name: base
    source: {folder: sd}
  - name: games
    source: {folder: ../zx/games}
    mount: /GAMES//
    from: sub/
    include: ["*.trd", "*.scl"]
    exclude: "*.bak"
    conflict: keep-lower
    opaque: [/GAMES/OLD]
    whiteout: [/GAMES/broken.trd]
    writable: true
    onDelete: trash
writes:
  access: readonly
  save: flat
  upper: games
)";

    const char* kFullJson = R"({
  "version": 1,
  "target": {"kind": "block", "fs": "fat32", "build": "rebuild", "size": "2GiB", "free": "256MiB",
             "label": "GAMES", "codepage": "cp1251", "partition": "none", "fixedTime": 1767268800},
  "layers": [
    {"name": "base", "source": {"folder": "sd"}},
    {"name": "games", "source": {"folder": "../zx/games"}, "mount": "/GAMES", "from": "/sub",
     "include": ["*.trd", "*.scl"], "exclude": ["*.bak"], "conflict": "keep-lower",
     "opaque": ["/GAMES/OLD"], "whiteout": ["/GAMES/broken.trd"], "writable": true, "onDelete": "trash"}
  ],
  "writes": {"access": "readonly", "save": "flat", "upper": "games"}
})";

    const std::filesystem::path kBase = std::filesystem::path("/work/media");
}  // namespace

TEST(ComposeDescriptor_Test, ParsesFullExample)
{
    const ComposeDescriptor d = ComposeDescriptor::Parse(kFull, kBase, "games.ucompose.yaml");
    ASSERT_TRUE(d.Ok()) << d.error;
    EXPECT_TRUE(d.report.empty()) << d.report.front();
    EXPECT_EQ(d.version, 1);
    EXPECT_EQ(d.target.kind, MediaKind::Block);
    EXPECT_EQ(d.target.fs, ComposeTarget::Fs::Fat32);
    EXPECT_EQ(d.target.build, ComposeTarget::Build::Rebuild);
    EXPECT_EQ(*d.target.size, 2ull * 1024 * 1024 * 1024);
    EXPECT_EQ(*d.target.free, 256ull * 1024 * 1024);
    EXPECT_EQ(*d.target.label, "GAMES");
    EXPECT_EQ(*d.target.codePage, CodePage::Cp1251);
    EXPECT_FALSE(*d.target.mbr);
    EXPECT_EQ(*d.target.fixedTimeUtc, 1767268800);
    ASSERT_EQ(d.layers.size(), 2u);
    const ComposeLayer& games = d.layers[1];
    EXPECT_EQ(games.name, "games");
    EXPECT_EQ(games.source.kind, ComposeSource::Kind::Folder);
    EXPECT_EQ(games.source.path, (kBase / "../zx/games").lexically_normal()) << "relative to the descriptor";
    EXPECT_EQ(games.mount, "/GAMES") << "target paths are normalized";
    EXPECT_EQ(games.from, "/sub");
    EXPECT_EQ(games.include, (std::vector<std::string>{"*.trd", "*.scl"}));
    EXPECT_EQ(games.exclude, (std::vector<std::string>{"*.bak"})) << "a single value is a list of one";
    EXPECT_EQ(games.conflict, ConflictPolicy::KeepLower);
    EXPECT_TRUE(games.writable);
    EXPECT_EQ(games.onDelete, DeletePolicy::Trash);
    EXPECT_EQ(d.writes.access, AccessMode::ReadOnly);
    EXPECT_EQ(d.writes.save, "flat");
    EXPECT_EQ(d.writes.upper, "games");
    EXPECT_EQ(d.writes.delta, (kBase / "games.ucompose.yaml.delta").lexically_normal());
}

TEST(ComposeDescriptor_Test, YamlAndJsonNormalizeTheSame)
{
    const ComposeDescriptor yaml = ComposeDescriptor::Parse(kFull, kBase, "games.ucompose.yaml");
    const ComposeDescriptor json = ComposeDescriptor::Parse(kFullJson, kBase, "games.ucompose.json");
    ASSERT_TRUE(yaml.Ok()) << yaml.error;
    ASSERT_TRUE(json.Ok()) << json.error;
    EXPECT_EQ(yaml.Normalized(), json.Normalized());
}

TEST(ComposeDescriptor_Test, NormalizedIgnoresFormatting)
{
    const char* a = "version: 1\nlayers:\n  - {source: {folder: x}, mount: /A/B}\ntarget: {size: 2GiB}\n";
    const char* b = "target:\n  size: 2147483648\nversion: 1\nlayers:\n  - mount: /A//B/\n    source:\n      folder: ./x\n";
    const ComposeDescriptor da = ComposeDescriptor::Parse(a, kBase, "a.ucompose.yaml");
    const ComposeDescriptor db = ComposeDescriptor::Parse(b, kBase, "b.ucompose.yaml");
    ASSERT_TRUE(da.Ok()) << da.error;
    ASSERT_TRUE(db.Ok()) << db.error;
    EXPECT_EQ(da.Normalized(), db.Normalized());
    const ComposeDescriptor dc = ComposeDescriptor::Parse("version: 1\nlayers: [{source: {folder: y}}]\n", kBase, "c");
    EXPECT_NE(da.Normalized(), dc.Normalized()) << "another source is another medium";
}

TEST(ComposeDescriptor_Test, TildeExpandsToHome)
{
    const char* home = std::getenv("HOME");
    if (!home)
        GTEST_SKIP() << "no HOME";
    const ComposeDescriptor d = ComposeDescriptor::Parse("version: 1\nlayers: [{source: {folder: ~/zx}}]\n", kBase, "t");
    ASSERT_TRUE(d.Ok()) << d.error;
    EXPECT_EQ(d.layers[0].source.path, (std::filesystem::path(home) / "zx").lexically_normal());
}

TEST(ComposeDescriptor_Test, UnknownKeysAndBadValuesReportedNotFatal)
{
    const ComposeDescriptor d = ComposeDescriptor::Parse(
        "version: 1\ncolour: blue\ntarget: {fs: ntfs, size: lots}\nlayers: [{source: {folder: x}, conflict: maybe, wat: 1}]\n",
        kBase, "d.ucompose.yaml");
    ASSERT_TRUE(d.Ok()) << d.error;
    EXPECT_EQ(d.report.size(), 5u);
    EXPECT_EQ(d.target.fs, ComposeTarget::Fs::Auto) << "a bad value keeps the default";
}

TEST(ComposeDescriptor_Test, FatalErrors)
{
    EXPECT_FALSE(ComposeDescriptor::Parse("layers: [{source: {folder: x}}]\n", kBase, "a").Ok()) << "no version";
    EXPECT_FALSE(ComposeDescriptor::Parse("version: 1\n", kBase, "b").Ok()) << "no layers";
    EXPECT_FALSE(ComposeDescriptor::Parse("version: 1\nlayers: [{mount: /x}]\n", kBase, "c").Ok()) << "no source";
    EXPECT_FALSE(ComposeDescriptor::Parse("version: 1\nlayers: [{source: {folder: x}}]\npartitions: []\n", kBase, "d").Ok())
        << "layers and partitions are exclusive";
    EXPECT_FALSE(ComposeDescriptor::Parse("version: 1\nlayers: [{source: {folder: x}\n", kBase, "e").Ok())
        << "malformed YAML is an error, never an abort";
    EXPECT_FALSE(ComposeDescriptor::Parse("[1, 2]", kBase, "f").Ok());
    EXPECT_FALSE(ComposeDescriptor::Parse("", kBase, "g").Ok());
}

TEST(ComposeDescriptor_Test, LoadResolvesAgainstTheFilesFolder)
{
    ScratchFolder folder("compose-load");
    const auto file = folder.File("cards/sd.ucompose.yaml", "version: 1\nlayers: [{source: {folder: ../games}}]\n");
    const ComposeDescriptor d = ComposeDescriptor::Load(file);
    ASSERT_TRUE(d.Ok()) << d.error;
    EXPECT_EQ(d.layers[0].source.path, (folder.Path() / "games").lexically_normal());
    EXPECT_FALSE(ComposeDescriptor::Load(folder.Path() / "missing.ucompose.yaml").Ok());
}

TEST(ComposeDescriptor_Test, ParseSize)
{
    uint64_t b = 0;
    EXPECT_TRUE(ComposeDescriptor::ParseSize("512", b));
    EXPECT_EQ(b, 512u);
    EXPECT_TRUE(ComposeDescriptor::ParseSize("64KiB", b));
    EXPECT_EQ(b, 65536u);
    EXPECT_TRUE(ComposeDescriptor::ParseSize("2GiB", b));
    EXPECT_EQ(b, 2147483648u);
    EXPECT_TRUE(ComposeDescriptor::ParseSize("1.44MB", b));
    EXPECT_EQ(b, 1440000u);
    EXPECT_TRUE(ComposeDescriptor::ParseSize("0x100", b));
    EXPECT_FALSE(ComposeDescriptor::ParseSize("lots", b));
    EXPECT_FALSE(ComposeDescriptor::ParseSize("5 parsecs", b));
    EXPECT_FALSE(ComposeDescriptor::ParseSize("-1", b));
}

TEST(ComposeDescriptor_Test, IsDescriptorName)
{
    EXPECT_TRUE(ComposeDescriptor::IsDescriptorName("sd.ucompose.yaml"));
    EXPECT_TRUE(ComposeDescriptor::IsDescriptorName("SD.UCOMPOSE.JSON"));
    EXPECT_TRUE(ComposeDescriptor::IsDescriptorName("a.ucompose.yml"));
    EXPECT_FALSE(ComposeDescriptor::IsDescriptorName("ucompose.yaml"));
    EXPECT_FALSE(ComposeDescriptor::IsDescriptorName("sd.yaml"));
    EXPECT_FALSE(ComposeDescriptor::IsDescriptorName(".unreal-media.yaml"));
}
