/// @file analysiscontrol_test.cpp
/// @brief The `analysis` verbs every plane shares (analysiscontrol.h): address parsing, the refusals, the status and
/// the CLI's words (cli-analysis.h) giving the same action and options, hence the same answer, as the REST routes'
/// query and body (analysis_api.cpp passes them through unchanged).

#include "pch.h"

#include <map>
#include <string>
#include <vector>

#include "../../../../automation/cli/src/commands/cli-analysis.h"
#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/analysis/analysiscontrol.h"
#include "debugger/analyzers/analysis/analysisjobs.h"
#include "debugger/assembler/z80textassembler.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/emulator.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"

namespace
{
const StateNode& Member(const StateNode& node, const std::string& name)
{
    static const StateNode kNull;
    const StateNode* found = node.find(name);
    return found ? *found : kNull;
}
}  // namespace

TEST(AnalysisControl_Test, ParseAddressForms)
{
    uint16_t address = 0;
    for (const char* text : {"$80BC", "#80BC", "0x80BC", "80BCh", "32956"})
    {
        ASSERT_TRUE(analysis::ParseAddress(text, address)) << text;
        EXPECT_EQ(address, 0x80BC) << text;
    }
    for (const char* text : {"", "$", "0x10000", "70000", "zz", "$80BCX"})
        EXPECT_FALSE(analysis::ParseAddress(text, address)) << text;
}

TEST(AnalysisControl_Test, CliWords)
{
    std::string action, error;
    std::map<std::string, std::string> options;
    bool json = false;

    ASSERT_TRUE(CliAnalysis::ParseArgs({}, action, options, json, error));
    EXPECT_EQ(action, "status");
    ASSERT_TRUE(CliAnalysis::ParseArgs({"start", "physical", "counters"}, action, options, json, error));
    EXPECT_EQ(action, "start");
    EXPECT_EQ(options, (std::map<std::string, std::string>{{"level", "counters"}, {"mode", "physical"}}));
    ASSERT_TRUE(CliAnalysis::ParseArgs({"explain", "$C000", "ram:1", "--json"}, action, options, json, error));
    EXPECT_EQ(options, (std::map<std::string, std::string>{{"address", "$C000"}, {"page", "ram:1"}}));
    EXPECT_TRUE(json);
    ASSERT_TRUE(CliAnalysis::ParseArgs({"explain", "address=0x8000"}, action, options, json, error));
    EXPECT_EQ(options.at("address"), "0x8000");

    ASSERT_TRUE(CliAnalysis::ParseArgs({"start", "record"}, action, options, json, error));
    EXPECT_EQ(options, (std::map<std::string, std::string>{{"record", "true"}}));
    ASSERT_TRUE(CliAnalysis::ParseArgs({"build", "500", "wait=2000"}, action, options, json, error));
    EXPECT_EQ(options, (std::map<std::string, std::string>{{"frames", "500"}, {"wait", "2000"}}));
    ASSERT_TRUE(CliAnalysis::ParseArgs({"explain", "$8000", "routine"}, action, options, json, error));
    EXPECT_EQ(options, (std::map<std::string, std::string>{{"address", "$8000"}, {"what", "routine"}}));

    EXPECT_FALSE(CliAnalysis::ParseArgs({"start", "fast"}, action, options, json, error));
    EXPECT_FALSE(CliAnalysis::ParseArgs({"explain", "1", "2", "3"}, action, options, json, error));
}

class AnalysisControlEmu_Test : public ::testing::Test
{
protected:
    static void SetUpTestSuite() { _shared = EmulatorTestHelper::CreateDebugEmulator({"debugmode"}, "PENTAGON"); }
    static void TearDownTestSuite()
    {
        EmulatorTestHelper::CleanupEmulator(_shared);
        _shared = nullptr;
    }
    void SetUp() override
    {
        _emulator = _shared;
        ASSERT_NE(_emulator, nullptr);
        // Each test starts from the default level: counters, no facts
        ASSERT_TRUE(analysis::Execute(*_emulator, "start", {{"level", "counters"}}).Ok());
        ASSERT_TRUE(analysis::Execute(*_emulator, "stop").Ok());
    }

