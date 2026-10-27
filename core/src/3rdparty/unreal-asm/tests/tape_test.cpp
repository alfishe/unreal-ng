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

TEST(Tape_Test, Gens1SaveKeepsBlanksAndEndsWithItsMarker)
{
    // GENS1 (Devpac 1983) loaded from its tape, three lines typed, P1,999,TEST (unreal-ng, 2026-10-20): P rewrites
    // line 2 of the BASIC loader into SAVE "TEST" CODE text,length and runs it. CODE at the text's address, the
    // length counting the #00 #00 after the last line
    const std::vector<TrdosFile> files = Files("tape/typed-gens1-P-TEST.tap");
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].TrimmedName(), "TEST");
    EXPECT_EQ(files[0].type, 'C');
    EXPECT_EQ(files[0].start, 0x7800);
    EXPECT_EQ(Detected(files[0]), "gens");
    const DecodeResult decoded = CodecRegistry::Builtin().Find("gens")->Decode(files[0].data, {});
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded.document.subversion, "1");
    EXPECT_EQ(decoded.document.Text(), "START LD A,1\n  RET\n; a comment  with  blanks");
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

TEST(Tape_Test, GensIncludeFileStopsBeforeStaleBytes)
{
    // GENS4 V4.0's T in a fresh session (unreal-ng, 2026-10-20): no #00 #00 after a block's lines, the buffer's old
    // bytes (its relocation table) follow; GENS4's own *F reads them as a line (*ERROR* 07). Nine lines in two blocks
    const std::vector<uint8_t> nine = ReadTestData("gens/typed-gens4-INC.bin");
    EXPECT_EQ(Files("tape/typed-gens4-T-INC-stale.tap").at(0).data, nine);

    // a CR in the stale bytes would end a "line": after block 1's last line (numbered above the next block's first)
    // and after the last block's (beyond the header's count)
    std::vector<uint8_t> tape = ReadTestData("tape/typed-gens4-T-INC-stale.tap");
    std::vector<TapeBlock> blocks;
    std::string error;
    ASSERT_TRUE(ReadTapeBlocks(tape, blocks, error)) << error;
    ASSERT_EQ(blocks.size(), 3u);
    size_t at = 0;
    for (size_t k = 0; k < blocks.size(); ++k)
    {
        const size_t length = tape[at] | tape[at + 1] << 8;
        if (k > 0)
        {
            tape[at + 2 + length - 3] = 0x0D;   // the next to last data byte
            uint8_t check = 0;
            for (size_t n = 0; n + 1 < length; ++n)
                check ^= tape[at + 2 + n];
            tape[at + 2 + length - 1] = check;
        }
        at += 2 + length;
    }
    std::vector<TrdosFile> files;
    ASSERT_TRUE(ReadTape(tape, files, error)) << error;
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].data, nine);
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
