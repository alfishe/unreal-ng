// UnicodeHelper: UTF-8 / UTF-16 / code points and the CP866 / CP1251 code pages

#include <gtest/gtest.h>

#include <string>

#include "common/unicodehelper.h"

TEST(UnicodeHelper_Test, Utf8RoundTripIncludingAstral)
{
    const std::string text = "Aя€😀";  // 1, 2, 3 and 4-byte sequences
    const std::u32string cps = UnicodeHelper::DecodeUtf8(text);
    ASSERT_EQ(cps.size(), 4u);
    EXPECT_EQ(cps[0], U'A');
    EXPECT_EQ(cps[1], 0x044F);
    EXPECT_EQ(cps[2], 0x20AC);
    EXPECT_EQ(cps[3], 0x1F600);
    EXPECT_EQ(UnicodeHelper::EncodeUtf8(cps), text);
}

TEST(UnicodeHelper_Test, BadUtf8BecomesReplacement)
{
    const std::u32string cps = UnicodeHelper::DecodeUtf8(std::string("a\x80" "b\xC0\xAF" "c\xE2\x82", 8));
    // stray continuation, overlong '/', truncated sequence
    ASSERT_EQ(cps.size(), 6u);
    EXPECT_EQ(cps[0], U'a');
    EXPECT_EQ(cps[1], UnicodeHelper::kReplacement);
    EXPECT_EQ(cps[2], U'b');
    EXPECT_EQ(cps[3], UnicodeHelper::kReplacement);
    EXPECT_EQ(cps[4], U'c');
    EXPECT_EQ(cps[5], UnicodeHelper::kReplacement);
}

TEST(UnicodeHelper_Test, Utf16SurrogatesBothWays)
{
    const std::u16string units = UnicodeHelper::ToUtf16(U"Я😀");
    ASSERT_EQ(units.size(), 3u);
    EXPECT_EQ(units[0], 0x042F);
    EXPECT_EQ(units[1], 0xD83D);
    EXPECT_EQ(units[2], 0xDE00);
    EXPECT_EQ(UnicodeHelper::FromUtf16(units), U"Я😀");
    EXPECT_EQ(UnicodeHelper::FromUtf16(std::u16string(1, char16_t(0xD800))), std::u32string(1, UnicodeHelper::kReplacement));
}

TEST(UnicodeHelper_Test, CodePagesRoundTripEveryByte)
{
    for (CodePage page : {CodePage::Cp866, CodePage::Cp1251})
    {
        for (int b = 0; b < 256; b++)
        {
            const char32_t cp = UnicodeHelper::FromCodePage(page, static_cast<uint8_t>(b));
            if (cp == UnicodeHelper::kReplacement)
                continue;  // CP1251 #98 is undefined
            uint8_t back = 0;
            ASSERT_TRUE(UnicodeHelper::ToCodePage(page, cp, back)) << UnicodeHelper::CodePageName(page) << " byte " << b;
            EXPECT_EQ(back, b);
        }
    }
}

TEST(UnicodeHelper_Test, KnownCyrillicBytes)
{
    uint8_t byte = 0;
    ASSERT_TRUE(UnicodeHelper::ToCodePage(CodePage::Cp866, U'Д', byte));
    EXPECT_EQ(byte, 0x84);
    ASSERT_TRUE(UnicodeHelper::ToCodePage(CodePage::Cp1251, U'Д', byte));
    EXPECT_EQ(byte, 0xC4);
    ASSERT_TRUE(UnicodeHelper::ToCodePage(CodePage::Cp866, U'Ё', byte));
    EXPECT_EQ(byte, 0xF0);
    ASSERT_TRUE(UnicodeHelper::ToCodePage(CodePage::Cp1251, U'Ё', byte));
    EXPECT_EQ(byte, 0xA8);
    EXPECT_FALSE(UnicodeHelper::ToCodePage(CodePage::Cp866, U'€', byte)) << "no euro sign in CP866";
    EXPECT_TRUE(UnicodeHelper::ToCodePage(CodePage::Cp1251, U'€', byte));
    EXPECT_EQ(byte, 0x88);

    CodePage page = CodePage::Cp866;
    EXPECT_TRUE(UnicodeHelper::ParseCodePage("Windows-1251", page));
    EXPECT_EQ(page, CodePage::Cp1251);
    EXPECT_TRUE(UnicodeHelper::ParseCodePage("866", page));
    EXPECT_EQ(page, CodePage::Cp866);
    EXPECT_FALSE(UnicodeHelper::ParseCodePage("koi8-r", page));
}

TEST(UnicodeHelper_Test, UpperCaseCoversTheCodePages)
{
    EXPECT_EQ(UnicodeHelper::ToUpper(U'z'), U'Z');
    EXPECT_EQ(UnicodeHelper::ToUpper(U'я'), U'Я');
    EXPECT_EQ(UnicodeHelper::ToUpper(U'ё'), U'Ё');
    EXPECT_EQ(UnicodeHelper::ToUpper(U'і'), U'І');
    EXPECT_EQ(UnicodeHelper::ToUpper(U'ґ'), U'Ґ');
    EXPECT_EQ(UnicodeHelper::ToUpper(U'€'), U'€');
}
