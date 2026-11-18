/// @file cli-ttd-cards_test.cpp
/// @brief CLI time travel on a sound card's CPU (cli-ttd-cards.h): the `--cpu` flag and the text of card moves and
/// readouts, over the TTDControl replies of a real NeoGS recording. No CLI socket.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../automation/cli/src/commands/cli-ttd-cards.h"
#include "_helpers/ttdcardbench.h"

TEST(CliTTDCards_Test, TheCpuFlagIsTakenOutOfTheArguments)
{
    std::vector<std::string> args = {"seek", "--cpu", "gs", "1234"};
    EXPECT_EQ(CliTTDCards::TakeCpuFlag(args), "gs");
    EXPECT_EQ(args, (std::vector<std::string>{"seek", "1234"}));

    std::vector<std::string> main = {"si-back", "--cpu", "MAIN"};
    EXPECT_EQ(CliTTDCards::TakeCpuFlag(main), "");
    EXPECT_EQ(main, (std::vector<std::string>{"si-back"}));

    std::vector<std::string> none = {"reverse-step", "--count", "3"};
    EXPECT_EQ(CliTTDCards::TakeCpuFlag(none), "");
    EXPECT_EQ(none.size(), 3u);
}

TEST(CliTTDCards_Test, CardMovesAndReadoutsAsText)
{
    ttdtest::Bench b;
    ASSERT_TRUE(ttdtest::Build(b, ttdtest::Card::NeoGS));
    const uint64_t perFrame = ttdtest::PerFrame(b);
    ttdtest::Record(b, 3);
    const uint64_t target = b.Count() - perFrame;
    ttd::TTDControl control(b.context);

    const ttd::TTDReply seek = control.Execute({"seek", {{"cpu", "gs"}, {"instruction", std::to_string(target)}}});
    const std::string text = CliTTDCards::FormatCardMove(seek, "\n");
    EXPECT_EQ(text.rfind("TTD: gs at instruction " + std::to_string(target) + " (from ", 0), 0u) << text;
    EXPECT_NE(text.find("  Card gs: instruction " + std::to_string(target) + ", PC #"), std::string::npos) << text;
    EXPECT_NE(text.find("(card position; main CPU at frame "), std::string::npos) << text;

    const ttd::TTDReply position = control.Execute({"position", {}});
    ASSERT_TRUE(position.Ok());
    const StateNode* cards = position.body.find("cards");
    ASSERT_NE(cards, nullptr);
    ASSERT_EQ(cards->items.size(), 1u);
    const std::string line = CliTTDCards::FormatCardLine(cards->items[0], "\n");
    EXPECT_NE(line.find("card position"), std::string::npos) << line;

    const ttd::TTDReply early = control.Execute({"seek", {{"cpu", "gs"}, {"instruction", "0"}}});
    const std::string refused = CliTTDCards::FormatCardMove(early, "\n");
    EXPECT_EQ(refused.rfind("TTD: gs did not move (at_start): ", 0), 0u) << refused;

    const ttd::TTDReply noBreakpoints = control.Execute({"reverse-continue", {{"cpu", "gs"}}});
    const std::string error = CliTTDCards::FormatCardMove(noBreakpoints, "\n");
    EXPECT_EQ(error.rfind("Error: ", 0), 0u) << error;
    EXPECT_NE(error.find("breakpoint"), std::string::npos) << error;
}
