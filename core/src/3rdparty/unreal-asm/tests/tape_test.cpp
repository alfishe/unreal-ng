// Tape images (TAP, TZX) as containers of source files: files GENS4 saved with P and T in unreal-ng, and a ZXDB tape
// of ZEUS sources. The files come out with their header names and the bytes the existing source testdata holds.

#include <gtest/gtest.h>

#include "testdata.h"
#include "unrealasm/containers.h"
#include "unrealasm/registry.h"

using namespace unrealasm;
using namespace unrealasm::containers;
using unrealasm::testing::ReadTestData;

namespace
{
std::vector<TrdosFile> Files(const char* relative)
{
    std::vector<TrdosFile> files;
    std::string error;
    EXPECT_TRUE(ReadTape(ReadTestData(relative), files, error)) << relative << ": " << error;
    return files;
}

std::string Detected(const TrdosFile& f)
{
    const DetectResult d = CodecRegistry::Builtin().Detect(f.data, f.Hints());
    return d.chosen ? d.chosen->Info().id : std::string();
}
}  // namespace

TEST(Tape_Test, TapBlocksCarryFlagsAndChecksums)
{
    std::vector<TapeBlock> blocks;
    std::string error;
    ASSERT_TRUE(ReadTapeBlocks(ReadTestData("tape/typed-gens4-P-PROBE4.tap"), blocks, error)) << error;
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_EQ(blocks[0].flag, 0);
    EXPECT_EQ(blocks[0].data.size(), 17u);
    EXPECT_EQ(blocks[1].flag, 0xFF);
    EXPECT_TRUE(blocks[0].checksumOk);
    EXPECT_TRUE(blocks[1].checksumOk);
}

TEST(Tape_Test, GensSaveIsOneFileWithItsHeader)
{
    const std::vector<TrdosFile> files = Files("tape/typed-gens4-P-PROBE4.tap");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].TrimmedName(), "PROBE4");
    EXPECT_EQ(files[0].type, 'C');
    EXPECT_EQ(files[0].data, ReadTestData("gens/typed-gens4-P-PROBE4.bin"));
    EXPECT_EQ(Detected(files[0]), "gens");
}

TEST(Tape_Test, GensIncludeFileJoinsTheLinesOfItsBlocks)
{
    // GENS4's T: header type 4 (length = lines + 1), a 256-byte block of whole lines and stale bytes after the #00 #00
    // word; the lines are the ones its P save of the same text holds
    const std::vector<TrdosFile> files = Files("tape/typed-gens4-T-INCL4.tap");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].TrimmedName(), "INCL4");
    EXPECT_EQ(files[0].data, ReadTestData("gens/typed-gens4-P-PROBE4.bin"));
    EXPECT_EQ(Detected(files[0]), "gens");
}

TEST(Tape_Test, TzxStandardBlocksGiveTheZeusSource)
{
    // ZXDB's Zeus Routines (Theo Develegas): a BASIC loader and the ZEUS source saved as Bytes at 32768
    const std::vector<TrdosFile> files = Files("tape/ZeusGlitter.tzx");
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].type, 'B');
    EXPECT_EQ(files[1].type, 'C');
    EXPECT_EQ(files[1].start, 32768);
    EXPECT_EQ(files[1].data, ReadTestData("zeus/ZeusRoutines__ZeusGlitter.bin"));
    EXPECT_EQ(Detected(files[1]), "zeus");
}

TEST(Tape_Test, NotATapeIsRefused)
{
    std::vector<TapeBlock> blocks;
    std::string error;
    const std::vector<uint8_t> junk = {0x40, 0x00, 1, 2, 3};
    EXPECT_FALSE(ReadTapeBlocks(junk, blocks, error));
    EXPECT_FALSE(error.empty());
    std::vector<uint8_t> tzx = {'Z', 'X', 'T', 'a', 'p', 'e', '!', 0x1A, 1, 20, 0x99};
    EXPECT_FALSE(ReadTapeBlocks(tzx, blocks, error));
}
