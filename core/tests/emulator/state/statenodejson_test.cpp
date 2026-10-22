// StateNodeToJsonText: the JSON text of a StateNode (CLI --json, media replies)

#include <gtest/gtest.h>

#include <cmath>

#include "emulator/state/statenodejson.h"

TEST(StateNodeJson_Test, NestedValuesKeepFieldOrder)
{
    StateNode medium = StateNode::Object();
    medium["source"] = "games/elite.trd";
    medium["dirty"] = true;
    medium["dirtyUnits"] = uint64_t{3};
    StateNode aliases = StateNode::Array();
    aliases.push("A");
    StateNode slot = StateNode::Object();
    slot["id"] = "fdd.a";
    slot["aliases"] = aliases;
    slot["medium"] = medium;
    slot["empty"] = StateNode();

    EXPECT_EQ(StateNodeToJsonText(slot),
              R"({"id":"fdd.a","aliases":["A"],"medium":{"source":"games/elite.trd","dirty":true,"dirtyUnits":3},"empty":null})");
}

TEST(StateNodeJson_Test, StringsAreEscapedAndNumbersValid)
{
    StateNode value = StateNode::Object();
    value["path"] = "C:\\zx\\\xD0\x98.trd";
    value["s"] = "q\"\n\x01";
    value["half"] = 0.5;
    value["nan"] = std::nan("");
    EXPECT_EQ(StateNodeToJsonText(value),
              "{\"path\":\"C:\\\\zx\\\\\xD0\x98.trd\",\"s\":\"q\\\"\\n\\u0001\",\"half\":0.5,\"nan\":null}");
}

// StateNodeFromJsonText: a companion's answers (the Stream Deck daemon's /api/v1/health) read back
TEST(StateNodeJson_Test, ParsesWhatItWritesAndTypesNumbers)
{
    StateNode value;
    std::string error;
    ASSERT_TRUE(StateNodeFromJsonText(
        R"( {"ok":true,"pid":4242,"half":0.5,"big":1e3,"neg":-7,"name":"deck \"A\"\n\u00e9\ud83d\ude00","none":null,
             "decks":[{"serial":"A1"},[],{}]} )",
        value, &error))
        << error;
    ASSERT_TRUE(value.isObject());
    EXPECT_EQ(value.find("ok")->kind, StateNode::Kind::Bool);
    EXPECT_TRUE(value.find("ok")->b);
    EXPECT_EQ(value.find("pid")->kind, StateNode::Kind::Int);
    EXPECT_EQ(value.find("pid")->i, 4242);
    EXPECT_EQ(value.find("neg")->i, -7);
    EXPECT_EQ(value.find("half")->kind, StateNode::Kind::Double);
    EXPECT_DOUBLE_EQ(value.find("big")->d, 1000.0);
    EXPECT_EQ(value.find("name")->s, "deck \"A\"\n\xC3\xA9\xF0\x9F\x98\x80");
    EXPECT_EQ(value.find("none")->kind, StateNode::Kind::Null);
    ASSERT_EQ(value.find("decks")->size(), 3u);
    EXPECT_EQ(value.find("decks")->items[0].find("serial")->s, "A1");

    StateNode again;
    ASSERT_TRUE(StateNodeFromJsonText(StateNodeToJsonText(value), again));
    EXPECT_EQ(StateNodeToJsonText(again), StateNodeToJsonText(value)) << "a round trip keeps every field and its order";
}

TEST(StateNodeJson_Test, RefusesWhatIsNotOneJsonValue)
{
    StateNode value;
    std::string error;
    for (const char* text : {"", "{", "[1,]", "{\"a\" 1}", "tru", "\"open", "{} {}", "{\"a\":\"\x01\"}", "[\"\\q\"]"})
    {
        EXPECT_FALSE(StateNodeFromJsonText(text, value, &error)) << text;
        EXPECT_FALSE(error.empty()) << text;
        EXPECT_EQ(value.kind, StateNode::Kind::Null) << text;
        error.clear();
    }
    std::string deep(100, '[');
    deep += std::string(100, ']');
    EXPECT_FALSE(StateNodeFromJsonText(deep, value)) << "nested too deep";
}
