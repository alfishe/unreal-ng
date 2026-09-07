#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/tzxtapebuilder.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "emulator/io/tape/tapetypes.h"

/// End-to-end TZX mounting through the public surfaces (design §5.6, top of
/// the test pyramid): Emulator::LoadTape validates and mounts the file,
/// Tape::EnsureImageLoaded routes the bytes through the registry (LoaderTZX
/// self-registers), and the block catalog is queryable before any playback.
class TzxLoad_Integration_Test : public ::testing::Test
{
protected:
    /// Program header + payload pair, mirroring the classic ROM flash-screen
    /// load order — the catalog must classify and pair them
    static std::vector<uint8_t> MakeProgramTzx()
    {
        std::vector<uint8_t> program = { 0x0C, 0x00, 0x00, 0x01, 0xEA, 'T', 'Z', 'X', 0x0D };

        std::vector<uint8_t> header;
        header.push_back(0x00);  // flag: header
        header.push_back(0x00);  // type: Program
        for (const char c : std::string("tzxtest   "))  // 7 + 3 spaces = the 10-byte name field
        {
            header.push_back(static_cast<uint8_t>(c));
        }
        header.push_back(static_cast<uint8_t>(program.size() & 0xFF));
        header.push_back(static_cast<uint8_t>(program.size() >> 8));
        header.push_back(0x00);  // autostart line lo
        header.push_back(0x80);  // autostart line hi ($8000 = no autorun)
        header.push_back(0x00);  // vars start lo
        header.push_back(0x00);  // vars start hi
        uint8_t headerChecksum = 0;
        for (const uint8_t byte : header)
        {
            headerChecksum ^= byte;
        }
        header.push_back(headerChecksum);

        std::vector<uint8_t> data;
        data.push_back(0xFF);  // flag: data
        uint8_t dataChecksum = 0xFF;
        for (const uint8_t byte : program)
        {
            data.push_back(byte);
            dataChecksum ^= byte;
        }
        data.push_back(dataChecksum);

        TzxTapeBuilder builder;
        builder.AddText("tzx integration");
        builder.AddStandardBlock(1000, header);
        builder.AddStandardBlock(1000, data);
        return builder.Bytes();
    }

    static std::string WriteScratchFile(const std::string& name, const std::vector<uint8_t>& bytes)
    {
        const std::string path = TestPathHelper::GetTestScratchPath("tzxload-int/" + name);
        EXPECT_TRUE(TzxTapeBuilder::WriteToFile(bytes, path));
        return path;
    }
};

TEST_F(TzxLoad_Integration_Test, LoadTapeMountsTzxCatalog)
{
    const std::string path = WriteScratchFile("program.tzx", MakeProgramTzx());

    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    EXPECT_TRUE(emulator->LoadTape(path));

    EmulatorContext* context = emulator->GetContext();
    Tape* tape = context->pTape;
    ASSERT_NE(tape, nullptr);

    EXPECT_TRUE(tape->EnsureImageLoaded());
    const std::vector<TapeBlockDescriptor>& catalog = tape->GetBlockCatalog();
    ASSERT_EQ(catalog.size(), 2u);
    EXPECT_EQ(catalog[0].kind, TapeBlockKindEnum::Header);
    EXPECT_EQ(catalog[0].name, "tzxtest");
    EXPECT_EQ(catalog[0].pairedDataIndex, 1u);
    EXPECT_EQ(catalog[1].kind, TapeBlockKindEnum::Data);
    EXPECT_EQ(catalog[1].pairedHeaderIndex, 0u);
    EXPECT_TRUE(catalog[0].checksumValid);
    EXPECT_TRUE(catalog[1].checksumValid);

    EXPECT_FALSE(tape->IsPlaying());
    EXPECT_EQ(tape->GetConsumptionCursor(), 0u);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST_F(TzxLoad_Integration_Test, LoadTapeRejectsMissingFile)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    EXPECT_FALSE(emulator->LoadTape(TestPathHelper::GetTestScratchPath("tzxload-int/missing.tzx")));

    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST_F(TzxLoad_Integration_Test, LoadTapeRejectsWrongExtension)
{
    // Valid TZX bytes behind an unsupported extension: rejected before any
    // parsing — the extension gate is LoadTape's, the registry's content
    // probe only kicks in once the file reaches EnsureImageLoaded
    const std::string path = WriteScratchFile("renamed.bin", MakeProgramTzx());

    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    EXPECT_FALSE(emulator->LoadTape(path));

    EmulatorTestHelper::CleanupEmulator(emulator);
}
