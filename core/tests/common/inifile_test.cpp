#include "pch.h"

#include "common/inifile.h"

#include "common/filehelper.h"

#include "_helpers/testpathhelper.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

/// The compatibility rules asserted here mirror the SimpleIni behavior the
/// emulator shipped with (see the class comment in common/inifile.h) - the
/// heritage "unreal.ini" machine configs and the persisted features.ini must
/// keep parsing exactly the same way after the parser swap.

class IniFile_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

/// region <Parsing>

TEST_F(IniFile_Test, ParsesSectionsAndKeys)
{
    IniFile ini;
    ini.LoadData("[misc]\nShareCPU=1\nRESET=DOS\n");

    EXPECT_STREQ(ini.GetValue("misc", "ShareCPU"), "1");
    EXPECT_STREQ(ini.GetValue("misc", "RESET"), "DOS");
    EXPECT_EQ(ini.GetAllSections().size(), 1u);
}

TEST_F(IniFile_Test, TrimsWhitespaceAndHandlesCrlf)
{
    IniFile ini;
    ini.LoadData("[ misc ]\r\n  ShareCPU  =  1  \r\n  mode = \r\n");

    EXPECT_STREQ(ini.GetValue("misc", "ShareCPU"), "1");
    // Present-but-empty value: not nullptr, empty string
    EXPECT_STREQ(ini.GetValue("misc", "mode"), "");
    EXPECT_FALSE(ini.GetValue("misc", "mode") == nullptr);
}

TEST_F(IniFile_Test, AcceptsUtf8Bom)
{
    IniFile ini;
    ini.LoadData("\xEF\xBB\xBF[misc]\nkey=value\n");

    EXPECT_STREQ(ini.GetValue("misc", "key"), "value");
}

TEST_F(IniFile_Test, SkipsFullLineComments)
{
    IniFile ini;
    ini.LoadData("; leading comment\n# other comment style\n[misc]\n; between keys\nkey=value\n");

    EXPECT_STREQ(ini.GetValue("misc", "key"), "value");
}

TEST_F(IniFile_Test, StripsInlineCommentsInValues)
{
    // The shipped parser carried a local SimpleIni patch: a backward scan from
    // end of line truncates the value at the first ';', '#' or '//' marker -
    // "intlen=128 ; t-states" must parse as 128 (see GetLongValueHandlesInlineComments)
    IniFile ini;
    ini.LoadData("[misc]\nShareCPU=1      ; 1 - only for fast CPUs\nmode=2 # hash style\npath=3 // C++ style\nplain=normalvalue\nempty= ; just a comment\n");

    EXPECT_STREQ(ini.GetValue("misc", "ShareCPU"), "1");
    EXPECT_STREQ(ini.GetValue("misc", "mode"), "2");
    EXPECT_STREQ(ini.GetValue("misc", "path"), "3");
    EXPECT_STREQ(ini.GetValue("misc", "plain"), "normalvalue");
    // Present-but-empty after stripping: not nullptr, empty string
    EXPECT_STREQ(ini.GetValue("misc", "empty"), "");
    EXPECT_FALSE(ini.GetValue("misc", "empty") == nullptr);
}

TEST_F(IniFile_Test, InlineCommentBackwardScanFindsLastMarker)
{
    IniFile ini;
    ini.LoadData("[misc]\nmixed=1 ; x # y\ncapture=video#.avi\n");

    // The scan starts at the end of the line, so the LAST marker wins and
    // earlier markers stay part of the value
    EXPECT_STREQ(ini.GetValue("misc", "mixed"), "1 ; x");
    // Shipped behavior: '#' inside a real value truncates it too - kept for
    // compatibility (spectrum3 unreal.ini: "ffmpeg.vout=video#.avi" -> "video")
    EXPECT_STREQ(ini.GetValue("misc", "capture"), "video");
}

TEST_F(IniFile_Test, HeritageHeaderYieldsStarSection)
{
    // First lines of the shipped unreal.ini configs: "[*] unreal speccy
    // configuration file" is a section named "*" (name ends at the first ']',
    // the rest of the line is ignored), and the version key lives in it
    IniFile ini;
    ini.LoadData("[*] unreal speccy configuration file\n\nUNREAL=0.37.9   ; make sure you don't have old INI version\n\n[MISC]\nShareCPU=1\n");

    EXPECT_STREQ(ini.GetValue("*", "UNREAL"), "0.37.9");
    EXPECT_STREQ(ini.GetValue("MISC", "ShareCPU"), "1");
}

