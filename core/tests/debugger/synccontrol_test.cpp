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
