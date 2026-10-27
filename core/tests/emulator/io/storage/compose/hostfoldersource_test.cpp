// HostFolderSource: a scanned host folder as one layer (multi-source DT-1, tdd.md §2)

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/compose/hostfoldersource.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"

namespace
{
    std::vector<std::string> Names(const FileTree& tree, const std::string& path)
    {
        std::vector<std::string> out;
        for (uint32_t child : tree.Node(tree.Find(path)).children)
            out.push_back(tree.Node(child).name);
        return out;
    }
}  // namespace

TEST(HostFolderSource_Test, WholeFolderWithHiddenDotNames)
{
    ScratchFolder folder("hfs-whole");
    folder.File("a.bin", "aaa");
    folder.File(".hidden", "h");
    folder.File("sub/b.bin", "bb");
    folder.File("empty.bin", "");
    FolderSnapshot snapshot;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, snapshot));

    SourcePool pool;
    FileTree tree;
    std::string error;
    ASSERT_TRUE(HostFolderSource::Enumerate(snapshot, {}, pool, tree, nullptr, &error)) << error;
    EXPECT_EQ(Names(tree, "/"), (std::vector<std::string>{"sub", ".hidden", "a.bin", "empty.bin"}));
    const TreeNode& hidden = tree.Node(tree.Find("/.hidden"));
    EXPECT_EQ(hidden.attributes & 0x02, 0x02);
    const TreeNode& a = tree.Node(tree.Find("/a.bin"));
    EXPECT_EQ(a.data.storage, FileData::Storage::HostFile);
    EXPECT_EQ(a.data.bytes, 3u);
    EXPECT_EQ(tree.Node(tree.Find("/empty.bin")).data.storage, FileData::Storage::Zero) << "nothing to read";
    EXPECT_EQ(pool.HostFileCount(), 3u) << "only files with bytes are registered";
}

TEST(HostFolderSource_Test, FromSubfolderAndIncludeFilesOnly)
{
    ScratchFolder folder("hfs-from");
    folder.File("out/game.com", "c");
    folder.File("out/notes.txt", "n");
    folder.File("out/lib/x.com", "x");
    folder.File("out/lib/y.o", "y");
    folder.File("other.com", "o");
    FolderSnapshot snapshot;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, snapshot));

    HostFolderSourceOptions options;
    options.from = "/out";
    options.include = {"*.COM"};
    SourcePool pool;
    FileTree tree;
    std::vector<std::string> report;
    ASSERT_TRUE(HostFolderSource::Enumerate(snapshot, options, pool, tree, &report, nullptr));
    EXPECT_EQ(Names(tree, "/"), (std::vector<std::string>{"lib", "game.com"})) << "patterns match case-insensitively";
    EXPECT_EQ(Names(tree, "/lib"), (std::vector<std::string>{"x.com"})) << "folders are walked, not filtered";
    EXPECT_EQ(report.size(), 2u) << "notes.txt and lib/y.o";
}

TEST(HostFolderSource_Test, MissingFromFails)
{
    ScratchFolder folder("hfs-missing");
    folder.File("file.bin", "f");
    FolderSnapshot snapshot;
    ASSERT_TRUE(FolderSnapshot::Scan(folder.Path(), {}, snapshot));
    HostFolderSourceOptions options;
    SourcePool pool;
    FileTree tree;
    std::string error;
    options.from = "/nope";
    EXPECT_FALSE(HostFolderSource::Enumerate(snapshot, options, pool, tree, nullptr, &error));
    EXPECT_NE(error.find("/nope"), std::string::npos);
    options.from = "/file.bin";
    EXPECT_FALSE(HostFolderSource::Enumerate(snapshot, options, pool, tree, nullptr, &error)) << "a file is not a folder";
}
