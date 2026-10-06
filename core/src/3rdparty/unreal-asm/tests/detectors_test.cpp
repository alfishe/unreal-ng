// The universal detectors (decision D-13): code page ranking on the same Russian text in every code page, line ends,
// text vs binary; short or letter-free input gives low confidence instead of a guess.

#include <gtest/gtest.h>

#include "testdata.h"
#include "unrealasm/encoding.h"

using namespace unrealasm::encoding;
using unrealasm::testing::ReadTestData;

TEST(Detectors_Test, CodePageOfTheSameTextInEveryPage)
{
    const CodePageDetector detector;
    struct Case { const char* file; CodePage expected; };
    for (const Case& c : {Case{"text/source-cp866-lf.asm", CodePage::Cp866}, Case{"text/source-koi8r-lf.asm", CodePage::Koi8r},
                          Case{"text/source-cp1251-lf.asm", CodePage::Cp1251}, Case{"text/source-utf8-lf.asm", CodePage::Utf8}})
    {
        const auto bytes = ReadTestData(c.file);
        ASSERT_FALSE(bytes.empty()) << c.file;
        const auto ranked = detector.Rank(bytes);
        EXPECT_EQ(ranked.front().codePage, c.expected) << c.file << ": got " << CodePageName(ranked.front().codePage);
        EXPECT_GE(ranked.front().confidence, 60) << c.file;
    }
}

TEST(Detectors_Test, AsciiAndShortInput)
{
    const CodePageDetector detector;
    const std::string ascii = "\tLD A,1\n\tRET\n";
    const auto best = detector.Best(std::vector<uint8_t>(ascii.begin(), ascii.end()));
    EXPECT_EQ(best.codePage, CodePage::Ascii);
    EXPECT_EQ(best.confidence, 100);

    const std::vector<uint8_t> two = {'A', 0xE0, 0xE1};   // two high bytes: too little evidence
    EXPECT_LT(detector.Best(two).confidence, 30);

    const std::vector<uint8_t> graphics = {0xB0, 0xB1, 0xB2, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF};   // pseudo-graphics only
    EXPECT_LE(detector.Best(graphics).confidence, 40);
}

TEST(Detectors_Test, LineEnds)
{
    const LineEndDetector detector;
    EXPECT_EQ(detector.Detect(ReadTestData("text/source-cp866-lf.asm")), LineEnd::Lf);
    EXPECT_EQ(detector.Detect(ReadTestData("text/source-cp866-crlf.asm")), LineEnd::CrLf);
    EXPECT_EQ(detector.Detect(ReadTestData("text/source-cp866-cr.asm")), LineEnd::Cr);
    EXPECT_EQ(detector.Detect(ReadTestData("text/mixed-ends.asm")), LineEnd::Mixed);
    EXPECT_EQ(detector.Detect(std::vector<uint8_t>{'a', 'b'}), LineEnd::None);
}

TEST(Detectors_Test, TextVersusBinary)
{
    const TextBinaryDetector detector;
    EXPECT_EQ(detector.TextScore(ReadTestData("text/source-cp866-lf.asm")), 100);
    std::vector<uint8_t> binary(256);
    for (size_t i = 0; i < binary.size(); ++i)
        binary[i] = static_cast<uint8_t>(i);
    EXPECT_LT(detector.TextScore(binary), 50);
    EXPECT_EQ(detector.TextScore({}), 100);
}
