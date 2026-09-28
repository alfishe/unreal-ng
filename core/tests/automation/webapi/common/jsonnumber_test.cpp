#include <gtest/gtest.h>
#include <json/json.h>

#include "../../../../automation/webapi/src/common/jsonnumber.h"

/// ParseJsonUInt: request fields that carry addresses and register values.
/// JSON has no hex literals, so hex arrives as a string - it used to reach
/// asUInt() and fail instead of being parsed.

namespace
{
bool Parse(const Json::Value& v, uint32_t max, uint32_t& out)
{
    out = 0xDEADBEEF;
    return ParseJsonUInt(v, max, out);
}
}  // namespace

TEST(JsonNumber_Test, AcceptsNumbersAndEveryHexSpelling)
{
    uint32_t v = 0;
    EXPECT_TRUE(Parse(Json::Value(4660), 0xFFFF, v));
    EXPECT_EQ(v, 0x1234u);
    for (const char* text : {"4660", "0x1234", "0X1234", "#1234", "$1234", "0x1234" })
    {
        SCOPED_TRACE(text);
        EXPECT_TRUE(Parse(Json::Value(text), 0xFFFF, v));
        EXPECT_EQ(v, 0x1234u);
    }
    EXPECT_TRUE(Parse(Json::Value("0xabCD"), 0xFFFF, v));
    EXPECT_EQ(v, 0xABCDu);
    EXPECT_TRUE(Parse(Json::Value(5.0), 0xFFFF, v)) << "an integral double is a number";
    EXPECT_EQ(v, 5u);
}

TEST(JsonNumber_Test, RangeLimitIsInclusive)
{
    uint32_t v = 0;
    EXPECT_TRUE(Parse(Json::Value(65535), 0xFFFF, v));
    EXPECT_TRUE(Parse(Json::Value("0xFFFF"), 0xFFFF, v));
    EXPECT_FALSE(Parse(Json::Value(65536), 0xFFFF, v));
    EXPECT_FALSE(Parse(Json::Value("0x10000"), 0xFFFF, v));
    EXPECT_FALSE(Parse(Json::Value("$100"), 0xFF, v));
    EXPECT_FALSE(Parse(Json::Value("99999999999999999999999"), 0xFFFFFFFFu, v)) << "no silent overflow";
}

TEST(JsonNumber_Test, RejectsEverythingElse)
{
    uint32_t v = 0;
    for (const char* text : {"", "0x", "#", "$", "12abc", "0x12g", " 12", "12 ", "-1", "+1", "1.5", "0b101", "abc"})
    {
        SCOPED_TRACE(std::string("'") + text + "'");
        EXPECT_FALSE(Parse(Json::Value(text), 0xFFFF, v));
    }
    EXPECT_FALSE(Parse(Json::Value(-1), 0xFFFF, v));
    EXPECT_FALSE(Parse(Json::Value(1.5), 0xFFFF, v));
    EXPECT_FALSE(Parse(Json::Value(true), 0xFFFF, v));
    EXPECT_FALSE(Parse(Json::Value(Json::nullValue), 0xFFFF, v));
    EXPECT_FALSE(Parse(Json::Value(Json::objectValue), 0xFFFF, v));
}
