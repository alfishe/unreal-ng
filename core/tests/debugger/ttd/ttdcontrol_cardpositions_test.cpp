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
        {"reverse-step-over", {{"cpu", "zz"}}, "Unknown cpu"},
        {"reverse-step-over", {{"cpu", "gs"}, {"count", "2"}}, "no option 'count'"},
        {"step-instruction", {{"kind", "over"}}, "needs 'cpu'"},
        {"step-instruction", {{"cpu", "gs"}, {"kind", "sideways"}}, "kind must be"},
        {"step-instruction", {{"cpu", "gs"}, {"kind", "over"}, {"dir", "back"}}, "reverse-step-over"},
        {"step-instruction", {{"cpu", "gs"}, {"kind", "out"}, {"count", "2"}}, "no 'count'"},
        {"step-instruction", {{"cpu", "gs"}, {"kind", "to"}}, "needs 'pc'"},
        {"step-instruction", {{"cpu", "gs"}, {"kind", "to"}, {"pc", "70000"}}, "pc must be"},
        {"step-instruction", {{"cpu", "gs"}, {"kind", "to"}, {"pc", "0x8000"}, {"max_frames", "0"}}, "positive"},
        {"step-instruction", {{"cpu", "gs"}, {"kind", "to"}, {"pc", "0x8000"}, {"dir", "back"}}, "forward only"},
        {"step-instruction", {{"cpu", "gs"}, {"kind", "over"}, {"pc", "0x8000"}}, "no 'pc'"},
        {"step-instruction", {{"cpu", "gs"}, {"pc", "0x8000"}}, "goes with kind to"},
        {"step-instruction", {{"pc", "0x8000"}}, "needs 'cpu'"},
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
    const ttd::TTDReply noCardOver = Verb(b, "reverse-step-over", {{"cpu", "ms"}});
    EXPECT_EQ(noCardOver.error, ttd::TTDControlError::Conflict);
    EXPECT_EQ(Field(noCardOver, "status").s, "no_card");
    EXPECT_FALSE(Field(noCardOver, "stepped").b);
}

/// reverse-step-over with cpu: the card's step over back, the card move's reply with the main CPU's reply words beside
/// it (stepped, over_call, skipped, pc, text); without cpu (or cpu main) the main CPU's as before
TEST(TTDControl_CardPositions_Test, ReverseStepOverTakesTheCpuOption)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    ASSERT_TRUE(LoadCardRoutines(b));
    const uint64_t perFrame = PerFrame(b);
    Record(b, 3);
    const uint64_t start = b.Count() - perFrame - 50;

    // Card positions forward until one step over back passes over a routine (its return the instruction before)
    ttd::TTDReply over;
    uint64_t from = start;
    for (int i = 0; i < 400; i++, from++)
    {
        ASSERT_TRUE(Verb(b, "seek", {{"cpu", "gs"}, {"instruction", std::to_string(from)}}).Ok());
        over = Verb(b, "reverse-step-over", {{"cpu", "neogs"}});
        ASSERT_TRUE(over.Ok()) << over.message;
        ASSERT_TRUE(Field(over, "stepped").b) << Field(over, "text").s;
        if (Field(over, "over_call").b)
            break;
        // Not over a routine: exactly one card instruction back
        EXPECT_EQ(static_cast<uint64_t>(Field(over, "target").i), from - 1);
        EXPECT_EQ(over.body.find("skipped"), nullptr);
    }
    ASSERT_TRUE(Field(over, "over_call").b) << "no card routine return within 400 instructions";
    EXPECT_EQ(Field(over, "cpu").s, "gs");
    EXPECT_EQ(Field(over, "status").s, "ok");
    EXPECT_TRUE(Field(over, "reached").b);
    EXPECT_EQ(static_cast<uint64_t>(Field(over, "from").i), from);
    const uint64_t landed = static_cast<uint64_t>(Field(over, "target").i);
    EXPECT_LT(landed, from - 1);
    EXPECT_EQ(static_cast<uint64_t>(Field(over, "skipped").i), from - 1 - landed);
    EXPECT_EQ(static_cast<uint64_t>(Field(over, "card").find("instruction")->i), landed);
    EXPECT_EQ(Field(over, "pc").i, Field(over, "card").find("pc")->i);
    EXPECT_NE(Field(over, "text").s.find("stepped back over a routine"), std::string::npos) << Field(over, "text").s;
    EXPECT_NE(over.body.find("check"), nullptr);
    EXPECT_EQ(over.body.find("call_out_of_reach"), nullptr);

    // The main CPU's: no card move words
    for (const std::map<std::string, std::string>& options :
         {std::map<std::string, std::string>{}, std::map<std::string, std::string>{{"cpu", "main"}}})
    {
        const ttd::TTDReply main = Verb(b, "reverse-step-over", options);
        ASSERT_TRUE(main.Ok()) << main.message;
        EXPECT_TRUE(Field(main, "stepped").b);
        EXPECT_EQ(main.body.find("card"), nullptr);
        EXPECT_FALSE(b.ttd->AtCardPosition()) << "a main CPU move leaves the card position";
    }
}