TEST_F(IniFile_Test, SkipsMalformedLinesWithoutLosingSection)
{
    IniFile ini;
    ini.LoadData("[misc]\nplain text without assignment\n= empty key is invalid\n[broken header without close\nkey=value\n");

    // The invalid header line must not switch the current section away from [misc]
    EXPECT_STREQ(ini.GetValue("misc", "key"), "value");
}

TEST_F(IniFile_Test, LooksUpCaseInsensitively)
{
    IniFile ini;
    ini.LoadData("[MISC]\nShareCPU=1\n");

    EXPECT_STREQ(ini.GetValue("misc", "sharecpu"), "1");
    EXPECT_STREQ(ini.GetValue("Misc", "SHARECPU"), "1");
}

TEST_F(IniFile_Test, MergesDuplicateSectionsAndLastKeyWins)
{
    IniFile ini;
    ini.LoadData("[misc]\na=1\n[b]\nx=2\n[MISC]\na=3\nb=4\n");

    EXPECT_STREQ(ini.GetValue("misc", "a"), "3");
    EXPECT_STREQ(ini.GetValue("misc", "b"), "4");
    EXPECT_EQ(ini.GetAllSections().size(), 2u);
}

TEST_F(IniFile_Test, KeysBeforeFirstHeaderGoToRootSection)
{
    IniFile ini;
    ini.LoadData("loose=1\n[misc]\nkey=value\n");

    EXPECT_STREQ(ini.GetValue("", "loose"), "1");
}

/// endregion </Parsing>

/// region <Parsing corner / negative cases>

TEST_F(IniFile_Test, SectionNameStopsAtFirstCloseBracket)
{
    // "name ends at the first ']'" (class comment) - a second ']' later on
    // the same header line must not confuse the section name
    IniFile ini;
    ini.LoadData("[a]stray text ] more ]\nkey=value\n");

    EXPECT_STREQ(ini.GetValue("a", "key"), "value");
    EXPECT_EQ(ini.GetAllSections().size(), 1u);
}

TEST_F(IniFile_Test, AssignmentSplitsAtFirstEqualsSign)
{
    // The key/value split uses the FIRST '=' on the line - a value that
    // itself contains '=' must be kept whole, not re-split
    IniFile ini;
    ini.LoadData("[a]\nkey=a=b=c\n");

    EXPECT_STREQ(ini.GetValue("a", "key"), "a=b=c");
}

TEST_F(IniFile_Test, EmptyKeyLineIsSkipped)
{
    // "= empty key is invalid" (class comment): a line whose key half is
    // empty after trimming must be dropped, not stored under key ""
    IniFile ini;
    ini.LoadData("[a]\n= no key here\n   = also no key\nkey=value\n");

    EXPECT_STREQ(ini.GetValue("a", "key"), "value");
    EXPECT_EQ(ini.GetValue("a", ""), nullptr);
}

TEST_F(IniFile_Test, WhitespaceOnlyLineDoesNotBreakParsing)
{
    IniFile ini;
    ini.LoadData("[a]\nfirst=1\n   \t  \nsecond=2\n");

    EXPECT_STREQ(ini.GetValue("a", "first"), "1");
    EXPECT_STREQ(ini.GetValue("a", "second"), "2");
}

TEST_F(IniFile_Test, WhitespaceOnlySectionNameMergesWithRootSection)
{
    // A header that trims to an empty name ("[ ]") is indistinguishable from
    // the unnamed root section ("") once folded - documenting this rather
    // than leaving it to be discovered by accident
    IniFile ini;
    ini.LoadData("loose=1\n[   ]\nkey=value\n");

    EXPECT_STREQ(ini.GetValue("", "loose"), "1");
    EXPECT_STREQ(ini.GetValue("", "key"), "value");
    EXPECT_EQ(ini.GetAllSections().size(), 1u);
}