    /// The CLI's path: its words, then analysis::Execute
    analysis::Reply Cli(const std::vector<std::string>& words)
    {
        std::string action, error;
        analysis::Options options;
        bool json = false;
        EXPECT_TRUE(CliAnalysis::ParseArgs(words, action, options, json, error)) << error;
        return analysis::Execute(*_emulator, action, options);
    }

    static inline Emulator* _shared = nullptr;
    Emulator* _emulator = nullptr;
};

TEST_F(AnalysisControlEmu_Test, RefusalsAndStatus)
{
    EXPECT_EQ(analysis::Execute(*_emulator, "frobnicate").error, analysis::Error::BadRequest);
    EXPECT_EQ(analysis::Execute(*_emulator, "status", {{"address", "1"}}).error, analysis::Error::BadRequest);
    EXPECT_EQ(analysis::Execute(*_emulator, "start", {{"level", "deep"}}).error, analysis::Error::BadRequest);
    EXPECT_EQ(analysis::Execute(*_emulator, "explain", {{"address", "$8000"}}).error, analysis::Error::Conflict)
        << "no facts before a start";

    const analysis::Reply started = analysis::Execute(*_emulator, "start");
    ASSERT_TRUE(started.Ok()) << started.message;
    EXPECT_EQ(Member(started.body, "state").s, "capturing");
    EXPECT_EQ(Member(started.body, "level").s, "facts");
    EXPECT_EQ(Member(started.body, "mode").s, "z80");
    EXPECT_TRUE(Member(Member(started.body, "facts"), "allocated").b);
    EXPECT_FALSE(started.text.empty());

    EXPECT_EQ(analysis::Execute(*_emulator, "explain").error, analysis::Error::BadRequest) << "no address";
    EXPECT_EQ(analysis::Execute(*_emulator, "explain", {{"address", "no_such_label"}}).error, analysis::Error::NotFound);
    EXPECT_EQ(analysis::Execute(*_emulator, "explain", {{"address", "$8000"}, {"page", "ram:1"}}).error,
              analysis::Error::BadRequest)
        << "page only in physical mode";

    EXPECT_EQ(Member(started.ToValue(), "text").s, started.text);
    const StateNode refusal = analysis::Execute(*_emulator, "frobnicate").ToValue();
    EXPECT_EQ(Member(refusal, "error").s, "Bad Request");
    EXPECT_FALSE(Member(refusal, "message").s.empty());
}

/// The CLI's words and the REST options reach the same Execute: the same text
TEST_F(AnalysisControlEmu_Test, CliAndRestGiveTheSameAnswer)
{
    ASSERT_TRUE(Cli({"start", "facts", "z80"}).Ok());

    const analysis::Reply cli = Cli({"explain", "$8000"});
    const analysis::Reply rest = analysis::Execute(*_emulator, "explain", {{"address", "$8000"}});
    ASSERT_TRUE(cli.Ok()) << cli.message;
    EXPECT_EQ(cli.text, rest.text);
    EXPECT_EQ(Cli({"status"}).text, analysis::Execute(*_emulator, "status").text);
    EXPECT_EQ(Cli({"stop"}).error, analysis::Error::None);
    EXPECT_EQ(Member(analysis::Execute(*_emulator, "status").body, "state").s, "stopped");
}

/// The L1 verbs end to end (P2): start record=true, run, build (the long operation, waited for here), status, explain
/// a routine (the fact sheet) and a byte, the refusals
class AnalysisControlL1_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _emulator->GetFeatureManager()->setFeature(Features::kSoundGeneration, false);
        if (_emulator->GetContext()->pSoundManager)
            _emulator->GetContext()->pSoundManager->mute();
        const AsmResult code = Z80TextAssembler().Assemble(R"(
        org $8000
start:  ld sp,$9F00
        di
main:   ld hl,$A000
        call fill
        jr main
fill:   ld b,8
loop:   ld (hl),b
        inc hl
        djnz loop
        ret
)",
                                                           0x8000);
        ASSERT_TRUE(code.ok) << code.error.message;
        for (size_t i = 0; i < code.bytes.size(); ++i)
            _emulator->GetMemory()->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), code.bytes[i]);
        _fill = static_cast<uint16_t>(code.symbols.at("fill"));
        Z80* z80 = _emulator->GetContext()->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->iff1 = z80->iff2 = 0;
    }
    void TearDown() override
    {
        if (_emulator)
        {
            analysis::L1Jobs::Instance().Forget(_emulator->GetId());
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    }

    Emulator* _emulator = nullptr;
    uint16_t _fill = 0;
};

