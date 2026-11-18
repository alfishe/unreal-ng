#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>

#include "_helpers/ttdcardbench.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/ttd/ttdcontrol.h"

/// The TTDControl verbs on a sound card's CPU (gs-debugger design §8), the layer the CLI, Lua, Python, REST,
/// WebSocket and MCP share: the `cpu` option of seek, step-instruction, reverse-step and reverse-continue, and the
/// cards[] readout of position and status. The engine's own behaviour is TTD_CardPositions_*'s.
///
/// Runtime justification: one NeoGS machine recording 3 frames, a few card moves (~0.2 s per test).
namespace
{
using namespace ttdtest;

ttd::TTDReply Verb(Bench& b, const std::string& verb, const std::map<std::string, std::string>& options = {})
{
    return ttd::TTDControl(b.context).Execute({verb, options});
}

const StateNode& Field(const ttd::TTDReply& reply, const char* name)
{
    static const StateNode none;
    const StateNode* node = reply.body.find(name);
    return node ? *node : none;
}
}  // namespace

TEST(TTDControl_CardPositions_Test, EachMoveVerbTakesTheCpuOption)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    const uint64_t perFrame = PerFrame(b);
    Record(b, 3);
    const uint64_t target = b.Count() - perFrame - 50;
    const std::string t = std::to_string(target);

    const ttd::TTDReply seek = Verb(b, "seek", {{"cpu", "gs"}, {"instruction", t}});
    ASSERT_TRUE(seek.Ok()) << seek.message;
    EXPECT_EQ(Field(seek, "cpu").s, "gs");
    EXPECT_EQ(Field(seek, "status").s, "ok");
    EXPECT_TRUE(Field(seek, "reached").b);
    EXPECT_EQ(static_cast<uint64_t>(Field(seek, "target").i), target);
    const StateNode& card = Field(seek, "card");
    EXPECT_EQ(static_cast<uint64_t>(card.find("instruction")->i), target);
    EXPECT_TRUE(card.find("card_position")->b);
    EXPECT_TRUE(card.find("caught_up")->b);
    const StateNode* main = card.find("main");
    ASSERT_NE(main, nullptr);
    EXPECT_EQ(main->find("frame")->i, Field(seek, "frame").i) << "the reply's position is the main CPU's boundary";
    EXPECT_EQ(main->find("tinframe")->i, Field(seek, "tinframe").i);
    EXPECT_NE(main->find("t_offset"), nullptr);
    EXPECT_NE(main->find("inside"), nullptr);
    EXPECT_NE(seek.body.find("check"), nullptr);
    EXPECT_EQ(Field(seek, "state").s, "detached");

    // N back and forward; reverse-step takes count only
    const ttd::TTDReply back = Verb(b, "step-instruction", {{"cpu", "neogs"}, {"dir", "back"}, {"count", "5"}});
    ASSERT_TRUE(back.Ok()) << back.message;
    EXPECT_EQ(static_cast<uint64_t>(Field(back, "from").i), target);
    EXPECT_EQ(static_cast<uint64_t>(Field(back, "card").find("instruction")->i), target - 5);
    const ttd::TTDReply forward = Verb(b, "step-instruction", {{"cpu", "gs"}, {"dir", "forward"}});
    ASSERT_TRUE(forward.Ok()) << forward.message;
    EXPECT_EQ(static_cast<uint64_t>(Field(forward, "card").find("instruction")->i), target - 4);
    const ttd::TTDReply reverse = Verb(b, "reverse-step", {{"cpu", "gs"}, {"count", "2"}});
    ASSERT_TRUE(reverse.Ok()) << reverse.message;
    EXPECT_EQ(static_cast<uint64_t>(Field(reverse, "card").find("instruction")->i), target - 6);

    // The readout: position (a poll, never a session operation) and status name the card position
    for (const char* verb : {"position", "status"})
    {
        const ttd::TTDReply r = Verb(b, verb);
        ASSERT_TRUE(r.Ok()) << verb;
        const StateNode& cards = Field(r, "cards");
        ASSERT_EQ(cards.items.size(), 1u) << verb;
        EXPECT_EQ(cards.items[0].find("cpu")->s, "gs") << verb;
        EXPECT_EQ(static_cast<uint64_t>(cards.items[0].find("instruction")->i), target - 6) << verb;
        EXPECT_TRUE(cards.items[0].find("card_position")->b) << verb;
    }

    // Reverse continue: refused without breakpoints (409), the previous hit with one, its breakpoint named
    const ttd::TTDReply none = Verb(b, "reverse-continue", {{"cpu", "gs"}});
    EXPECT_EQ(none.error, ttd::TTDControlError::Conflict);
    EXPECT_EQ(Field(none, "status").s, "no_breakpoints");
    const uint16_t pc = static_cast<uint16_t>(Field(reverse, "card").find("pc")->i);
    BreakpointSpec spec;
    spec.type = BRK_MEMORY;
    spec.access = BRK_MEM_EXECUTE;
    spec.address = pc;
    std::string error;
    const uint16_t id = b.context->pDebugManager->GetBreakpointsManager(DebugCpuId::GS)->AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;
    const ttd::TTDReply hit = Verb(b, "reverse-continue", {{"cpu", "gs"}, {"max_frames", "10"}});
    ASSERT_TRUE(hit.Ok()) << hit.message;
    EXPECT_TRUE(Field(hit, "reached").b);
    EXPECT_LT(static_cast<uint64_t>(Field(hit, "card").find("instruction")->i), target - 6);
    EXPECT_EQ(Field(hit, "breakpoint").find("id")->i, id);
    EXPECT_EQ(Field(hit, "breakpoint").find("kind")->s, "execute");
    EXPECT_EQ(Field(hit, "breakpoint").find("address")->i, pc);
    EXPECT_GE(Field(hit, "searched_frames").i, 1);
    EXPECT_FALSE(Field(hit, "truncated").b);

    // Before the history: an answer, not an error (nothing moved)
    const ttd::TTDReply early = Verb(b, "seek", {{"cpu", "gs"}, {"instruction", "0"}});
    ASSERT_TRUE(early.Ok()) << early.message;
    EXPECT_FALSE(Field(early, "reached").b);
    EXPECT_EQ(Field(early, "status").s, "at_start");
    EXPECT_FALSE(Field(early, "message").s.empty());
}

