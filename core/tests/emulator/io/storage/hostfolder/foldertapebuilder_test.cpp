// A host folder built into a tape (integration-tape.md §4): headers from Hobeta
// files and plain files, ready tapes appended, the manifest's order, names and
// pause, the per-file limit, the C90 capacity. And ACC-8: the real ROM loads
// from a folder with LOAD "" and LOAD "" CODE

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/scratchfolder.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/hostfolder/foldertapebuilder.h"
#include "emulator/io/tape/tapecatalog.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "loaders/disk/loader_hobeta.h"

namespace
{
    /// 10 POKE 60000,42 (tokenized, 26 bytes)
    const std::vector<uint8_t> kPokeProgram = {0x00, 0x0A, 0x16, 0x00, 0xF4, '6', '0', '0', '0', '0', 0x0E, 0x00, 0x00,
                                               0x60, 0xEA, 0x00, ',', '4', '2', 0x0E, 0x00, 0x00, 0x2A, 0x00, 0x00, 0x0D};

    /// The program as a Hobeta file with TR-DOS's autorun line 10 after it
    std::string BootHobeta()
    {
        LoaderHobeta::Header header;
        std::memcpy(header.name, "boot    ", 8);
        header.type = 'B';
        header.start = static_cast<uint16_t>(kPokeProgram.size());
        header.length = static_cast<uint16_t>(kPokeProgram.size());
        header.sectors = 1;
        std::string file(LoaderHobeta::HEADER_SIZE + 256, '\0');
        LoaderHobeta::serializeHeader(header, reinterpret_cast<uint8_t*>(file.data()));
        std::memcpy(file.data() + LoaderHobeta::HEADER_SIZE, kPokeProgram.data(), kPokeProgram.size());
        const uint8_t autorun[] = {0x80, 0xAA, 10, 0};
        std::memcpy(file.data() + LoaderHobeta::HEADER_SIZE + kPokeProgram.size(), autorun, sizeof(autorun));
        return file;
    }

    /// A .tap with one headerless 3-byte data block
    std::string SmallTap()
    {
        const uint8_t block[] = {0xFF, 1, 2, 3, 0xFF ^ 1 ^ 2 ^ 3};
        std::string tap = {static_cast<char>(sizeof(block)), 0};
        tap.append(reinterpret_cast<const char*>(block), sizeof(block));
        return tap;
    }

    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::vector<TapeBlockDescriptor> Catalog(const TapeImage& image) { return TapeCatalogParser::Build(image); }

    bool Reported(const MediaResult& result, const std::string& what)
    {
        for (const std::string& line : result.report)
        {
            if (line.find(what) != std::string::npos)
                return true;
        }
        return false;
    }
}  // namespace

/// Order, names, types, start addresses, autorun, ready tapes, pauses
TEST(FolderTapeBuilder_Test, FilesBecomeHeaderAndDataBlocks)
{
    ScratchFolder folder("foldertape-layout");
    folder.File("boot.$B", BootHobeta());
    folder.File("intro.scr", std::string(6912, 'S'));
    folder.File("zz-game.bin", std::string(100, 'G'));
    folder.File("ready.tap", SmallTap());
    folder.File("Длинное имя файла.cod", std::string(10, 'X'));
    folder.File(".unreal-media.yaml", "order: [boot.$B, intro.scr, ready.tap]\ntape: {pause: 500}\n");

    std::unique_ptr<TapeImage> image;
    const MediaResult built = FolderTapeBuilder::Build(folder.Path(), image);
    ASSERT_TRUE(built.Ok()) << built.message;
    ASSERT_NE(image, nullptr);
    const auto catalog = Catalog(*image);
    ASSERT_EQ(catalog.size(), 9u);

    // boot.$B: the Hobeta name, a program with its autorun line
    EXPECT_TRUE(catalog[0].headerValid);
    EXPECT_EQ(catalog[0].name, "boot");
    EXPECT_EQ(catalog[0].headerType, TAP_BLOCK_PROGRAM);
    EXPECT_EQ(catalog[0].declaredLength, kPokeProgram.size());
    EXPECT_EQ(catalog[0].param1, 10) << "autorun line";
    EXPECT_EQ(catalog[1].rawSize, kPokeProgram.size() + 2);

    // intro.scr: a 6912-byte screen loads at 16384
    EXPECT_EQ(catalog[2].name, "intro");
    EXPECT_EQ(catalog[2].headerType, TAP_BLOCK_CODE);
    EXPECT_EQ(catalog[2].param1, 16384);
    EXPECT_EQ(catalog[2].declaredLength, 6912);

    // ready.tap: its one block, unchanged
    EXPECT_EQ(catalog[4].kind, TapeBlockKindEnum::Data);
    EXPECT_EQ(catalog[4].rawSize, 5u);
    EXPECT_TRUE(catalog[4].checksumValid);

    // the rest byte-wise sorted: zz-game.bin, then the Cyrillic name (UTF-8 sorts last)
    EXPECT_EQ(catalog[5].name, "zz-game");
    EXPECT_EQ(catalog[5].param1, 32768);
    EXPECT_EQ(catalog[7].name, "_______ __") << "10 characters, non-ASCII become '_'";

    for (const TapeBlockDescriptor& block : catalog)
        EXPECT_EQ(block.timing.pauseMs, 500) << "the manifest's pause, block " << block.index;
}