TEST_F(IniFile_Test, EmbeddedNulByteInLineDoesNotCrash)
{
    // std::string is 8-bit-clean; a value with an embedded NUL parses
    // without crashing, even though a c_str() consumer downstream would see
    // a value truncated at the NUL - documented tolerance, not a promise
    // that the NUL survives round-trip through the C API
    IniFile ini;
    std::string data = "[a]\nkey=va";
    data.push_back('\0');
    data += "lue\nnext=ok\n";

    EXPECT_NO_THROW(ini.LoadData(data));
    EXPECT_STREQ(ini.GetValue("a", "next"), "ok");
}

/// endregion </Parsing corner / negative cases>

/// region <Typed getters>

TEST_F(IniFile_Test, GetLongValueParsesDecimalAndHex)
{
    IniFile ini;
    ini.LoadData("[a]\ndec=42\nneg=-5\nhex=0x1F\nHEXPREFIX=0X10\nzero=0\n");

    EXPECT_EQ(ini.GetLongValue("a", "dec"), 42);
    EXPECT_EQ(ini.GetLongValue("a", "neg"), -5);
    EXPECT_EQ(ini.GetLongValue("a", "hex"), 31);
    EXPECT_EQ(ini.GetLongValue("a", "HEXPREFIX"), 16);
    EXPECT_EQ(ini.GetLongValue("a", "zero"), 0);
}

TEST_F(IniFile_Test, GetLongValueFallsBackToDefault)
{
    IniFile ini;
    ini.LoadData("[a]\nempty=\njunk=abc\nbare=0x\n");

    EXPECT_EQ(ini.GetLongValue("a", "absent", 7), 7);
    EXPECT_EQ(ini.GetLongValue("a", "empty", 7), 7);
    EXPECT_EQ(ini.GetLongValue("a", "junk", 7), 7);
    EXPECT_EQ(ini.GetLongValue("a", "bare", 7), 7);
}

TEST_F(IniFile_Test, GetLongValueHandlesInlineComments)
{
    // Regression guard inherited from the old simpleini_test.cpp: an inline
    // comment used to defeat strtol and silently feed the caller the default
    // (this exact case broke intlen reading on heritage configs)
    IniFile ini;
    ini.LoadData("[ULA]\nintlen=128    ; int length in t-states\n");

    EXPECT_EQ(ini.GetLongValue("ULA", "intlen", -1), 128);
}

TEST_F(IniFile_Test, GetDoubleValueRules)
{
    IniFile ini;
    ini.LoadData("[a]\nx=3.5\ny=-0.25\nempty=\njunk=abc\n");

    EXPECT_DOUBLE_EQ(ini.GetDoubleValue("a", "x"), 3.5);
    EXPECT_DOUBLE_EQ(ini.GetDoubleValue("a", "y"), -0.25);
    EXPECT_DOUBLE_EQ(ini.GetDoubleValue("a", "absent", 1.5), 1.5);
    EXPECT_DOUBLE_EQ(ini.GetDoubleValue("a", "empty", 1.5), 1.5);
    EXPECT_DOUBLE_EQ(ini.GetDoubleValue("a", "junk", 1.5), 1.5);
}

TEST_F(IniFile_Test, GetValueReturnsDefaultWhenAbsent)
{
    IniFile ini;
    ini.LoadData("[a]\nk=v\n");

    EXPECT_EQ(ini.GetValue("a", "missing"), nullptr);
    EXPECT_EQ(ini.GetValue("nosection", "k"), nullptr);
    EXPECT_STREQ(ini.GetValue("a", "missing", "fallback"), "fallback");
}

/// endregion </Typed getters>

/// region <Typed getter corner / negative cases>

TEST_F(IniFile_Test, GetLongValueAcceptsLeadingPlusSign)
{
    IniFile ini;
    ini.LoadData("[a]\nplus=+42\n");

    EXPECT_EQ(ini.GetLongValue("a", "plus"), 42);
}

