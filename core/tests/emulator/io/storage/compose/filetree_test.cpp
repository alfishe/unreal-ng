// FileTree: the flat tree of sources and unions (multi-source tdd.md §2, §3)

#include <gtest/gtest.h>

#include "emulator/io/storage/compose/filetree.h"

namespace
{
    uint32_t Dir(FileTree& tree, uint32_t parent, const char* name)
    {
        TreeNode node;
        node.name = name;
        node.isDirectory = true;
        return tree.Add(parent, std::move(node));
    }
}  // namespace

TEST(FileTree_Test, FindAndPathOf)
{
    FileTree tree;
    const uint32_t a = Dir(tree, FileTree::kRoot, "a");
    const uint32_t b = Dir(tree, a, "b");
    TreeNode leaf;
    leaf.name = "c.txt";
    leaf.data.bytes = 3;
    const uint32_t file = tree.Add(b, std::move(leaf));

    EXPECT_EQ(tree.Find("/a/b/c.txt"), file);
    EXPECT_EQ(tree.Find("a//b/./c.txt"), file);
    EXPECT_EQ(tree.Find("/a/b/missing"), FileTree::kNone);
    EXPECT_EQ(tree.Find("/a/b/c.txt/deeper"), FileTree::kNone);
    EXPECT_EQ(tree.Find("/"), FileTree::kRoot);
    EXPECT_EQ(tree.PathOf(file), "/a/b/c.txt");
    EXPECT_EQ(tree.PathOf(FileTree::kRoot), "/");
}

TEST(FileTree_Test, DetachAndCopySubtree)
{
    FileTree source;
    const uint32_t games = Dir(source, FileTree::kRoot, "GAMES");
    TreeNode file;
    file.name = "elite.trd";
    file.data.bytes = 10;
    source.Add(games, std::move(file));

    FileTree target;
    const uint32_t copy = target.CopySubtree(source, games, FileTree::kRoot, 3);
    EXPECT_EQ(target.Node(copy).layer, 3u);
    const uint32_t elite = target.Find("/GAMES/elite.trd");
    ASSERT_NE(elite, FileTree::kNone);
    EXPECT_EQ(target.Node(elite).layer, 3u);
    EXPECT_EQ(target.Node(elite).data.bytes, 10u);

    target.Detach(copy);
    EXPECT_EQ(target.Find("/GAMES"), FileTree::kNone) << "detached nodes are unreachable";
    EXPECT_TRUE(target.Node(FileTree::kRoot).children.empty());
}
