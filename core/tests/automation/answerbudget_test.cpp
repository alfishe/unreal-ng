// Answer budgets (docs/inprogress/2026-11-04-answer-budgets): fitting text and JSON in N characters

#include <gtest/gtest.h>
#include <json/json.h>

#include <string>

#include "../../automation/common/answerbudget.h"

namespace
{
std::string Lines(int count, const std::string& prefix = "line ")
{
    std::string text = "Summary: " + std::to_string(count) + " lines";
    for (int i = 1; i <= count; i++)
        text += "\n" + prefix + std::to_string(i) + " .........................................";
    text += "\nTotal: " + std::to_string(count);
    return text;
}
}  // namespace

TEST(AnswerBudget_Test, ATextThatFitsIsUnchanged)
{
    const std::string text = Lines(5);
    EXPECT_EQ(answer_budget::FitText(text, 5000), text);
}

TEST(AnswerBudget_Test, ALongTextKeepsTheFirstAndLastLinesAndSaysWhatWasLeftOut)
{
    const std::string text = Lines(400);
    const std::string fitted = answer_budget::FitText(text, 2000);
    EXPECT_LE(answer_budget::Chars(fitted), 2000u);
    EXPECT_EQ(fitted.rfind("Summary: 400 lines\nline 1 ", 0), 0u) << fitted;
    EXPECT_NE(fitted.find("\nTotal: 400"), std::string::npos) << "the totals line stays";
    EXPECT_NE(fitted.find("of 402 lines"), std::string::npos) << fitted;
    EXPECT_NE(fitted.find("left out for max_chars=2000; narrow the call or raise max_chars"), std::string::npos) << fitted;
    EXPECT_EQ(fitted.find("line 200 "), std::string::npos);
}

TEST(AnswerBudget_Test, OneLongLineIsCutAndNeverInsideAUtf8Sequence)
{
    std::string line;
    for (int i = 0; i < 3000; i++)
        line += "\xD0\x96";  // Cyrillic Zhe: 2 bytes, 1 character
    const std::string fitted = answer_budget::FitText(line, 600);
    EXPECT_LE(answer_budget::Chars(fitted), 600u);
    EXPECT_NE(fitted.find("...\n... "), std::string::npos) << fitted;
    // Every lead byte is followed by its continuation byte
    for (size_t i = 0; i < fitted.size(); i++)
        if (static_cast<unsigned char>(fitted[i]) == 0xD0)
            EXPECT_EQ(static_cast<unsigned char>(fitted[i + 1]), 0x96u) << i;
}

TEST(AnswerBudget_Test, JsonLosesTheTailOfItsBiggestArrayAndSaysWhere)
{
    Json::Value value;
    value["count"] = 1000;
    for (int i = 0; i < 1000; i++)
    {
        Json::Value item;
        item["name"] = "symbol_" + std::to_string(i);
        item["address"] = 0x8000 + i;
        value["symbols"].append(item);
    }
    value["small"].append(1);
    value["small"].append(2);
    size_t full = 0;
    const Json::Value fitted = answer_budget::FitJson(value, 3000, &full);
    EXPECT_EQ(full, answer_budget::CompactJson(value).size()) << "the size estimate is the written size";
    EXPECT_LE(answer_budget::CompactJson(fitted).size(), 3000u);
    EXPECT_EQ(fitted["count"].asInt(), 1000);
    EXPECT_EQ(fitted["small"].size(), 2u) << "only what had to go";
    ASSERT_GT(fitted["symbols"].size(), 10u);
    EXPECT_EQ(fitted["symbols"][0]["name"].asString(), "symbol_0") << "the head stays";
    EXPECT_EQ(fitted["truncated"]["omitted"]["symbols"].asUInt(), 1000u - fitted["symbols"].size());
    EXPECT_EQ(fitted["truncated"]["chars"].asUInt64(), full);
    EXPECT_EQ(fitted["truncated"]["max_chars"].asUInt(), 3000u);
}

TEST(AnswerBudget_Test, JsonCutsALongStringAndNestedPathsAreNamed)
{
    Json::Value value;
    value["frames"][0]["events"] = Json::Value(Json::arrayValue);
    for (int i = 0; i < 500; i++)
        value["frames"][0]["events"].append("event " + std::to_string(i));
    value["dump"] = std::string(5000, 'A');
    const Json::Value fitted = answer_budget::FitJson(value, 1500);
    EXPECT_LE(answer_budget::CompactJson(fitted).size(), 1500u) << answer_budget::CompactJson(fitted);
    EXPECT_TRUE(fitted["truncated"]["omitted"].isMember("dump"));
    EXPECT_TRUE(fitted["truncated"]["omitted"].isMember("frames[0].events"));
}

TEST(AnswerBudget_Test, AJsonStringOfLinesIsCutOnLineBoundaries)
{
    Json::Value value;
    value["hexdump"] = Lines(300, "0x8000: FF FF ");
    const Json::Value fitted = answer_budget::FitJson(value, 1500, nullptr, 1500);
    const std::string dump = fitted["hexdump"].asString();
    EXPECT_EQ(dump.rfind("Summary: 300 lines\n0x8000: FF FF 1 ", 0), 0u) << dump;
    EXPECT_NE(dump.find("left out for max_chars=1500"), std::string::npos) << dump;
    EXPECT_NE(dump.find("\nTotal: 300"), std::string::npos) << dump;
    EXPECT_LE(answer_budget::CompactJson(fitted).size(), 1500u);
}

TEST(AnswerBudget_Test, AShortCutOfLinesEndsOnALineBoundary)
{
    Json::Value value;
    value["hexdump"] = Lines(300, "0x8000: FF FF ");
    const std::string dump = answer_budget::FitJson(value, 600, nullptr, 600)["hexdump"].asString();
    ASSERT_GE(dump.size(), 4u);
    EXPECT_EQ(dump.substr(dump.size() - 4), "\n...") << dump;
}

TEST(AnswerBudget_Test, JsonThatFitsIsUnchanged)
{
    Json::Value value;
    value["a"] = "short";
    EXPECT_EQ(answer_budget::FitJson(value, 1000), value);
}

TEST(AnswerBudget_Test, TheBudgetIsAWholeNumberInRange)
{
    size_t value = 0;
    std::string why;
    EXPECT_TRUE(answer_budget::Parse(std::string("2000"), value, why));
    EXPECT_EQ(value, 2000u);
    EXPECT_TRUE(answer_budget::Parse(Json::Value(500), value, why));
    for (const char* bad : {"", "499", "12a", "-1", "99999999999"})
        EXPECT_FALSE(answer_budget::Parse(std::string(bad), value, why)) << bad;
    EXPECT_FALSE(answer_budget::Parse(Json::Value(true), value, why));
    EXPECT_NE(why.find("max_chars"), std::string::npos);
}
