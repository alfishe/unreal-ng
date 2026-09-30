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
