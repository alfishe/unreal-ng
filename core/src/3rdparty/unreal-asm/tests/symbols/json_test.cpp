// The native file's JSON reader and writer: every value type, escapes, exact integers, member order, bad input

#include <gtest/gtest.h>

#include "symbols/io/json.h"

using namespace unrealasm::symbols;

namespace
{
json::Value Parsed(const std::string& text)
{
    json::Value v;
    std::string error;
    size_t offset = 0;
    EXPECT_TRUE(json::Parse(text, v, error, offset)) << text << ": " << error;
    return v;
}
}  // namespace

TEST(Json_Test, ValuesRoundTrip)
{
    const std::string text = R"({"b":true,"n":null,"i":-9007199254740993,"r":1.5,"s":"a\"\\\n\u0001","a":[1,[],{}],"o":{"z":1,"a":2}})";
    const json::Value v = Parsed(text);
    EXPECT_EQ(v.Get("i")->integer, -9007199254740993LL);   // exact past a double's 53 bits
    EXPECT_DOUBLE_EQ(v.Get("r")->real, 1.5);
    EXPECT_EQ(v.Get("s")->string, std::string("a\"\\\n\x01"));
    EXPECT_EQ(v.Get("o")->object[0].first, "z");           // member order kept
    EXPECT_EQ(json::Write(v), text);
}

TEST(Json_Test, UnicodeEscapes)
{
    EXPECT_EQ(Parsed(R"("\u0410\u00e9")").string, "\xD0\x90\xC3\xA9");
    EXPECT_EQ(Parsed(R"("\ud83d\ude00")").string, "\xF0\x9F\x98\x80");   // a surrogate pair
    EXPECT_EQ(Parsed("\"\xD0\x90\"").string, "\xD0\x90");                 // raw UTF-8 passes through
    EXPECT_EQ(json::Quote("\xD0\x90"), "\"\xD0\x90\"");
}

TEST(Json_Test, BadInput)
{
    for (const char* bad : {"", "{", "[1,]", "{\"a\" 1}", "\"\\ud800\"", "tru", "1 2", "\"a\nb\"", "{\"a\":1,}", "-", "01x"})
    {
        json::Value v;
        std::string error;
        size_t offset = 0;
        EXPECT_FALSE(json::Parse(bad, v, error, offset)) << bad;
        EXPECT_FALSE(error.empty()) << bad;
    }
    std::string deep(1000, '[');
    json::Value v;
    std::string error;
    size_t offset = 0;
    EXPECT_FALSE(json::Parse(deep, v, error, offset));   // nesting is bounded, no stack overflow
}
