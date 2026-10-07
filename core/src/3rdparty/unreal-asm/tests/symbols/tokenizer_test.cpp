// The shared lexer of the text symbol formats: every number notation, identifiers, strings, comments, columns

#include <gtest/gtest.h>

#include "symbols/codecs/text/tokenizer.h"

using namespace unrealasm::symbols::text;

TEST(Tokenizer_Test, Numbers)
{
    const std::pair<const char*, int64_t> cases[] = {{"1234", 1234}, {"#C000", 0xC000}, {"$C000", 0xC000}, {"0xC000", 0xC000}, {"C000h", -1},
                                                     {"0C000H", 0xC000}, {"%1010", 10}, {"0b1010", 10}, {"1010b", 10}, {"0800EH", 0x800E},
                                                     {"0x0000C010", 0xC010}, {"12", 12}};
    for (const auto& [text, expected] : cases)
    {
        int64_t value = 0;
        const bool ok = ParseNumber(text, value);
        if (expected < 0)
            EXPECT_FALSE(ok) << text;   // a hex number must start with a digit: C000h is a name
        else
        {
            ASSERT_TRUE(ok) << text;
            EXPECT_EQ(value, expected) << text;
        }
    }
    int64_t value = 0;
    EXPECT_FALSE(ParseNumber("17q", value));
    EXPECT_TRUE(ParseNumber("17q", value, true));
    EXPECT_EQ(value, 15);
    EXPECT_FALSE(ParseNumber("0x1FFFFFFFF", value));   // past 32 bits
}

TEST(Tokenizer_Test, Lines)
{
    const auto t = Tokenize("PRINT-A-1: EQU #0010 ; \"comment\"");
    ASSERT_EQ(t.size(), 9u);                       // PRINT - A - 1 : EQU #0010 ;...
    EXPECT_EQ(t[0].kind, TokenKind::Ident);
    EXPECT_EQ(t[0].text, "PRINT");                 // '-' is punctuation by default
    EXPECT_EQ(t[1].text, "-");
    EXPECT_EQ(t[5].kind, TokenKind::Punct);        // ':'
    EXPECT_EQ(t[6].kind, TokenKind::Ident);        // EQU
    EXPECT_EQ(t[7].kind, TokenKind::Number);
    EXPECT_EQ(t[7].value, 0x10);
    EXPECT_EQ(t[7].column, 16u);
    EXPECT_EQ(t[8].kind, TokenKind::Comment);
    const auto c = Tokenize("x 'a;b' ; rest");
    ASSERT_EQ(c.size(), 3u);
    EXPECT_EQ(c[1].kind, TokenKind::String);       // ';' inside a string is no comment
    EXPECT_EQ(c[1].text, "'a;b'");
    EXPECT_EQ(c[2].kind, TokenKind::Comment);
    TokenizerOptions options;
    options.identExtra = "_.?!@-";
    options.slashComment = true;
    const auto d = Tokenize("PRINT-A-1 // note", options);
    ASSERT_EQ(d.size(), 2u);
    EXPECT_EQ(d[0].text, "PRINT-A-1");
    EXPECT_EQ(d[1].kind, TokenKind::Comment);
}
