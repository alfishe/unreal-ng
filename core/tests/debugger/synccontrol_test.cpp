#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "common/base64.h"
#include "debugger/asm/asmcontrol.h"
#include "debugger/debugmanager.h"
#include "debugger/labels/labelmanager.h"
#include "unrealasm/symbols/symbol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "debugger/asm/sync/asmsyncservice.h"

/// The asm-synchronizer's verbs (asm-synchronizer.md §8, phase Y0) on a machine whose RAM holds a dumped assembler
/// session (unreal-asm testdata/sync): the pages are written into Memory directly, no guest code runs
namespace
{
std::filesystem::path SyncData(const std::string& folder)
{
    return TestPathHelper::FindProjectRoot() / "core" / "src" / "3rdparty" / "unreal-asm" / "testdata" / "sync" / folder;
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// Pages 2 and 6 of a dump into the machine, page 6 at #C000 as it was
void LoadSession(EmulatorContext* context, const std::string& folder)
{
    Memory* memory = context->pMemory;
    for (const uint16_t page : {uint16_t(2), uint16_t(6)})
    {
        const std::vector<uint8_t> bytes = ReadBytes(SyncData(folder) / ("page" + std::to_string(page) + ".bin"));
        ASSERT_EQ(bytes.size(), 0x4000u);
        std::copy(bytes.begin(), bytes.end(), memory->RAMPageAddress(page));
    }
    memory->SetRAMPageToBank3(6);
}

AsmReply SyncRun(EmulatorContext* context, const std::string& verb, std::map<std::string, std::string> options = {})
{
    return AsmControl(context).Execute({verb, std::move(options)});
}

class SyncControl_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("asm-sync", "PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
};
}  // namespace

TEST_F(SyncControl_Test, AFreshMachineHasNoAssembler)
{
    AsmReply reply = SyncRun(_context, "sync-probe");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(reply.body.find("candidates")->items.empty());
    reply = SyncRun(_context, "sync-status");
    EXPECT_EQ(reply.HttpStatus(), 404);
    EXPECT_EQ(reply.body.find("state")->s, "none");
    EXPECT_EQ(SyncRun(nullptr, "sync-status").HttpStatus(), 400) << "no instance";
}

TEST_F(SyncControl_Test, AlasmInTheEditorGivesItsFileAndText)
{
    LoadSession(_context, "alasm509-typing");
    AsmReply reply = SyncRun(_context, "sync-status");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("assembler")->s, "alasm-5.09");
    EXPECT_EQ(reply.body.find("name")->s, "SNAKE");
    EXPECT_EQ(reply.body.find("page")->i, 6);
    EXPECT_TRUE(reply.body.find("typing")->b) << "a line typed and not entered";

