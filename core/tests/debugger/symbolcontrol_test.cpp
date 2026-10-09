#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>

#include "_helpers/testpathhelper.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/labels/symbolcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "unrealasm/symbols/symbol.h"

/// The symbol verbs every surface calls (symbols/tdd.md section 8): options, refusals, the reply's fields
class SymbolControl_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = std::make_unique<EmulatorContext>(LoggerLevel::LogError);
        _labels = std::make_unique<LabelManager>(_context.get());
    }
    void TearDown() override
    {
        for (const std::string& path : _files)
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }

    std::string Write(const std::string& name, const std::string& text)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        std::ofstream(path, std::ios::binary) << text;
        _files.push_back(path);
        return path;
    }
    std::string Scratch(const std::string& name)
    {
        _files.push_back(TestPathHelper::GetUniqueTestScratchPath(name));
        return _files.back();
    }
    SymbolReply Run(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        return SymbolControl(_labels.get()).Execute({verb, std::move(options)});
    }

    std::unique_ptr<EmulatorContext> _context;
    std::unique_ptr<LabelManager> _labels;
    std::vector<std::string> _files;
};

TEST_F(SymbolControl_Test, UnknownVerbsAndOptionsAreRefusedTheSameWay)
{
    SymbolReply reply = Run("load");
    EXPECT_EQ(reply.HttpStatus(), 400);
    EXPECT_NE(reply.message.find("verbs: formats, detect, sets, import, export, set, drop, scan, import-live"), std::string::npos)
        << reply.message;

    reply = Run("import", {{"path", "x.sym"}, {"page", "ram3"}});
    EXPECT_EQ(reply.HttpStatus(), 400);
    EXPECT_NE(reply.message.find("has no option 'page'"), std::string::npos) << reply.message;
    EXPECT_EQ(reply.ToValue().find("error")->s, "Bad Request");

    EXPECT_EQ(Run("import").message, "'import' needs a path or data (the file as base64), not both");
    EXPECT_EQ(Run("import", {{"path", "x.sym"}, {"policy", "merge"}}).HttpStatus(), 400);
    EXPECT_EQ(Run("import", {{"path", "x.sym"}, {"space", "ram"}}).HttpStatus(), 400);
    EXPECT_EQ(Run("export", {{"path", "x.sym"}, {"pages", "keep"}}).HttpStatus(), 400);
    EXPECT_EQ(Run("set", {{"id", "user"}}).message, "'set' needs enabled or priority");
}

TEST_F(SymbolControl_Test, FormatsListEveryCodec)
{
    const SymbolReply reply = Run("formats");
    ASSERT_TRUE(reply.Ok());
    const StateNode& formats = *reply.body.find("formats");
    EXPECT_GE(formats.items.size(), 10u);
    bool native = false;
    for (const StateNode& f : formats.items)
        native = native || (f.find("id")->s == "native" && f.find("family")->s == "native");
    EXPECT_TRUE(native);
}

