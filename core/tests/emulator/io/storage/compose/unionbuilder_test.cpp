// UnionBuilder: layers merged into one tree under the target's name rules
// (multi-source decision tree DT-2, tdd.md §3)

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "emulator/io/storage/compose/filetree.h"
#include "emulator/io/storage/compose/unionbuilder.h"

namespace
{
    /// Add "/a/b/c.txt" (creating the folders); size marks which layer it came from
    uint32_t AddFile(FileTree& tree, const std::string& path, uint64_t size)
    {
        uint32_t at = FileTree::kRoot;
        size_t pos = 1;
        while (true)
        {
            const size_t slash = path.find('/', pos);
            const std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
            if (slash == std::string::npos)
            {
                TreeNode file;
                file.name = part;
                file.data.bytes = size;
                return tree.Add(at, std::move(file));
            }
            uint32_t next = tree.Child(at, part);
            if (next == FileTree::kNone)
            {
                TreeNode dir;
                dir.name = part;
                dir.isDirectory = true;
                next = tree.Add(at, std::move(dir));
            }
            at = next;
            pos = slash + 1;
        }
    }

    uint32_t AddDir(FileTree& tree, const std::string& path)
    {
        const uint32_t marker = AddFile(tree, path + "/x", 0);
        const uint32_t dir = tree.Node(marker).parent;
        tree.Detach(marker);
        return dir;
    }

    /// "name(size)" listing of a directory, in order; "name/" for folders
    std::vector<std::string> List(const FileTree& tree, const std::string& path)
    {
        std::vector<std::string> out;
        const uint32_t dir = tree.Find(path);
        EXPECT_NE(dir, FileTree::kNone) << path;
        if (dir == FileTree::kNone)
            return out;
        for (uint32_t child : tree.Node(dir).children)
        {
            const TreeNode& n = tree.Node(child);
            out.push_back(n.isDirectory ? n.name + "/" : n.name + "(" + std::to_string(n.data.bytes) + ")");
        }
        return out;
    }

    bool Merge(const std::vector<UnionLayer>& layers, FileTree& out, std::vector<std::string>* report = nullptr,
               std::string* error = nullptr)
    {
        return UnionBuilder::Merge(layers, UnionBuilder::FatKey, out, report, error);
    }

    UnionLayer Layer(const FileTree& tree, const char* name, const char* mount = "/")
    {
        UnionLayer layer;
        layer.tree = &tree;
        layer.name = name;
        layer.mount = mount;
        return layer;
    }

    using Names = std::vector<std::string>;
}  // namespace

TEST(UnionBuilder_Test, UpperShadowsLowerFile)
{
    FileTree low, up, out;
    AddFile(low, "/game.trd", 1);
    AddFile(low, "/keep.txt", 1);
    AddFile(up, "/game.trd", 2);
    std::vector<std::string> report;
    ASSERT_TRUE(Merge({Layer(low, "low"), Layer(up, "up")}, out, &report));
    EXPECT_EQ(List(out, "/"), (Names{"game.trd(2)", "keep.txt(1)"}));
    ASSERT_EQ(report.size(), 1u);
    EXPECT_NE(report[0].find("/game.trd: layer 'up' shadows layer 'low'"), std::string::npos) << report[0];
}

TEST(UnionBuilder_Test, DirectoriesMerge)
{
    FileTree low, up, out;
    AddFile(low, "/GAMES/a.trd", 1);
    AddFile(low, "/GAMES/sub/x.bin", 1);
    AddFile(up, "/GAMES/b.trd", 2);
    AddFile(up, "/GAMES/sub/y.bin", 2);
    ASSERT_TRUE(Merge({Layer(low, "low"), Layer(up, "up")}, out));
    EXPECT_EQ(List(out, "/GAMES"), (Names{"sub/", "a.trd(1)", "b.trd(2)"})) << "folders first, then files by name";
    EXPECT_EQ(List(out, "/GAMES/sub"), (Names{"x.bin(1)", "y.bin(2)"}));
}