TEST_F(IniFile_Test, GetLongValueHexIsCaseInsensitiveInDigitsAndPrefix)
{
    IniFile ini;
    ini.LoadData("[a]\nlower=0x1f\nupperPrefix=0X1f\nupperDigits=0x1F\n");

    EXPECT_EQ(ini.GetLongValue("a", "lower"), 31);
    EXPECT_EQ(ini.GetLongValue("a", "upperPrefix"), 31);
    EXPECT_EQ(ini.GetLongValue("a", "upperDigits"), 31);
}

TEST_F(IniFile_Test, GetLongValueHexWithTrailingJunkFallsBack)
{
    IniFile ini;
    ini.LoadData("[a]\nbadhex=0x1Fg\n");

    EXPECT_EQ(ini.GetLongValue("a", "badhex", 7), 7);
}

TEST_F(IniFile_Test, GetLongValueRejectsNegativeHexPrefix)
{
    // '-' takes the decimal branch (only a leading '0' selects hex); "-0x1F"
    // parses as decimal "-0" then chokes on the trailing "x1F" - documenting
    // that a negative hex literal is not supported, same as the shipped
    // parser this replaces
    IniFile ini;
    ini.LoadData("[a]\nneghex=-0x1F\n");

    EXPECT_EQ(ini.GetLongValue("a", "neghex", 7), 7);
}

TEST_F(IniFile_Test, GetLongValueRejectsInternalWhitespace)
{
    // Outer whitespace is trimmed by the line parser; whitespace INSIDE the
    // remaining value is trailing garbage as far as strtol is concerned
    IniFile ini;
    ini.LoadData("[a]\nspaced=4 2\n");

    EXPECT_EQ(ini.GetLongValue("a", "spaced", 7), 7);
}

TEST_F(IniFile_Test, GetLongValueOverflowClampsInsteadOfFallingBack)
{
    // Documented contract is "not fully numeric -> default"; a value that IS
    // fully numeric but doesn't fit in `long` is not covered by that rule -
    // strtol clamps to LONG_MIN/LONG_MAX and reports full consumption
    // (errno is not consulted). Pinned down here so a future change to this
    // behavior is a deliberate decision, not an accidental one.
    IniFile ini;
    ini.LoadData("[a]\nhuge=99999999999999999999999999\nhugeneg=-99999999999999999999999999\n");

    EXPECT_EQ(ini.GetLongValue("a", "huge", 7), std::numeric_limits<long>::max());
    EXPECT_EQ(ini.GetLongValue("a", "hugeneg", 7), std::numeric_limits<long>::min());
}

TEST_F(IniFile_Test, GetDoubleValueParsesExponentNotation)
{
    IniFile ini;
    ini.LoadData("[a]\nsci=1.5e3\nnegsci=-2E-2\n");

    EXPECT_DOUBLE_EQ(ini.GetDoubleValue("a", "sci"), 1500.0);
    EXPECT_DOUBLE_EQ(ini.GetDoubleValue("a", "negsci"), -0.02);
}

TEST_F(IniFile_Test, GetDoubleValueRejectsInternalWhitespace)
{
    IniFile ini;
    ini.LoadData("[a]\nspaced=1.5 2.5\n");

    EXPECT_DOUBLE_EQ(ini.GetDoubleValue("a", "spaced", 9.0), 9.0);
}

TEST_F(IniFile_Test, GetDoubleValueAcceptsStrtodSpecialTokens)
{
    // strtod's C99/C++11 grammar accepts "nan"/"inf"/"infinity" (any case) as
    // valid, fully-consumed numbers - GetDoubleValue has no extra filtering
    // on top, so a config value spelled this way is NOT rejected as "junk".
    // Pinned down so this is a known, intentional characteristic rather than
    // a surprise the next time someone touches this function.
    IniFile ini;
    ini.LoadData("[a]\nnotanumber=nan\ninfinite=inf\n");

    EXPECT_TRUE(std::isnan(ini.GetDoubleValue("a", "notanumber", 0.0)));
    EXPECT_TRUE(std::isinf(ini.GetDoubleValue("a", "infinite", 0.0)));
}

/// endregion </Typed getter corner / negative cases>

/// region <Mutation / serialization>

