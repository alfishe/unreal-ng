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
#include "debugger/analyzers/analysis/analysiscontrol.h"
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