TEST(UnionBuilder_Test, OpaqueHidesLower)
{
    FileTree low, up, out;
    AddFile(low, "/BIN/old.com", 1);
    AddFile(up, "/BIN/new.com", 2);
    UnionLayer upper = Layer(up, "up");
    upper.opaque = {"/BIN"};
    std::vector<std::string> report;
    ASSERT_TRUE(Merge({Layer(low, "low"), upper}, out, &report));
    EXPECT_EQ(List(out, "/BIN"), (Names{"new.com(2)"}));
    EXPECT_EQ(report.size(), 1u);
}

TEST(UnionBuilder_Test, WhiteoutRemoves)
{
    FileTree low, up, out;
    AddFile(low, "/GAMES/broken.trd", 1);
    AddFile(low, "/GAMES/good.trd", 1);
    UnionLayer upper = Layer(up, "up");
    upper.whiteout = {"/GAMES/broken.trd", "/not/there"};
    std::vector<std::string> report;
    ASSERT_TRUE(Merge({Layer(low, "low"), upper}, out, &report));
    EXPECT_EQ(List(out, "/GAMES"), (Names{"good.trd(1)"}));
    ASSERT_EQ(report.size(), 1u) << "a whiteout of nothing is not reported";
    EXPECT_NE(report[0].find("whiteout"), std::string::npos);
}

TEST(UnionBuilder_Test, CaseInsensitiveCollisionFat)
{
    FileTree low, up, out;
    AddFile(low, "/Readme.txt", 1);
    AddFile(up, "/README.TXT", 2);
    ASSERT_TRUE(Merge({Layer(low, "low"), Layer(up, "up")}, out));
    EXPECT_EQ(List(out, "/"), (Names{"README.TXT(2)"})) << "one FAT entry: the upper layer's";
}

TEST(UnionBuilder_Test, CyrillicCaseFolds)
{
    FileTree low, up, out;
    AddFile(low, "/Игра.trd", 1);
    AddFile(up, "/ИГРА.TRD", 2);
    ASSERT_TRUE(Merge({Layer(low, "low"), Layer(up, "up")}, out));
    EXPECT_EQ(List(out, "/"), (Names{"ИГРА.TRD(2)"}));
}

TEST(UnionBuilder_Test, SameLayerKeepsCaseVariants)
{
    FileTree only, out;
    AddFile(only, "/a.txt", 1);
    AddFile(only, "/A.TXT", 2);
    ASSERT_TRUE(Merge({Layer(only, "only")}, out));
    EXPECT_EQ(List(out, "/"), (Names{"A.TXT(2)", "a.txt(1)"})) << "one layer never merges with itself";
}

TEST(UnionBuilder_Test, FileReplacesDirectoryAndBack)
{
    FileTree low, up, out;
    AddFile(low, "/x/inner.bin", 1);
    AddFile(low, "/y", 1);
    AddFile(up, "/x", 2);
    AddFile(up, "/y/inner.bin", 2);
    ASSERT_TRUE(Merge({Layer(low, "low"), Layer(up, "up")}, out));
    EXPECT_EQ(List(out, "/"), (Names{"y/", "x(2)"}));
    EXPECT_EQ(List(out, "/y"), (Names{"inner.bin(2)"}));
}

TEST(UnionBuilder_Test, ConflictErrorFails)
{
    FileTree low, up, out;
    AddFile(low, "/a.bin", 1);
    AddFile(up, "/A.BIN", 2);
    UnionLayer upper = Layer(up, "up");
    upper.conflict = ConflictPolicy::Error;
    std::string error;
    EXPECT_FALSE(Merge({Layer(low, "low"), upper}, out, nullptr, &error));
    EXPECT_NE(error.find("/A.BIN"), std::string::npos) << error;
    EXPECT_NE(error.find("conflict: error"), std::string::npos) << error;
}