TEST_F(IniFile_Test, SetValueCreatesSectionsAndReplacesKeys)
{
    IniFile ini;
    ini.SetValue("hud", "state", "on");
    ini.SetValue("hud", "mode", "");
    ini.SetValue("fasttape", "state", "off");
    ini.SetValue("HUD", "STATE", "off"); // case-insensitive replace, first spelling kept

    EXPECT_STREQ(ini.GetValue("hud", "state"), "off");
    EXPECT_STREQ(ini.GetValue("hud", "mode"), "");

    const std::vector<std::string> sections = ini.GetAllSections();
    EXPECT_EQ(sections.size(), 2u);
    EXPECT_EQ(sections[0], "hud");
    EXPECT_EQ(sections[1], "fasttape");
}

TEST_F(IniFile_Test, SaveDataEmitsCanonicalFormat)
{
    IniFile ini;
    ini.SetValue("a", "k", "v");
    ini.SetValue("b", "x", "1");

    EXPECT_EQ(ini.SaveData(), "[a]\nk = v\n\n[b]\nx = 1\n");
}

TEST_F(IniFile_Test, RoundTripPreservesData)
{
    const std::string original = "[*] unreal speccy configuration file\nUNREAL=0.37.9\n\n[MISC]\nShareCPU=1\n";

    IniFile ini;
    ini.LoadData(original);
    const std::string saved = ini.SaveData();

    IniFile reloaded;
    reloaded.LoadData(saved);

    EXPECT_STREQ(reloaded.GetValue("*", "UNREAL"), "0.37.9");
    EXPECT_STREQ(reloaded.GetValue("MISC", "ShareCPU"), "1");
    EXPECT_EQ(reloaded.SaveData(), saved);
}

TEST_F(IniFile_Test, SaveLoadFileRoundTrip)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("inifile_test.ini");

    IniFile ini;
    ini.SetValue("misc", "ShareCPU", "1");
    ini.SetValue("misc", "RESET", "DOS");
    ASSERT_TRUE(ini.SaveFile(path));

    IniFile reloaded;
    ASSERT_TRUE(reloaded.LoadFile(path));
    EXPECT_STREQ(reloaded.GetValue("misc", "ShareCPU"), "1");
    EXPECT_STREQ(reloaded.GetValue("misc", "RESET"), "DOS");
}

TEST_F(IniFile_Test, LoadFileReportsMissingFile)
{
    IniFile ini;
    EXPECT_FALSE(ini.LoadFile(TestPathHelper::GetUniqueTestScratchPath("inifile_missing.ini")));
    EXPECT_TRUE(ini.IsEmpty());
}

TEST_F(IniFile_Test, EmptyInputYieldsNoSections)
{
    IniFile ini;
    ini.LoadData("");
    EXPECT_TRUE(ini.IsEmpty());

    ini.LoadData("; comment only\n\n");
    EXPECT_TRUE(ini.IsEmpty());
}

/// endregion </Mutation / serialization>

/// region <Extreme sizes: politely decline, no crash, keep parsing>
//
// These pin the kMaxLineLength guard (class comment in inifile.h): a single
// oversized line must be skipped without allocating a copy of it - the point
// is not "does it eventually succeed on a huge line" but "does the file's
// OTHER lines still parse, fast, regardless of how large one bad line is".
// None of these should take measurably longer than the constant-size tests
// above - if one does, the guard isn't short-circuiting before the copy.

TEST_F(IniFile_Test, LineAtMaxLengthStillParses)
{
    // key= (4 bytes) + value filling the rest of the budget exactly
    const std::string value(IniFile::kMaxLineLength - 4, 'v');
    IniFile ini;
    ini.LoadData("[a]\nkey=" + value + "\n");

    EXPECT_EQ(ini.GetValue("a", "key"), value);
}

TEST_F(IniFile_Test, LineOneByteOverMaxLengthIsSkippedButFileKeepsParsing)
{
    // "skip only invalid lines": a file with a valid line, then one line
    // exactly one byte over budget, then another valid line - only the
    // middle one must be dropped.
    const std::string tooLong(IniFile::kMaxLineLength - 3, 'v');  // "key=" + this is one byte over
    IniFile ini;
    ini.LoadData("[a]\nbefore=1\nkey=" + tooLong + "\nafter=2\n");

    EXPECT_STREQ(ini.GetValue("a", "before"), "1");
    EXPECT_EQ(ini.GetValue("a", "key"), nullptr);
    EXPECT_STREQ(ini.GetValue("a", "after"), "2");
}

