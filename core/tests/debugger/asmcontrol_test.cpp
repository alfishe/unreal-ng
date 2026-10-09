#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "_helpers/testpathhelper.h"
#include "common/base64.h"
#include "debugger/asm/asmcontrol.h"
#include "debugger/asm/diskfiles.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/labels/symbolcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/fdd.h"

/// The assembler-source verbs every surface calls (unreal-asm tdd §7): sources from host files, images, uploads and
/// the disk in a drive; outputs to host files and to the disk
namespace
{
std::filesystem::path AsmTestData()
{
    return TestPathHelper::FindProjectRoot() / "core" / "src" / "3rdparty" / "unreal-asm" / "testdata";
}

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

AsmReply AsmRun(const std::string& verb, std::map<std::string, std::string> options = {}, EmulatorContext* context = nullptr)
{
    return AsmControl(context).Execute({verb, std::move(options)});
}
}  // namespace

TEST(AsmControl_Test, VerbsOptionsFormatsDialects)
{
    AsmReply reply = AsmRun("assemble");
    EXPECT_EQ(reply.HttpStatus(), 400);
    EXPECT_NE(reply.message.find("verbs: formats, dialects, files, detect, decode, encode, convert"), std::string::npos);
    EXPECT_NE(AsmRun("decode", {{"path", "x"}, {"to", "y"}}).message.find("has no option 'to'"), std::string::npos);

    reply = AsmRun("formats");
    ASSERT_TRUE(reply.Ok());
    bool alasm = false;
    for (const StateNode& f : reply.body.find("formats")->items)
        alasm = alasm || (f.find("id")->s == "alasm" && f.find("versions")->items.size() >= 8);
    EXPECT_TRUE(alasm);

    reply = AsmRun("dialects");
    ASSERT_TRUE(reply.Ok());
    const auto has = [](const StateNode& list, const char* id) {
        return std::any_of(list.items.begin(), list.items.end(), [&](const StateNode& n) { return n.s == id; });
    };
    EXPECT_TRUE(has(*reply.body.find("read"), "alasm"));
    EXPECT_TRUE(has(*reply.body.find("write"), "sjasmplus"));
}