    // The file its SAVE wrote, as base64
    reply = SyncRun(_context, "sync-extract", {{"as", "file"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    std::vector<uint8_t> file;
    ASSERT_TRUE(base64::Decode(reply.body.find("data")->s, file));
    EXPECT_TRUE(file == ReadBytes(SyncData("alasm509-typing") / "SNAKE.H"));

    // As text, decoded with ALASM 5.07-5.09's table
    reply = SyncRun(_context, "sync-extract");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("format")->s, "alasm");
    EXPECT_GT(reply.body.find("lines")->i, 10);
    EXPECT_FALSE(reply.body.find("text")->s.empty());
}

TEST_F(SyncControl_Test, TasmMidLineGivesTheLineBeingTypedAndConverts)
{
    LoadSession(_context, "tasm412-typing");
    AsmReply reply = SyncRun(_context, "sync-status");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("assembler")->s, "tasm-4.12");
    EXPECT_TRUE(reply.body.find("editor")->b);
    EXPECT_EQ(reply.body.find("current_line")->i, 25) << "the editor reopened at line 20, the session went 5 down";

    reply = SyncRun(_context, "sync-extract", {{"as", "file"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    std::vector<uint8_t> file;
    ASSERT_TRUE(base64::Decode(reply.body.find("data")->s, file));
    EXPECT_TRUE(file == ReadBytes(SyncData("tasm412-typing") / "SNAKE.A")) << "the typed line is in it, as TASM saved it";

    reply = SyncRun(_context, "sync-extract", {{"as", "dialect"}, {"to", "sjasmplus"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("to")->s, "sjasmplus");
    EXPECT_FALSE(reply.body.find("text")->s.empty());

    EXPECT_EQ(SyncRun(_context, "sync-extract", {{"as", "dialect"}}).HttpStatus(), 400) << "no 'to'";
    EXPECT_EQ(SyncRun(_context, "sync-extract", {{"as", "pdf"}}).HttpStatus(), 400);
    EXPECT_EQ(SyncRun(_context, "sync-status", {{"assembler", "masm-9"}}).HttpStatus(), 404);
}

TEST_F(SyncControl_Test, TheFileGoesToTheHostAsTheAssemblerSavesIt)
{
    LoadSession(_context, "tasm412-command");
    const std::string out = TestPathHelper::GetUniqueTestScratchPath("sync-SNAKE.A");
    const AsmReply reply = SyncRun(_context, "sync-extract", {{"as", "file"}, {"output", out}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_FALSE(reply.body.find("editor")->b) << "the command line: every line is in the text";
    EXPECT_TRUE(ReadBytes(out) == ReadBytes(SyncData("tasm412-command") / "SNAKE.A"));
}

// The watch (phase Y1): the worker reads the text, builds it after the pause, publishes its labels

TEST_F(SyncControl_Test, TheWatchPublishesTheLabelsAndFollowsAChange)
{
    // Boot-bound worker: the waits are on its builds (interval 50 ms, no quiet period)
    LoadSession(_context, "tasm412-top");
    AsmReply reply = SyncRun(_context, "sync-watch", {{"interval", "50"}, {"quiet", "0"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(reply.body.find("watching")->b);
    EXPECT_EQ(SyncRun(_context, "sync-watch", {{"interval", "5"}}).HttpStatus(), 400);
    EXPECT_EQ(SyncRun(_context, "sync-watch", {{"as", "dialect"}}).HttpStatus(), 400);

    const auto generation = [&]() {
        const AsmReply hints = SyncRun(_context, "sync-hints");
        return hints.Ok() ? hints.body.find("generation")->i : 0;
    };
    ASSERT_TRUE(TestWait::For([&] { return generation() >= 1; })) << "the first build";
    reply = SyncRun(_context, "sync-hints");
    EXPECT_EQ(reply.body.find("assembler")->s, "tasm-4.12");
    EXPECT_EQ(reply.body.find("set")->s, "live:sync:tasm-4.12");
    EXPECT_GT(reply.body.find("labels")->i, 10);

    LabelManager* labels = _context->pDebugManager->GetLabelManager();
    ASSERT_NE(labels->GetLabelByName("KEY"), nullptr) << "SNAKE's KEY, from the live set";
    bool found = false;
    for (const auto& set : labels->GetSymbolSets())
        if (set.id == "live:sync:tasm-4.12")
        {
            found = true;
            EXPECT_EQ(set.priority, AsmSyncService::kLabelPriority);
        }
    EXPECT_TRUE(found);

    // The guest edits: the next look sees the change, the next build follows
    const int64_t before = generation();
    LoadSession(_context, "tasm412-typing");
    EXPECT_TRUE(TestWait::For([&] { return generation() > before; })) << "a build after the change";
    reply = SyncRun(_context, "sync-status");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(reply.body.find("watch")->find("watching")->b);

    reply = SyncRun(_context, "sync-unwatch");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_FALSE(reply.body.find("watching")->b);
    EXPECT_NE(labels->GetLabelByName("KEY"), nullptr) << "the labels stay after the watch";
}

// The push watch (Z2): each save of the host file goes into ALASM while it waits for a command; a change the user made
// in the guest is a conflict and stays; while ALASM is busy the write waits
TEST_F(SyncControl_Test, ThePushWatchWritesEachSaveAndKeepsTheUsersChanges)
{
    // Worker-bound: the waits are on its writes (interval 50 ms, no quiet period)
    LoadSession(_context, "alasm509-loaded");
    const auto stack = [&](uint16_t loop) {
        const std::vector<uint16_t> words = {0x9199, 0x91E3, loop, 0x2D2B};   // #9707: the A> prompt; #9733: a command runs
        uint8_t* page5 = _context->pMemory->RAMPageAddress(5);
        for (size_t k = 0; k < words.size(); ++k)
        {
            page5[0x1FBE + 2 * k] = static_cast<uint8_t>(words[k]);
            page5[0x1FBE + 2 * k + 1] = static_cast<uint8_t>(words[k] >> 8);
        }
        _context->pCore->GetZ80()->sp = 0x5FBE;
    };
    stack(0x9707);
    const std::filesystem::path folder = TestPathHelper::GetUniqueTestScratchPath("push");
    std::filesystem::create_directories(folder);
    const std::filesystem::path main = folder / "main.asm";
    const auto save = [&](const std::string& text) { std::ofstream(main, std::ios::binary) << text; };
    const auto guestText = [&]() {
        const AsmReply reply = SyncRun(_context, "sync-extract");
        return reply.Ok() ? reply.body.find("text")->s : std::string();
    };
    AsmSyncService* service = _context->pDebugManager->GetAsmSyncService();
    const auto state = [&]() { return service->PushStatusValue().find("state")->s; };
    const auto pushes = [&]() { return service->PushStatusValue().find("pushes")->i; };

    EXPECT_EQ(SyncRun(_context, "sync-push-watch", {}).HttpStatus(), 400) << "no path";
    EXPECT_EQ(SyncRun(_context, "sync-push-watch", {{"path", (folder / "missing.asm").string()}}).HttpStatus(), 404);

    save("        ORG #6000\nfirst   RET\n");
    AsmReply reply = SyncRun(_context, "sync-push-watch", {{"path", main.string()}, {"title", "MAIN"}, {"interval", "50"}, {"quiet", "0"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(reply.body.find("pushing")->b);
    ASSERT_TRUE(TestWait::For([&] { return pushes() >= 1; })) << "the first write: " << state();
    EXPECT_NE(guestText().find("first"), std::string::npos);

    // A save: written again, over the watch's own text (marked changed, yet not the user's)
    save("        ORG #6000\nsecond  RET\n        NOP\n");
    ASSERT_TRUE(TestWait::For([&] { return pushes() >= 2; })) << "the save: " << state();
    EXPECT_NE(guestText().find("second"), std::string::npos);

    // ALASM busy: the next save waits, then goes in once it is idle again
    stack(0x9733);
    save("        ORG #6000\nthird   RET\n");
    ASSERT_TRUE(TestWait::For([&] { return state() == "pending"; })) << state();
    stack(0x9707);
    ASSERT_TRUE(TestWait::For([&] { return pushes() >= 3; })) << "after the wait: " << state();
    EXPECT_NE(guestText().find("third"), std::string::npos);

    // The user edits the text in the guest (a label renamed): the next save is a conflict and the guest keeps its text
    uint8_t* text = _context->pMemory->RAMPageAddress(6);
    uint8_t* at = std::search(text + 0x40, text + 0x4000, reinterpret_cast<const uint8_t*>("third"), reinterpret_cast<const uint8_t*>("third") + 5);
    ASSERT_NE(at, text + 0x4000);
    at[0] = 'T';
    save("        ORG #6000\nfourth  RET\n");
    ASSERT_TRUE(TestWait::For([&] { return state() == "conflict"; })) << state();
    EXPECT_NE(guestText().find("Third"), std::string::npos);
    EXPECT_EQ(pushes(), 3);

    reply = SyncRun(_context, "sync-status");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("push")->find("state")->s, "conflict");
    reply = SyncRun(_context, "sync-push-unwatch");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_FALSE(reply.body.find("pushing")->b);
    std::filesystem::remove_all(folder);
}

TEST_F(SyncControl_Test, TheWatchTakesTheProjectFromAHostFolder)
{
    // Y5 (asm-synchronizer.md §5.4): a host folder joins the project of every build; a bad item is refused
    const std::filesystem::path folder = TestPathHelper::GetUniqueTestScratchPath("asm-sync-project");
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "lib.asm") << "VALUE   EQU 1234\n";
    std::ofstream(folder / "font.bin", std::ios::binary) << std::string(64, '\0');

    EXPECT_EQ(SyncRun(_context, "sync-watch", {{"project", "nowhere-at-all"}}).HttpStatus(), 400);
    EXPECT_EQ(SyncRun(_context, "sync-watch", {{"project", "disk:Z"}}).HttpStatus(), 400);

    LoadSession(_context, "tasm412-top");
    AsmReply reply = SyncRun(_context, "sync-watch", {{"interval", "50"}, {"quiet", "0"}, {"project", "none," + folder.string()}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const auto generation = [&]() {
        const AsmReply hints = SyncRun(_context, "sync-hints");
        return hints.Ok() ? hints.body.find("generation")->i : 0;
    };
    ASSERT_TRUE(TestWait::For([&] { return generation() >= 1; })) << "the first build";
    reply = SyncRun(_context, "sync-hints");
    ASSERT_NE(reply.body.find("project"), nullptr);
    EXPECT_EQ(reply.body.find("project")->i, 1) << "lib.asm; font.bin is a size for INCBIN only";
    SyncRun(_context, "sync-unwatch");
    std::filesystem::remove_all(folder);
}

// Part B (Z0): a sjasmplus source written into ALASM 5.09 at its command line, as ALASM's own text
TEST_F(SyncControl_Test, InjectWritesASourceIntoAlasmAtItsCommandLine)
{
    LoadSession(_context, "alasm509-loaded");
    // ALASM's stack (page 5): the line input's return and the command loop's while it waits for a command
    auto stack = [&](uint16_t loop) {
        const std::vector<uint16_t> words = {0x5C78, 0x0042, 0x9084, 0x9043, 0x8EF2, 0xBF15, 0x7FFD, 0x9199, 0x91E3, loop};
        uint8_t* page5 = _context->pMemory->RAMPageAddress(5);
        for (size_t k = 0; k < words.size(); ++k)
        {
            page5[0x1FBE + 2 * k] = static_cast<uint8_t>(words[k]);
            page5[0x1FBE + 2 * k + 1] = static_cast<uint8_t>(words[k] >> 8);
        }
        _context->pCore->GetZ80()->sp = 0x5FBE;
    };
    const std::map<std::string, std::string> source = {{"text", "        ORG #6000\nstart   ld a,1+2*3\n        jr start\n"}, {"from", "sjasmplus"}};

    stack(0x9733);   // a command running: refused
    AsmReply reply = SyncRun(_context, "sync-inject", source);
    EXPECT_EQ(reply.error, AsmControlError::Refused);
    EXPECT_NE(reply.message.find("not waiting for a command"), std::string::npos) << reply.message;

    stack(0x9707);
    reply = SyncRun(_context, "sync-inject", source);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("assembler")->s, "alasm-5.09");
    EXPECT_EQ(reply.body.find("page")->i, 6);
    EXPECT_EQ(reply.body.find("name")->s, "SNAKE");   // the text's name in memory stays
    EXPECT_EQ(reply.body.find("previous")->find("name")->s, "SNAKE");

    // ALASM's text now: the source in ALASM's spelling, marked changed since saved
    reply = SyncRun(_context, "sync-extract");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const std::string text = reply.body.find("text")->s;
    EXPECT_NE(text.find("LD A,1+(2*3)"), std::string::npos) << text;
    EXPECT_NE(text.find("JR start"), std::string::npos) << text;
    EXPECT_TRUE(reply.body.find("changed")->b);

    // Changes not saved: another write needs force
    reply = SyncRun(_context, "sync-inject", source);
    EXPECT_EQ(reply.error, AsmControlError::Refused);
    std::map<std::string, std::string> forced = source;
    forced["force"] = "true";
    EXPECT_TRUE(SyncRun(_context, "sync-inject", forced).Ok());
}

// A host file with INCLUDEs: each INCLUDEd source into an ALASM page of its own (4, 0), the INCLUDEs naming the pages
TEST_F(SyncControl_Test, InjectPutsTheSourcesAHostFileIncludesIntoPagesOfTheirOwn)
{
    LoadSession(_context, "alasm509-loaded");
    const std::vector<uint16_t> words = {0x9199, 0x91E3, 0x9707, 0x2D2B};   // the A> prompt
    uint8_t* page5 = _context->pMemory->RAMPageAddress(5);
    for (size_t k = 0; k < words.size(); ++k)
    {
        page5[0x1FBE + 2 * k] = static_cast<uint8_t>(words[k]);
        page5[0x1FBE + 2 * k + 1] = static_cast<uint8_t>(words[k] >> 8);
    }
    _context->pCore->GetZ80()->sp = 0x5FBE;
    const std::filesystem::path folder = TestPathHelper::GetUniqueTestScratchPath("project");
    std::filesystem::create_directories(folder / "lib");
    const auto write = [&](const std::filesystem::path& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; };
    // INF reads as ASM80's directive: detection is "ambiguous: asm80 vs text", the file still goes as a text
    write(folder / "main.asm", "        ORG #6000\n        INCLUDE \"lib/inc1.asm\"\n        INCLUDE \"inc2.asm\"\n        LD A,INF\nend_    RET\nINF     EQU 1\n");
    write(folder / "lib" / "inc1.asm", "        LD HL,end_\n");
    write(folder / "inc2.asm", "        NOP\n");

    // no 'from': a text file is sjasmplus (it was "no frontend for dialect ''")
    AsmReply reply = SyncRun(_context, "sync-inject", {{"path", (folder / "main.asm").string()}, {"title", "MAIN"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("from")->s, "sjasmplus");
    EXPECT_EQ(reply.body.find("page")->i, 6);
    const StateNode* includes = reply.body.find("includes");
    ASSERT_NE(includes, nullptr);
    ASSERT_EQ(includes->items.size(), 2u);
    EXPECT_EQ(includes->items[0].find("source")->s, "lib/inc1");
    EXPECT_EQ(includes->items[0].find("name")->s, "INC1");
    EXPECT_EQ(includes->items[0].find("page")->i, 4);
    EXPECT_EQ(includes->items[1].find("name")->s, "INC2");
    EXPECT_EQ(includes->items[1].find("page")->i, 0);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(_context->pMemory->RAMPageAddress(4)), 8), "INC1    ");
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(_context->pMemory->RAMPageAddress(0)), 8), "INC2    ");

    reply = SyncRun(_context, "sync-extract");
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const std::string text = reply.body.find("text")->s;
    EXPECT_NE(text.find("INCLUDE \"INC1\",4"), std::string::npos) << text;
    EXPECT_NE(text.find("INCLUDE \"INC2\",0"), std::string::npos) << text;
    std::filesystem::remove_all(folder);
}

// Part B (Z1): the same verbs on a snapshot file instead of the machine - ALASM 5.09's idle template; the machine stays untouched
TEST_F(SyncControl_Test, InjectWritesASourceIntoAnAlasmSnapshot)
{
    const std::filesystem::path templates = TestPathHelper::FindProjectRoot() / "core" / "src" / "3rdparty" / "unreal-asm" / "testdata" / "templates";
    const std::string output = TestPathHelper::GetUniqueTestScratchPath("injected.szx");
    const std::vector<uint8_t> before(_context->pMemory->RAMPageAddress(6), _context->pMemory->RAMPageAddress(6) + 0x4000);

    AsmReply reply = SyncRun(_context, "sync-probe", {{"snapshot", (templates / "alasm-5.09.szx").string()}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    ASSERT_FALSE(reply.body.find("candidates")->items.empty());

    std::map<std::string, std::string> source = {{"text", "        ORG #6000\nstart   ld a,1+2*3\n        jr start\n"}, {"from", "sjasmplus"},
                                                 {"snapshot", (templates / "alasm-5.09.szx").string()}, {"assembler", "alasm-5.09"}, {"title", "MAIN"}};
    reply = SyncRun(_context, "sync-inject", source);
    EXPECT_EQ(reply.error, AsmControlError::BadRequest) << "no output: " << reply.message;
    source["output"] = output;
    reply = SyncRun(_context, "sync-inject", source);
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.find("format")->s, "szx");
    EXPECT_EQ(reply.body.find("page")->i, 6);
    EXPECT_EQ(reply.body.find("name")->s, "MAIN");

    reply = SyncRun(_context, "sync-extract", {{"snapshot", output}, {"assembler", "alasm-5.09"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const std::string text = reply.body.find("text")->s;
    EXPECT_NE(text.find("LD A,1+(2*3)"), std::string::npos) << text;
    EXPECT_EQ(reply.body.find("snapshot")->s, output);
    EXPECT_TRUE(std::equal(before.begin(), before.end(), _context->pMemory->RAMPageAddress(6))) << "the machine's RAM is not written";

    EXPECT_EQ(SyncRun(_context, "sync-status", {{"snapshot", output + ".missing"}}).error, AsmControlError::NotFound);
    std::filesystem::remove(output);
}

// The same verbs with no emulator instance (the WebAPI's /asm/sync routes): only on a snapshot file
TEST(SyncControlNoInstance_Test, SnapshotVerbsNeedNoEmulator)
{
    const std::filesystem::path templates = TestPathHelper::FindProjectRoot() / "core" / "src" / "3rdparty" / "unreal-asm" / "testdata" / "templates";
    const std::string snapshot = (templates / "alasm-5.09.szx").string();
    const std::string output = TestPathHelper::GetUniqueTestScratchPath("injected-noinstance.szx");

    AsmReply reply = SyncRun(nullptr, "sync-probe", {{"snapshot", snapshot}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    ASSERT_FALSE(reply.body.find("candidates")->items.empty());

    reply = SyncRun(nullptr, "sync-inject",
                    {{"text", "        ORG #6000\n        nop\n"}, {"from", "sjasmplus"}, {"snapshot", snapshot}, {"assembler", "alasm-5.09"}, {"output", output}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    reply = SyncRun(nullptr, "sync-extract", {{"snapshot", output}, {"assembler", "alasm-5.09"}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_NE(reply.body.find("text")->s.find("NOP"), std::string::npos);
    EXPECT_TRUE(SyncRun(nullptr, "sync-status", {{"snapshot", output}}).Ok());

    EXPECT_EQ(SyncRun(nullptr, "sync-probe").error, AsmControlError::BadRequest);          // the machine: needs one
    EXPECT_EQ(SyncRun(nullptr, "sync-watch", {{"snapshot", snapshot}}).error, AsmControlError::BadRequest);
    std::filesystem::remove(output);
}