TEST_F(IniFile_Test, ExtremelyLongUnterminatedLineDoesNotCrashOrHang)
{
    // Tens of MB, no newline anywhere - the pathological case for a
    // line-oriented parser with no line-length guard. Must decline instantly
    // rather than allocate a same-sized copy.
    const std::string huge(64 * 1024 * 1024, 'a');

    IniFile ini;
    EXPECT_NO_THROW(ini.LoadData(huge));
    EXPECT_TRUE(ini.IsEmpty());  // never a valid header or assignment - and now also over budget
}

TEST_F(IniFile_Test, ExtremelyLongKeyAloneIsDeclined)
{
    // Value is short; the KEY is what blows the per-line budget.
    const std::string hugeKey(2 * 1024 * 1024, 'k');
    IniFile ini;
    ini.LoadData("[a]\n" + hugeKey + "=v\nok=1\n");

    EXPECT_EQ(ini.GetValue("a", hugeKey.c_str()), nullptr);
    EXPECT_STREQ(ini.GetValue("a", "ok"), "1");
}

TEST_F(IniFile_Test, ExtremelyLongValueAloneIsDeclined)
{
    // Key is short; the VALUE is what blows the per-line budget.
    const std::string hugeValue(2 * 1024 * 1024, 'v');
    IniFile ini;
    ini.LoadData("[a]\nkey=" + hugeValue + "\nok=1\n");

    EXPECT_EQ(ini.GetValue("a", "key"), nullptr);
    EXPECT_STREQ(ini.GetValue("a", "ok"), "1");
}

TEST_F(IniFile_Test, ExtremelyLongSectionHeaderIsDeclinedWithoutLosingCurrentSection)
{
    // An oversized "[...]" header must not switch sections (same rule as an
    // unterminated header, SkipsMalformedLinesWithoutLosingSection) and must
    // not itself crash or allocate a huge copy.
    const std::string hugeHeaderName(2 * 1024 * 1024, 'h');
    IniFile ini;
    ini.LoadData("[a]\nfirst=1\n[" + hugeHeaderName + "]\nsecond=2\n");

    EXPECT_STREQ(ini.GetValue("a", "first"), "1");
    EXPECT_STREQ(ini.GetValue("a", "second"), "2");
    EXPECT_EQ(ini.GetAllSections().size(), 1u);
}

TEST_F(IniFile_Test, ManyConsecutiveExtremeLinesAreEachDeclinedIndependently)
{
    // Not a one-shot fluke: the decline path must keep working across many
    // oversized lines in a row, still finishing fast (the guard is O(1) per
    // line once the newline is found, not O(line length)).
    std::string data = "[a]\n";
    for (int i = 0; i < 50; i++)
    {
        data += std::string(3 * 1024 * 1024, 'x');
        data += '\n';
    }
    data += "ok=1\n";

    IniFile ini;
    EXPECT_NO_THROW(ini.LoadData(data));
    EXPECT_STREQ(ini.GetValue("a", "ok"), "1");
    EXPECT_EQ(ini.GetAllSections().size(), 1u);
}

TEST_F(IniFile_Test, ExtremelyLongLineViaLoadFileDoesNotCrash)
{
    // Same guard, exercised through the file-reading path (LoadFile reads
    // the whole file into memory first, same as LoadData sees it).
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("inifile_extreme_line.ini");
    {
        std::ofstream file(FileHelper::ToFsPath(path), std::ios::binary);
        ASSERT_TRUE(file.is_open());
        file << "[a]\nbefore=1\nkey=" << std::string(8 * 1024 * 1024, 'v') << "\nafter=2\n";
    }

    IniFile ini;
    EXPECT_NO_THROW(ASSERT_TRUE(ini.LoadFile(path)));
    EXPECT_STREQ(ini.GetValue("a", "before"), "1");
    EXPECT_EQ(ini.GetValue("a", "key"), nullptr);
    EXPECT_STREQ(ini.GetValue("a", "after"), "2");
}

/// endregion </Extreme sizes: politely decline, no crash, keep parsing>