TEST(AsmControl_Test, DecodeAHobetaFileAsItsCodecReadsIt)
{
    const std::string path = (AsmTestData() / "alasm" / "128KDRV.$H").string();
    AsmReply reply = AsmRun("detect", {{"path", path}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("format")->s, "alasm");

    reply = AsmRun("decode", {{"path", path}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("format")->s, "alasm");
    EXPECT_EQ(reply.body.find("text")->s, ReadText(AsmTestData() / "alasm" / "128KDRV.txt"));

    // The same bytes uploaded
    std::ifstream in(path, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    reply = AsmRun("decode", {{"data", base64::Encode(bytes)}, {"name", "128KDRV.$H"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("text")->s, ReadText(AsmTestData() / "alasm" / "128KDRV.txt"));

    EXPECT_EQ(AsmRun("decode", {{"path", path}, {"codec", "nope"}}).HttpStatus(), 404);
    EXPECT_EQ(AsmRun("decode", {{"path", (AsmTestData() / "no-such-file").string()}}).HttpStatus(), 404);
    EXPECT_EQ(AsmRun("decode").HttpStatus(), 400);
}

TEST(AsmControl_Test, EncodeAndDecodeComeBackToTheText)
{
    const std::string text = ReadText(AsmTestData() / "alasm" / "128KDRV.txt");
    AsmReply encoded = AsmRun("encode", {{"text", text}, {"codec", "alasm"}, {"version", "5.07"}});
    ASSERT_TRUE(encoded.Ok()) << encoded.message;
    EXPECT_GT(encoded.body.find("bytes")->i, 0);
    const AsmReply decoded = AsmRun("decode", {{"data", encoded.body.find("data")->s}, {"name", "x"}, {"codec", "alasm"}, {"version", "5.07"}});
    ASSERT_TRUE(decoded.Ok()) << decoded.message;
    EXPECT_EQ(decoded.body.find("text")->s, text);

    EXPECT_EQ(AsmRun("encode", {{"text", "x"}}).HttpStatus(), 400);
    EXPECT_EQ(AsmRun("encode", {{"text", "x"}, {"codec", "alasm"}, {"lineend", "lfcr"}}).HttpStatus(), 400);
}

TEST(AsmControl_Test, ConvertGivesTheTargetDialect)
{
    const std::filesystem::path dir = AsmTestData() / "dialects" / "alasm-sjasmplus";
    const AsmReply reply = AsmRun("convert", {{"path", (dir / "constructs.alasm.txt").string()}, {"codec", "text"}, {"from", "alasm"}, {"to", "sjasmplus"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("text")->s, ReadText(dir / "constructs.sjasmplus.asm"));
    EXPECT_EQ(AsmRun("convert", {{"path", (dir / "constructs.alasm.txt").string()}, {"to", "cobol"}}).HttpStatus(), 404);
}

TEST(AsmControl_Machine_Test, SourcesGoToAndComeFromTheDiskInADrive)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("asm-disk", "PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    ASSERT_TRUE(emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd"), 0));
    EmulatorContext* context = emulator->GetContext();

    // ALASM text written to the disk as a source ALASM lists (type H), then read back from it
    const std::string text = ReadText(AsmTestData() / "alasm" / "128KDRV.txt");
    AsmReply reply = AsmRun("encode", {{"text", text}, {"codec", "alasm"}, {"output", "disk:A/128KDRV.H"}}, context);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    reply = AsmRun("files", {}, context);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    bool listed = false;
    for (const StateNode& f : reply.body.find("files")->items)
        listed = listed || (f.find("name")->s == "128KDRV" && f.find("type")->s == "H" && f.find("format")->kind == StateNode::Kind::String &&
                            f.find("format")->s == "alasm");
    EXPECT_TRUE(listed);
    reply = AsmRun("decode", {{"path", "disk:A/128KDRV.H"}}, context);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("text")->s, text);

    // Written again: the older file is deleted, the new one read
    ASSERT_TRUE(AsmRun("encode", {{"text", "TWO NOP\n"}, {"codec", "alasm"}, {"output", "disk:A/128KDRV.H"}}, context).Ok());
    reply = AsmRun("decode", {{"path", "disk:A/128KDRV.H"}}, context);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_NE(reply.body.find("text")->s.find("NOP"), std::string::npos);

    // A symbol file on the disk imports as well
    ASSERT_TRUE(AsmRun("encode", {{"text", "8000 FROM_DISK\n"}, {"codec", "text"}, {"output", "disk:A/labels.s"}}, context).Ok());
    const SymbolReply symbols =
        SymbolControl(context).Execute({"import", {{"path", "disk:A/labels.s"}, {"format", "simple-sym"}}});
    ASSERT_TRUE(symbols.Ok()) << symbols.message;
    EXPECT_EQ(context->pDebugManager->GetLabelManager()->GetLabelByName("FROM_DISK")->address, 0x8000);

    // Write protection refuses
    context->coreState.diskDrives[0]->setWriteProtect(true);
    EXPECT_EQ(AsmRun("encode", {{"text", "NOP"}, {"codec", "alasm"}, {"output", "disk:A/X.H"}}, context).HttpStatus(), 409);
    EXPECT_EQ(AsmRun("decode", {{"path", "disk:A/MISSING.H"}}, context).HttpStatus(), 404);

    manager->RemoveEmulator(emulator->GetUUID());
}

TEST(AsmControl_Test, DiskFileReferences)
{
    DiskFileRef ref;
    ASSERT_TRUE(ParseDiskFileRef("disk:b/GAME.H", ref));
    EXPECT_EQ(ref.drive, 1);
    EXPECT_EQ(ref.name, "GAME");
    EXPECT_EQ(ref.type, 'H');
    ASSERT_TRUE(ParseDiskFileRef("disk:A/boot", ref));
    EXPECT_EQ(ref.type, 0);
    EXPECT_FALSE(ParseDiskFileRef("disk:E/GAME.H", ref));
    EXPECT_FALSE(ParseDiskFileRef("disk:A/NAMELONGER9.H", ref));
    EXPECT_FALSE(ParseDiskFileRef("/host/file.H", ref));
}
