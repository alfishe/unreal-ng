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

/// `ttd reverse-step-over --cpu gs`: the card move's text says when the card went back over a routine
TEST(CliTTDCards_Test, AStepOverBackNamesTheRoutinePassedOver)
{
    ttd::TTDReply reply;
    reply.body["cpu"] = "gs";
    reply.body["reached"] = true;
    reply.body["from"] = 1205;
    reply.body["target"] = 1190;
    reply.body["over_call"] = true;
    reply.body["skipped"] = 14;
    const std::string over = CliTTDCards::FormatCardMove(reply, "\n");
    EXPECT_EQ(over.rfind("TTD: gs at instruction 1190 (from 1205), stepped back over a routine of 14 instruction(s)", 0), 0u)
        << over;

    reply.body["over_call"] = false;
    reply.body["call_out_of_reach"] = true;
    reply.body["target"] = 1204;
    const std::string reach = CliTTDCards::FormatCardMove(reply, "\n");
    EXPECT_NE(reach.find("on the return: the routine's start is out of reach"), std::string::npos) << reach;
}

TEST(CliTTDCards_Test, AStepOverInThePastSaysWhereItEnded)
{
    ttd::TTDReply reply;
    reply.body["cpu"] = "gs";
    reply.body["kind"] = "over";
    reply.body["reached"] = true;
    reply.body["from"] = 1200;
    reply.body["target"] = 1290;
    reply.body["searched_frames"] = 1;
    reply.body["history_end"] = false;
    reply.body["truncated"] = false;
    const std::string over = CliTTDCards::FormatCardMove(reply, "\n");
    EXPECT_EQ(over.rfind("TTD: gs at instruction 1290 (from 1200)\n", 0), 0u) << over;
    EXPECT_NE(over.find("Searched 1 frame(s)\n"), std::string::npos) << over;

    reply.body["history_end"] = true;
    const std::string end = CliTTDCards::FormatCardMove(reply, "\n");
    EXPECT_NE(end.find("did not end in the history: at its end (the next step runs on live)"), std::string::npos)
        << end;

    reply.body["history_end"] = false;
    reply.body["truncated"] = true;
    const std::string cut = CliTTDCards::FormatCardMove(reply, "\n");
    EXPECT_NE(cut.find("did not end in the frames searched"), std::string::npos) << cut;
    EXPECT_EQ(cut.find("max_frames reached"), std::string::npos) << cut;
}
