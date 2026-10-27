// SourcePool: host files and devices shared by every layer (multi-source tdd.md §2)

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/compose/sourcepool.h"

TEST(SourcePool_Test, OpenHostFilesBounded)
{
    ScratchFolder folder("sourcepool-bounded");
    SourcePool pool;
    std::vector<uint32_t> files;
    for (int i = 0; i < 50; i++)
    {
        const auto path = folder.File("f" + std::to_string(i) + ".bin", std::string(16, static_cast<char>('A' + i % 26)));
        files.push_back(pool.AddHostFile(path, 16));
    }
    uint8_t buffer[16];
    for (int round = 0; round < 3; round++)
    {
        for (int i = 0; i < 50; i++)
        {
            ASSERT_EQ(pool.ReadHost(files[i], 0, buffer, 16), 16u);
            EXPECT_EQ(buffer[0], 'A' + i % 26);
            EXPECT_LE(pool.OpenStreams(), SourcePool::kMaxOpenFiles);
        }
    }
    EXPECT_TRUE(pool.Warnings().empty());
}

TEST(SourcePool_Test, MissingHostFileReadsShortAndWarnsOnce)
{
    ScratchFolder folder("sourcepool-missing");
    SourcePool pool;
    const uint32_t file = pool.AddHostFile(folder.Path() / "gone.bin", 100);
    uint8_t buffer[100];
    EXPECT_EQ(pool.ReadHost(file, 0, buffer, 100), 0u);
    EXPECT_EQ(pool.ReadHost(file, 50, buffer, 50), 0u);
    EXPECT_EQ(pool.Warnings().size(), 1u);
}