TEST(FolderTapeBuilder_Test, ManifestOverridesNameTypeStartAndLine)
{
    ScratchFolder folder("foldertape-manifest");
    folder.File("loader.bin", std::string(kPokeProgram.begin(), kPokeProgram.end()));
    folder.File("code.bin", std::string(16, 'C'));
    folder.File(".unreal-media.yaml",
                "order: [loader.bin]\nfiles:\n  loader.bin: {name: MyLoader, type: B, line: 10}\n"
                "  code.bin: {name: engine, start: 24576}\n");

    std::unique_ptr<TapeImage> image;
    ASSERT_TRUE(FolderTapeBuilder::Build(folder.Path(), image).Ok());
    const auto catalog = Catalog(*image);
    ASSERT_EQ(catalog.size(), 4u);
    EXPECT_EQ(catalog[0].name, "MyLoader");
    EXPECT_EQ(catalog[0].headerType, TAP_BLOCK_PROGRAM);
    EXPECT_EQ(catalog[0].param1, 10);
    EXPECT_EQ(catalog[0].param2, kPokeProgram.size()) << "no variables";
    EXPECT_EQ(catalog[2].name, "engine");
    EXPECT_EQ(catalog[2].param1, 24576);
    EXPECT_EQ(catalog[2].timing.pauseMs, FolderTapeBuilder::kDefaultPauseMs);
}

/// A file over one tape block is skipped; a file that would pass the end of
/// the cassette side is skipped, and placing goes on with the next file
TEST(FolderTapeBuilder_Test, LimitsSkipAndReport)
{
    ScratchFolder folder("foldertape-limits");
    folder.File("a-small.bin", std::string(10, 'a'));      // ~ 5 + 2 + 2 s
    folder.File("b-big.bin", std::string(4000, 'b'));      // ~ 25 s more
    folder.File("c-huge.bin", std::string(70000, 'c'));    // over a block
    folder.File("d-small.bin", std::string(10, 'd'));

    std::vector<uint8_t> tzx;
    const MediaResult built = FolderTapeBuilder::BuildTzx(folder.Path(), tzx, 25 * 1000);
    ASSERT_TRUE(built.Ok()) << built.message;
    EXPECT_TRUE(Reported(built, "c-huge.bin: skipped, larger than one tape block"));
    EXPECT_TRUE(Reported(built, "b-big.bin: skipped, does not fit on one side of a C90"));

    std::unique_ptr<TapeImage> image;
    ASSERT_TRUE(FolderTapeBuilder::Build(folder.Path(), image, 25 * 1000).Ok());
    const auto catalog = Catalog(*image);
    ASSERT_EQ(catalog.size(), 4u);
    EXPECT_EQ(catalog[0].name, "a-small");
    EXPECT_EQ(catalog[2].name, "d-small");

    ScratchFolder empty("foldertape-empty");
    EXPECT_EQ(FolderTapeBuilder::BuildTzx(empty.Path(), tzx).error, MediaError::DoesNotFit);
}

/// ACC-8: the real ROM loads a folder: LOAD "" runs the autorun program, then
/// LOAD "" CODE puts the next file at its start address.
/// Boots the ROM and loads through it: slower than 50 ms by nature
TEST(FolderTapeBuilder_Rom_Test, LoadFromAFolder)
{
    MessageCenter::DisposeDefaultMessageCenter();
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    emulator->EnableTurboMode();
    EmulatorContext* context = emulator->GetContext();
    Memory& memory = *context->pMemory;
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);

    ScratchFolder folder("foldertape-rom");
    folder.File("1-boot.$B", BootHobeta());
    folder.File("2-data.bin", std::string("\x11\x22\x33\x44", 4));
    std::string error;
    ASSERT_TRUE(emulator->LoadTape(Utf8(folder.Path()), &error)) << error;

    for (int i = 0; i < 100; i++)
        mainLoop->RunFrame();

    auto runUntil = [&](auto done) {
        for (int frame = 0; frame < 100 && !done(); frame++)
            mainLoop->RunFrame();
        return done();
    };

    memory.DirectWriteToZ80Memory(60000, 0);
    ASSERT_TRUE(BasicEncoder::runCommand(emulator, "LOAD \"\"").success);
    EXPECT_TRUE(runUntil([&] { return memory.DirectReadFromZ80Memory(60000) == 42; })) << "the program autoran";

    for (int i = 0; i < 10; i++)
        mainLoop->RunFrame();
    ASSERT_TRUE(BasicEncoder::runCommand(emulator, "LOAD \"\" CODE").success);
    EXPECT_TRUE(runUntil([&] { return memory.DirectReadFromZ80Memory(32768 + 3) == 0x44; }));
    EXPECT_EQ(memory.DirectReadFromZ80Memory(32768), 0x11);

    EmulatorTestHelper::CleanupEmulator(emulator);
    MessageCenter::DisposeDefaultMessageCenter();
}
