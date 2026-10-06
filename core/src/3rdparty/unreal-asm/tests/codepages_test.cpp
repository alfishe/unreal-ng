// Code page tables and UTF-8 conversion (encoding.h): every byte of every single-byte page round-trips; known
// characters land where the code pages put them; unmappable characters are reported.

#include <gtest/gtest.h>

#include "unrealasm/encoding.h"

using namespace unrealasm::encoding;

TEST(CodePages_Test, EveryByteRoundTrips)
{
    for (CodePage page : {CodePage::Ascii, CodePage::Utf8, CodePage::Cp866, CodePage::Koi8r, CodePage::Cp1251, CodePage::ZxSpectrum})
        for (int b = 0; b < 256; ++b)
        {
            const char32_t cp = ByteToCodePoint(static_cast<uint8_t>(b), page);
            uint8_t back = 0;
            ASSERT_TRUE(CodePointToByte(cp, page, back)) << CodePageName(page) << " byte " << b;
            EXPECT_EQ(back, b) << CodePageName(page);
        }
}

TEST(CodePages_Test, KnownCharacters)
{
    EXPECT_EQ(ByteToCodePoint(0x80, CodePage::Cp866), U'А');
    EXPECT_EQ(ByteToCodePoint(0xE0, CodePage::Cp866), U'р');
    EXPECT_EQ(ByteToCodePoint(0xF0, CodePage::Cp866), U'Ё');
    EXPECT_EQ(ByteToCodePoint(0xC1, CodePage::Koi8r), U'а');
    EXPECT_EQ(ByteToCodePoint(0xE1, CodePage::Koi8r), U'А');
    EXPECT_EQ(ByteToCodePoint(0xC0, CodePage::Cp1251), U'А');
    EXPECT_EQ(ByteToCodePoint(0xFF, CodePage::Cp1251), U'я');
    EXPECT_EQ(ByteToCodePoint(0x98, CodePage::Cp1251), 0xF798u) << "undefined in CP1251: kept as U+F700 + byte";
    EXPECT_EQ(ByteToCodePoint(0x60, CodePage::ZxSpectrum), U'£');
    EXPECT_EQ(ByteToCodePoint(0x5E, CodePage::ZxSpectrum), U'↑');
    EXPECT_EQ(ByteToCodePoint(0x7F, CodePage::ZxSpectrum), U'©');
    EXPECT_EQ(ByteToCodePoint(0x8F, CodePage::ZxSpectrum), U'█');
    EXPECT_EQ(ByteToCodePoint(0x83, CodePage::ZxSpectrum), U'▀') << "top two quadrants";
}

TEST(CodePages_Test, Utf8ConversionBothWays)
{
    const std::vector<uint8_t> cp866 = {0x8F, 0xE0, 0xA8, 0xA2, 0xA5, 0xE2};   // "Привет"
    EXPECT_EQ(ToUtf8(cp866, CodePage::Cp866), "Привет");
    std::vector<uint8_t> back;
    std::string error;
    ASSERT_TRUE(FromUtf8("Привет", CodePage::Cp866, back, error)) << error;
    EXPECT_EQ(back, cp866);

    EXPECT_FALSE(FromUtf8("€", CodePage::Cp866, back, error)) << "no euro sign in CP866";
    EXPECT_NE(error.find("U+20AC"), std::string::npos) << error;
    EXPECT_EQ(back, std::vector<uint8_t>{'?'});
}

TEST(CodePages_Test, InvalidUtf8BytesSurvive)
{
    const std::vector<uint8_t> bytes = {'A', 0xFF, 0xC3, 'B'};   // 0xFF invalid, 0xC3 truncated
    size_t invalid = 0;
    const std::string text = ToUtf8(bytes, CodePage::Utf8, &invalid);
    EXPECT_EQ(invalid, 2u);
    std::vector<uint8_t> back;
    std::string error;
    ASSERT_TRUE(FromUtf8(text, CodePage::Utf8, back, error)) << error;
    EXPECT_EQ(back, bytes);
}

TEST(CodePages_Test, Names)
{
    CodePage page;
    EXPECT_TRUE(ParseCodePage("KOI8-R", page));
    EXPECT_EQ(page, CodePage::Koi8r);
    EXPECT_TRUE(ParseCodePage("windows-1251", page));
    EXPECT_EQ(page, CodePage::Cp1251);
    EXPECT_TRUE(ParseCodePage("866", page));
    EXPECT_EQ(page, CodePage::Cp866);
    EXPECT_FALSE(ParseCodePage("latin1", page));
    EXPECT_EQ(CodePageName(CodePage::Utf8), "utf-8");
    EXPECT_EQ(LineEndBytes(LineEnd::CrLf), "\r\n");
}