/// step-instruction with cpu and kind over / out: the card's step through the history, the card move's reply with
/// kind, history_end, truncated and searched_frames; at the history's end at_end (200, nothing moved)
TEST(TTDControl_CardPositions_Test, StepInstructionKindOverAndOut)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    const uint64_t perFrame = PerFrame(b);
    Record(b, 3);
    const uint64_t start = b.Count() - perFrame * 2;

    ASSERT_TRUE(Verb(b, "seek", {{"cpu", "gs"}, {"instruction", std::to_string(start)}}).Ok());
    const ttd::TTDReply over = Verb(b, "step-instruction", {{"cpu", "gs"}, {"kind", "over"}});
    ASSERT_TRUE(over.Ok()) << over.message;
    EXPECT_EQ(Field(over, "status").s, "ok");
    EXPECT_TRUE(Field(over, "reached").b);
    EXPECT_EQ(Field(over, "kind").s, "over");
    EXPECT_EQ(static_cast<uint64_t>(Field(over, "from").i), start);
    EXPECT_GT(static_cast<uint64_t>(Field(over, "target").i), start);
    EXPECT_FALSE(Field(over, "history_end").b);
    EXPECT_NE(over.body.find("searched_frames"), nullptr);
    EXPECT_NE(over.body.find("check"), nullptr);
    EXPECT_EQ(static_cast<uint64_t>(Field(over, "card").find("instruction")->i),
              static_cast<uint64_t>(Field(over, "target").i));

    // kind in: the plain card step
    const ttd::TTDReply in = Verb(b, "step-instruction", {{"cpu", "gs"}, {"kind", "in"}, {"dir", "forward"}});
    ASSERT_TRUE(in.Ok()) << in.message;
    EXPECT_EQ(static_cast<uint64_t>(Field(in, "target").i), static_cast<uint64_t>(Field(over, "target").i) + 1);

    // kind out: later on (the routine's end, or the history's end when the card is in none)
    const ttd::TTDReply out = Verb(b, "step-instruction", {{"cpu", "gs"}, {"kind", "out"}, {"dir", "forward"}});
    ASSERT_TRUE(out.Ok()) << out.message;
    EXPECT_EQ(Field(out, "kind").s, "out");
    EXPECT_TRUE(Field(out, "reached").b) << Field(out, "message").s;
    EXPECT_GT(static_cast<uint64_t>(Field(out, "target").i), static_cast<uint64_t>(Field(in, "target").i));

    // The history's end: nothing to step through, an answer
    const ttd::TTDTimePoint end = b.ttd->SessionEndPosition();
    ASSERT_TRUE(b.ttd->SeekTo({end.frame, 0}));
    const uint64_t endCount = b.ttd->CardPositionNow(b.cpu).instruction;
    ASSERT_TRUE(Verb(b, "seek", {{"cpu", "gs"}, {"instruction", std::to_string(endCount)}}).Ok());
    const ttd::TTDReply atEnd = Verb(b, "step-instruction", {{"cpu", "gs"}, {"kind", "over"}});
    ASSERT_TRUE(atEnd.Ok()) << atEnd.message;
    EXPECT_EQ(Field(atEnd, "status").s, "at_end");
    EXPECT_FALSE(Field(atEnd, "reached").b);
    EXPECT_NE(Field(atEnd, "message").s.find("live"), std::string::npos) << Field(atEnd, "message").s;
}

/// step-instruction with cpu, kind to and pc: the card's run to the cursor through the history, the card move's reply
/// with cursor, hit, searched_frames and truncated; a cursor never reached is no_hit (200, nothing moved)
TEST(TTDControl_CardPositions_Test, StepInstructionKindToRunsToTheCursor)
{
    Bench b;
    ASSERT_TRUE(Build(b, Card::NeoGS));
    ASSERT_TRUE(LoadCardRoutines(b));
    const uint64_t perFrame = PerFrame(b);
    Record(b, 3);
    const uint64_t start = b.Count() - perFrame * 2;

    ASSERT_TRUE(Verb(b, "seek", {{"cpu", "gs"}, {"instruction", std::to_string(start)}}).Ok());
    const ttd::TTDReply to = Verb(b, "step-instruction", {{"cpu", "gs"}, {"kind", "to"}, {"pc", "#4020"}});
    ASSERT_TRUE(to.Ok()) << to.message;
    EXPECT_EQ(Field(to, "status").s, "ok");
    EXPECT_TRUE(Field(to, "reached").b);
    EXPECT_TRUE(Field(to, "hit").b);
    EXPECT_EQ(Field(to, "kind").s, "to");
    EXPECT_EQ(Field(to, "cursor").i, 0x4020);
    EXPECT_EQ(static_cast<uint64_t>(Field(to, "from").i), start);
    EXPECT_GT(static_cast<uint64_t>(Field(to, "target").i), start);
    EXPECT_LE(static_cast<uint64_t>(Field(to, "target").i), start + 20) << "the routine is a few instructions away";
    EXPECT_EQ(Field(to, "card").find("pc")->i, 0x4020);
    EXPECT_EQ(to.body.find("history_end"), nullptr);
    EXPECT_FALSE(Field(to, "truncated").b);
    EXPECT_NE(to.body.find("searched_frames"), nullptr);
    EXPECT_NE(to.body.find("check"), nullptr);

    // Again: the next time round, not where it stands
    const ttd::TTDReply again = Verb(b, "step-instruction", {{"cpu", "gs"}, {"kind", "to"}, {"pc", "16416"}});
    ASSERT_TRUE(again.Ok()) << again.message;
    EXPECT_GT(Field(again, "target").i, Field(to, "target").i);

    // Never reached: an answer, nothing moved
    const ttd::TTDReply miss = Verb(b, "step-instruction", {{"cpu", "gs"}, {"kind", "to"}, {"pc", "$5000"}});
    ASSERT_TRUE(miss.Ok()) << miss.message;
    EXPECT_EQ(Field(miss, "status").s, "no_hit");
    EXPECT_FALSE(Field(miss, "reached").b);
    EXPECT_FALSE(Field(miss, "hit").b);
    EXPECT_NE(Field(miss, "message").s.find("#5000"), std::string::npos) << Field(miss, "message").s;
    EXPECT_EQ(b.ttd->CardPositionNow(b.cpu).instruction, static_cast<uint64_t>(Field(again, "target").i));
}