TEST(UnionBuilder_Test, KeepLower)
{
    FileTree low, up, out;
    AddFile(low, "/a.bin", 1);
    AddFile(up, "/a.bin", 2);
    AddFile(up, "/b.bin", 2);
    UnionLayer upper = Layer(up, "up");
    upper.conflict = ConflictPolicy::KeepLower;
    std::vector<std::string> report;
    ASSERT_TRUE(Merge({Layer(low, "low"), upper}, out, &report));
    EXPECT_EQ(List(out, "/"), (Names{"a.bin(1)", "b.bin(2)"}));
    ASSERT_EQ(report.size(), 1u);
    EXPECT_NE(report[0].find("keep-lower"), std::string::npos);
}

TEST(UnionBuilder_Test, MountCreatesIntermediateDirs)
{
    FileTree base, games, out;
    AddFile(base, "/boot.bin", 1);
    AddFile(games, "/elite.trd", 2);
    ASSERT_TRUE(Merge({Layer(base, "base"), Layer(games, "games", "/A/B//C/")}, out));
    EXPECT_EQ(List(out, "/"), (Names{"A/", "boot.bin(1)"}));
    EXPECT_EQ(List(out, "/A/B/C"), (Names{"elite.trd(2)"}));
    EXPECT_EQ(out.Node(out.Find("/A")).layer, 1u) << "the mount path belongs to the layer that needed it";
}

TEST(UnionBuilder_Test, MountOverAFileReplacesIt)
{
    FileTree base, games, out;
    AddFile(base, "/GAMES", 1);
    AddFile(games, "/elite.trd", 2);
    std::vector<std::string> report;
    ASSERT_TRUE(Merge({Layer(base, "base"), Layer(games, "games", "/GAMES")}, out, &report));
    EXPECT_EQ(List(out, "/GAMES"), (Names{"elite.trd(2)"}));
    EXPECT_EQ(report.size(), 1u);
}

TEST(UnionBuilder_Test, DeterministicOrder)
{
    FileTree a, b;
    for (const char* name : {"zeta", "alpha", "Beta", "mid", "_under", "Ünï"})
        AddFile(a, std::string("/") + name, 1);
    for (const char* name : {"Ünï", "mid", "alpha", "_under", "Beta", "zeta"})
        AddFile(b, std::string("/") + name, 1);
    AddDir(a, "/dir");
    AddDir(b, "/dir");
    FileTree outA, outB;
    ASSERT_TRUE(Merge({Layer(a, "a")}, outA));
    ASSERT_TRUE(Merge({Layer(b, "b")}, outB));
    EXPECT_EQ(List(outA, "/"), List(outB, "/"));
    EXPECT_EQ(List(outA, "/").front(), "dir/");
}

TEST(UnionBuilder_Test, ExtentsFollowTheCopiedNodes)
{
    FileTree low, out;
    low.Extents() = {{100, 2, 0}, {200, 3, 2}};
    const uint32_t file = AddFile(low, "/frag.bin", 5 * 512);
    low.Node(file).data.storage = FileData::Storage::DeviceExtents;
    low.Node(file).data.firstExtent = 0;
    low.Node(file).data.extentCount = 2;
    FileTree other;
    other.Extents() = {{1, 1, 0}};
    const uint32_t first = AddFile(other, "/first.bin", 512);
    other.Node(first).data.storage = FileData::Storage::DeviceExtents;
    other.Node(first).data.extentCount = 1;

    ASSERT_TRUE(Merge({Layer(other, "other"), Layer(low, "low")}, out));
    const TreeNode& copied = out.Node(out.Find("/frag.bin"));
    ASSERT_EQ(copied.data.extentCount, 2u);
    EXPECT_EQ(out.Extents()[copied.data.firstExtent].sourceLba, 100u);
    EXPECT_EQ(out.Extents()[copied.data.firstExtent + 1].sourceLba, 200u);
}