TEST(TTDControl_CardPositions_Test, BadRequestsAreRefusedBeforeAnythingMoves)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    Record(b, 2);
    struct Case
    {
        const char* verb;
        std::map<std::string, std::string> options;
        const char* says;
    };
    const Case cases[] = {
        {"seek", {{"cpu", "zz"}, {"instruction", "5"}}, "Unknown cpu"},
        {"seek", {{"cpu", "gs"}}, "instruction"},
        {"seek", {{"cpu", "gs"}, {"instruction", "-3"}}, "non-negative"},
        {"seek", {{"cpu", "gs"}, {"instruction", "5"}, {"frame", "3"}}, "no option 'frame'"},
        {"seek", {{"instruction", "5"}}, "needs 'cpu'"},
        {"step-instruction", {{"count", "2"}}, "needs 'cpu'"},
        {"step-instruction", {{"cpu", "gs"}, {"count", "0"}}, "positive"},
        {"reverse-step", {{"cpu", "gs"}, {"tstates", "10"}}, "no option 'tstates'"},
        {"reverse-step", {{"cpu", "ms"}}, "count"},
        {"reverse-continue", {{"cpu", "gs"}, {"pcs", "100"}}, "no option 'pcs'"},
        {"reverse-continue", {{"cpu", "gs"}, {"max_frames", "0"}}, "positive"},
        {"reverse-continue", {{"pcs", "100"}, {"max_frames", "3"}}, "needs 'cpu'"},
    };
    const ttd::TTDTimePoint before = b.ttd->CurrentPosition();
    for (const Case& c : cases)
    {
        const ttd::TTDReply r = Verb(b, c.verb, c.options);
        EXPECT_EQ(r.error, ttd::TTDControlError::BadRequest) << c.verb << ": " << r.message;
        EXPECT_NE(r.message.find(c.says), std::string::npos) << c.verb << ": " << r.message;
    }
    EXPECT_EQ(b.ttd->CurrentPosition().frame, before.frame);
    EXPECT_EQ(b.ttd->CurrentPosition().tInFrame, before.tInFrame);

    // cpu main: the main CPU's verbs as before
    const ttd::TTDReply mainStep = Verb(b, "step-instruction", {{"cpu", "main"}, {"dir", "back"}});
    ASSERT_TRUE(mainStep.Ok()) << mainStep.message;
    EXPECT_NE(mainStep.body.find("stepped"), nullptr);

    // No MultiSound card on this machine: refused (409), named
    const ttd::TTDReply noCard = Verb(b, "seek", {{"cpu", "multisound"}, {"instruction", "5"}});
    EXPECT_EQ(noCard.error, ttd::TTDControlError::Conflict);
    EXPECT_EQ(Field(noCard, "status").s, "no_card");
}