TEST_F(AnalysisControlL1_Test, BuildThenExplainARoutine)
{
    EXPECT_EQ(analysis::Execute(*_emulator, "build").error, analysis::Error::Conflict) << "no recording yet";
    EXPECT_EQ(analysis::Execute(*_emulator, "cancel").error, analysis::Error::Conflict) << "no build running";
    EXPECT_EQ(analysis::Execute(*_emulator, "build", {{"wait", "60000"}}).error, analysis::Error::BadRequest);
    EXPECT_EQ(analysis::Execute(*_emulator, "start", {{"record", "maybe"}}).error, analysis::Error::BadRequest);
    EXPECT_EQ(analysis::Execute(*_emulator, "explain", {{"address", "$8000"}, {"what", "routine"}}).error,
              analysis::Error::Conflict)
        << "no L1 yet";

    const analysis::Reply started = analysis::Execute(*_emulator, "start", {{"record", "true"}});
    ASSERT_TRUE(started.Ok()) << started.message;
    EXPECT_TRUE(Member(started.body, "recording").b);
    _emulator->RunNFrames(2);

    const analysis::Reply built = analysis::Execute(*_emulator, "build", {{"wait", "20000"}});
    ASSERT_TRUE(built.Ok()) << built.message;
    EXPECT_GT(Member(built.body, "job").i, 0u);
    ASSERT_EQ(Member(built.body, "state").s, "done") << built.text;
    EXPECT_EQ(Member(Member(built.body, "bundle"), "frames").i, 2u);
    EXPECT_NE(built.text.find("routines"), std::string::npos) << built.text;

    const analysis::Reply status = analysis::Execute(*_emulator, "status");
    const StateNode& ready = Member(Member(status.body, "l1"), "ready");
    EXPECT_TRUE(Member(ready, "ready").b);
    EXPECT_GE(Member(ready, "routines").i, 2u);
    EXPECT_FALSE(Member(status.body, "recording").b) << "build ended the recording";
    EXPECT_NE(status.text.find("L1 ready"), std::string::npos) << status.text;

    // The routine at fill: its fact sheet (within the budget, with a disassembly excerpt)
    const analysis::Reply routine = analysis::Execute(*_emulator, "explain", {{"address", std::to_string(_fill)}});
    ASSERT_TRUE(routine.Ok()) << routine.message;
    EXPECT_EQ(Member(routine.body, "what").s, "routine");
    EXPECT_LE(routine.text.size(), 1024u);
    EXPECT_NE(routine.text.find("Routine"), std::string::npos) << routine.text;
    EXPECT_NE(routine.text.find("djnz"), std::string::npos) << routine.text;
    EXPECT_NE(routine.text.find("loop"), std::string::npos) << routine.text;

    // A byte the code only writes: L0 (what=auto finds no routine there); what=routine refuses
    const analysis::Reply byte = analysis::Execute(*_emulator, "explain", {{"address", "$A000"}});
    ASSERT_TRUE(byte.Ok()) << byte.message;
    EXPECT_EQ(Member(byte.body, "what").s, "byte");
    EXPECT_EQ(analysis::Execute(*_emulator, "explain", {{"address", "$A000"}, {"what", "routine"}}).error,
              analysis::Error::NotFound);
    EXPECT_EQ(Member(analysis::Execute(*_emulator, "explain", {{"address", std::to_string(_fill)}, {"what", "byte"}}).body,
                     "what")
                  .s,
              "byte");
}