TEST_F(SymbolControl_Test, ImportReportsTheFormatTheSetAndTheCounts)
{
    const std::string path = Write("game.sym", "8000 START\n8010 LOOP\n");
    SymbolReply reply = Run("import", {{"path", path}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("format")->s, "simple-sym");
    EXPECT_EQ(reply.body.find("set")->s, "file:" + path);
    EXPECT_EQ(reply.body.find("records")->i, 2);
    EXPECT_EQ(reply.body.find("added")->i, 2);
    EXPECT_EQ(reply.body.find("labels")->i, 2);

    // Into a named set at a page, with a base: merged by the policy
    const std::string paged = Write("paged.sym", "0100 P_START\n0110 P_LOOP\n");
    reply = Run("import", {{"path", paged}, {"set", "game"}, {"space", "ram3"}, {"base", "0x10"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("set")->s, "game");
    const auto set = _labels->GetSymbolSets();
    ASSERT_EQ(set.size(), 2u);
    EXPECT_EQ(set[1].symbols.size(), 2u);
    EXPECT_EQ(set[1].symbols[0].location.space.Format(), "ram3");
    EXPECT_EQ(set[1].symbols[0].location.offset, 0x110u);

    // Again with policy fail: the same names at the same places are no conflict
    reply = Run("import", {{"path", paged}, {"set", "game"}, {"space", "ram3"}, {"base", "0x10"}, {"policy", "fail"}});
    EXPECT_TRUE(reply.Ok()) << reply.message;
    // A name moved: policy fail stops, 409
    const std::string moved = Write("moved.sym", "0200 P_START\n");
    reply = Run("import", {{"path", moved}, {"set", "game"}, {"space", "ram3"}, {"policy", "fail"}});
    EXPECT_EQ(reply.HttpStatus(), 409) << reply.message;
    EXPECT_FALSE(reply.body.find("conflicts")->items.empty());

    // A file that is not there
    reply = Run("import", {{"path", Scratch("missing.sym")}});
    EXPECT_EQ(reply.HttpStatus(), 400);
    EXPECT_NE(reply.message.find("Cannot read"), std::string::npos);
}

TEST_F(SymbolControl_Test, SetsCanBeSwitchedReprioritizedAndDropped)
{
    const std::string a = Write("a.sym", "8000 SAME\n");
    const std::string b = Write("b.sym", "9000 SAME\n");
    ASSERT_TRUE(Run("import", {{"path", a}}).Ok());
    ASSERT_TRUE(Run("import", {{"path", b}}).Ok());
    EXPECT_EQ(_labels->GetLabelByName("SAME")->address, 0x9000);

    SymbolReply reply = Run("sets");
    ASSERT_TRUE(reply.Ok());
    ASSERT_EQ(reply.body.find("sets")->items.size(), 2u);
    EXPECT_EQ(reply.body.find("sets")->items[0].find("origin")->find("kind")->s, "file");

    reply = Run("set", {{"id", "file:" + b}, {"enabled", "off"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_FALSE(reply.body.find("set")->find("enabled")->b);
    EXPECT_EQ(_labels->GetLabelByName("SAME")->address, 0x8000);

    reply = Run("set", {{"id", "file:" + b}, {"enabled", "on"}, {"priority", "1"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(_labels->GetLabelByName("SAME")->address, 0x8000);

    EXPECT_EQ(Run("set", {{"id", "nope"}, {"enabled", "on"}}).HttpStatus(), 404);
    EXPECT_TRUE(Run("drop", {{"id", "file:" + a}}).Ok());
    EXPECT_EQ(_labels->GetLabelByName("SAME")->address, 0x9000);
    EXPECT_EQ(Run("drop", {{"id", "file:" + a}}).HttpStatus(), 404);
}

TEST_F(SymbolControl_Test, ExportWritesTheLabelsOrTheNamedSets)
{
    const std::string a = Write("ea.sym", "8000 FROM_A\n");
    const std::string b = Write("eb.sym", "9000 FROM_B\n");
    ASSERT_TRUE(Run("import", {{"path", a}}).Ok());
    ASSERT_TRUE(Run("import", {{"path", b}}).Ok());
    ASSERT_TRUE(_labels->AddLabel("ODD", 0x1234, 0, 0x5678, ""));

    // By the extension: every label as it shows
    const std::string all = Scratch("all.sym");
    SymbolReply reply = Run("export", {{"path", all}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("format")->s, "simple-sym");
    EXPECT_EQ(reply.body.find("written")->i, 3);

    // One set, a named format
    const std::string one = Scratch("one.txt");
    reply = Run("export", {{"path", one}, {"format", "unreal-map"}, {"sets", "file:" + b}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("written")->i, 1);
    std::ifstream in(one, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("FROM_B"), std::string::npos);
    EXPECT_EQ(text.find("FROM_A"), std::string::npos);

    // The native file keeps the sets, read back as they were
    const std::string native = Scratch("store.usym.json");
    reply = Run("export", {{"path", native}, {"format", "native"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    LabelManager other(_context.get());
    ASSERT_TRUE(SymbolControl(&other).Execute({"import", {{"path", native}}}).Ok());
    EXPECT_EQ(other.GetSymbolSets().size(), 3u);
    EXPECT_EQ(other.GetLabelByName("ODD")->bankOffset, 0x5678);
    EXPECT_EQ(other.GetLabelByName("ODD")->type, "");

    EXPECT_EQ(Run("export", {{"path", Scratch("x.sym")}, {"sets", "nope"}}).HttpStatus(), 404);
    EXPECT_EQ(Run("export", {{"path", Scratch("x.unknownext")}}).HttpStatus(), 400);
}

TEST_F(SymbolControl_Test, AnUploadedFileImportsFromItsBase64)
{
    // "8000 START\n9000 LOOP\n" as base64, with the name that picks the format
    SymbolReply reply = Run("import", {{"data", "ODAwMCBTVEFSVAo5MDAwIExPT1AK"}, {"name", "game.sym"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("format")->s, "simple-sym");
    EXPECT_EQ(reply.body.find("set")->s, "upload:game.sym");
    EXPECT_EQ(_labels->GetLabelByName("LOOP")->address, 0x9000);
    EXPECT_EQ(_labels->GetSymbolSets()[0].origin.kind, "upload");

    EXPECT_EQ(Run("import", {{"data", "!!"}, {"name", "x.sym"}}).message, "'data' is no base64");
    EXPECT_EQ(Run("import", {{"data", "AA=="}, {"path", "x.sym"}}).HttpStatus(), 400);
}

TEST_F(SymbolControl_Test, DetectRanksTheCodecs)
{
    const std::string path = Write("detect.sym", "8000 START\n");
    const SymbolReply reply = Run("detect", {{"path", path}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("format")->s, "simple-sym");
    EXPECT_FALSE(reply.body.find("candidates")->items.empty());
}

TEST_F(SymbolControl_Test, AliasesShowAsLabels)
{
    // A merge gives a second name for a place as an alias: both names resolve, the first shows at the address
    const std::string first = Write("al1.sym", "8000 MAIN\n");
    const std::string second = Write("al2.sym", "8000 ENTRY\n");
    ASSERT_TRUE(Run("import", {{"path", first}, {"set", "m"}}).Ok());
    const SymbolReply reply = Run("import", {{"path", second}, {"set", "m"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("aliased")->i, 1);
    ASSERT_NE(_labels->GetLabelByName("ENTRY"), nullptr);
    EXPECT_EQ(_labels->GetLabelByName("ENTRY")->address, 0x8000);
    EXPECT_EQ(_labels->GetLabelByZ80Address(0x8000)->name, "MAIN");
    ASSERT_TRUE(_labels->RemoveLabel("ENTRY"));
    EXPECT_EQ(_labels->GetLabelByName("ENTRY"), nullptr);
    EXPECT_NE(_labels->GetLabelByName("MAIN"), nullptr);
}

TEST(SymbolControl_Machine_Test, AnAssemblersLabelTableInRamImports)
{
    // ALASM 5.09's table, dumped from RAM page 3 after it assembled (unreal-asm testdata/symbols/live)
    const std::filesystem::path live = TestPathHelper::FindProjectRoot() / "core" / "src" / "3rdparty" / "unreal-asm" / "testdata" /
                                       "symbols" / "live";
    std::ifstream in(live / "alasm509-lta-ram3.bin", std::ios::binary);
    const std::vector<uint8_t> dump((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_EQ(dump.size(), 0x4000u);
    size_t expected = 0;
    {
        std::ifstream list(live / "alasm509-lta.expected.txt");
        std::string line;
        while (std::getline(list, line))
        {
            std::istringstream fields(line);
            std::string name, value, mark;
            fields >> name >> value >> mark;
            expected += !name.empty() && mark.empty();
        }
    }

    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("symbols-live", "PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    std::copy(dump.begin(), dump.end(), context->pMemory->RAMPageAddress(3));
    context->pDebugManager->GetLabelManager()->ClearAllLabels();

    SymbolReply reply = SymbolControl(context).Execute({"scan", {}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const auto& candidates = reply.body.find("candidates")->items;
    ASSERT_FALSE(candidates.empty());
    EXPECT_EQ(candidates[0].find("scanner")->s, "alasm-table");
    EXPECT_EQ(candidates[0].find("page")->i, 3);
    EXPECT_EQ(candidates[0].find("offset")->i, 0x3D8A);

    reply = SymbolControl(context).Execute({"import-live", {}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("set")->s, "live:alasm-table@ram3:#3D8A");
    EXPECT_EQ(static_cast<size_t>(reply.body.find("records")->i), expected);
    EXPECT_EQ(static_cast<size_t>(reply.body.find("labels")->i), expected);

    EXPECT_EQ(SymbolControl(context).Execute({"import-live", {{"scanner", "xas-table"}}}).HttpStatus(), 404);
    manager->RemoveEmulator(emulator->GetUUID());
}
