#include <gtest/gtest.h>
#include "3rdparty/simpleini/simpleini.h"
#include <cstring>
#include <string>

/**
 * SimpleINI Unit Tests
 * 
 * These tests verify that our modifications to SimpleINI correctly handle
 * inline comments in INI values. This is critical for correct parameter parsing.
 * 
 * If any of these tests fail after a SimpleINI update, it means the inline
 * comment stripping logic needs to be re-applied.
 */

class SimpleINITest : public ::testing::Test {
protected:
    CSimpleIniA ini;
    
    void SetUp() override {
        ini.SetUnicode();
    }
    
    // Helper to load INI from string
    bool LoadFromString(const char* iniContent) {
        return ini.LoadData(iniContent, strlen(iniContent)) == SI_OK;
    }
};

// Test: Semicolon inline comment stripping
TEST_F(SimpleINITest, StripsSemicolonInlineComments) {
    const char* iniData = 
        "[test]\n"
        "value=123 ; this is a comment\n";
    
    ASSERT_TRUE(LoadFromString(iniData)) 
        << "Failed to load test INI data";
    
    const char* value = ini.GetValue("test", "value", nullptr);
    ASSERT_NE(value, nullptr) 
        << "Value not found in INI";
    
    EXPECT_STREQ(value, "123") 
        << "\n"
        << "============================================================\n"
        << "SIMPLEINI INLINE COMMENT STRIPPING FAILURE\n"
        << "============================================================\n"
        << "Expected: '123'\n"
        << "Got: '" << value << "'\n"
        << "\n"
        << "The semicolon (;) inline comment was NOT stripped.\n"
        << "\n"
        << "FIX REQUIRED in simpleini.h:\n"
        << "\n"
        << "LOCATION: In the LoadEntry() function, find this code:\n"
        << "\n"
        << "  // find the end of the value which is the end of this line\n"
        << "  a_pVal = a_pData;\n"
        << "  while (*a_pData && !IsNewLineChar(*a_pData)) {\n"
        << "      ++a_pData;\n"
        << "  }\n"
        << "\n"
        << "AFTER the above code block, BEFORE the 'remove trailing spaces'\n"
        << "section, ADD this inline comment stripping logic:\n"
        << "\n"
        << "  // strip inline comments (';', '#', or '//') from the value\n"
        << "  // search backwards from end of line to find comment start\n"
        << "  pTrail = a_pData - 1;\n"
        << "  while (pTrail >= a_pVal) {\n"
        << "      if (*pTrail == ';' || *pTrail == '#') {\n"
        << "          // found single-char comment marker\n"
        << "          a_pData = pTrail;\n"
        << "          break;\n"
        << "      }\n"
        << "      // check for '//' comment (need two consecutive slashes)\n"
        << "      if (*pTrail == '/' && pTrail > a_pVal && *(pTrail - 1) == '/') {\n"
        << "          // found '//' comment marker\n"
        << "          a_pData = pTrail - 1;  // point to first '/'\n"
        << "          break;\n"
        << "      }\n"
        << "      --pTrail;\n"
        << "  }\n"
        << "\n"
        << "This should be placed BEFORE this existing code:\n"
        << "\n"
        << "  // remove trailing spaces from the value\n"
        << "  pTrail = a_pData - 1;\n"
        << "  if (*a_pData) { // prepare for the next round\n"
        << "      SkipNewLine(a_pData);\n"
        << "  }\n"
        << "============================================================\n";
}

// Test: Hash inline comment stripping
TEST_F(SimpleINITest, StripsHashInlineComments) {
    const char* iniData = 
        "[test]\n"
        "value=456 # this is a hash comment\n";
    
    ASSERT_TRUE(LoadFromString(iniData));
    
    const char* value = ini.GetValue("test", "value", nullptr);
    ASSERT_NE(value, nullptr);
    
    EXPECT_STREQ(value, "456") 
        << "\n"
        << "============================================================\n"
        << "SIMPLEINI INLINE COMMENT STRIPPING FAILURE\n"
        << "============================================================\n"
        << "The hash (#) inline comment was NOT stripped.\n"
        << "See fix instructions in the semicolon test above.\n"
        << "============================================================\n";
}

// Test: Double-slash inline comment stripping
TEST_F(SimpleINITest, StripsDoubleSlashInlineComments) {
    const char* iniData = 
        "[test]\n"
        "value=789 // this is a C++ style comment\n";
    
    ASSERT_TRUE(LoadFromString(iniData));
    
    const char* value = ini.GetValue("test", "value", nullptr);
    ASSERT_NE(value, nullptr);
    
    EXPECT_STREQ(value, "789") 
        << "\n"
        << "============================================================\n"
        << "SIMPLEINI INLINE COMMENT STRIPPING FAILURE\n"
        << "============================================================\n"
        << "The double-slash (//) inline comment was NOT stripped.\n"
        << "See fix instructions in the semicolon test above.\n"
        << "============================================================\n";
}

// Test: Trailing whitespace removal
TEST_F(SimpleINITest, StripsTrailingWhitespace) {
    const char* iniData = 
        "[test]\n"
        "value=abc   \n";
    
    ASSERT_TRUE(LoadFromString(iniData));
    
    const char* value = ini.GetValue("test", "value", nullptr);
    ASSERT_NE(value, nullptr);
    
    EXPECT_STREQ(value, "abc") 
        << "Trailing whitespace was not removed (expected: 'abc', got: '" << value << "')";
}

// Test: Combined whitespace and comment stripping
TEST_F(SimpleINITest, StripsBothWhitespaceAndComments) {
    const char* iniData = 
        "[test]\n"
        "value=hello    ; comment with leading spaces\n";
    
    ASSERT_TRUE(LoadFromString(iniData));
    
    const char* value = ini.GetValue("test", "value", nullptr);
    ASSERT_NE(value, nullptr);
    
    EXPECT_STREQ(value, "hello") 
        << "Both whitespace and comments should be stripped (got: '" << value << "')";
}

// Test: GetLongValue with inline comments
TEST_F(SimpleINITest, GetLongValueHandlesInlineComments) {
    const char* iniData = 
        "[ULA]\n"
        "intlen=128    ; int length in t-states\n";
    
    ASSERT_TRUE(LoadFromString(iniData));
    
    long value = ini.GetLongValue("ULA", "intlen", -1);
    
    EXPECT_EQ(value, 128) 
        << "\n"
        << "============================================================\n"
        << "CRITICAL: GetLongValue PARSING FAILURE\n"
        << "============================================================\n"
        << "Expected: 128\n"
        << "Got: " << value << "\n"
        << "\n"
        << "This is the exact bug that broke intlen parameter reading!\n"
        << "The inline comment prevents strtol() from parsing the number.\n"
        << "\n"
        << "IMPACT: All numeric INI parameters with inline comments will\n"
        << "return default values instead of configured values.\n"
        << "\n"
        << "See fix instructions in the semicolon test above.\n"
        << "============================================================\n";
}

// Test: Value without comments remains unchanged
TEST_F(SimpleINITest, PreservesValuesWithoutComments) {
    const char* iniData = 
        "[test]\n"
        "value=normalvalue\n";
    
    ASSERT_TRUE(LoadFromString(iniData));
    
    const char* value = ini.GetValue("test", "value", nullptr);
    ASSERT_NE(value, nullptr);
    
    EXPECT_STREQ(value, "normalvalue") 
        << "Values without comments should be preserved exactly";
}

// Test: Multiple comment types in one file
TEST_F(SimpleINITest, HandlesMultipleCommentTypes) {
    const char* iniData = 
        "[test]\n"
        "a=1 ; semicolon\n"
        "b=2 # hash\n"
        "c=3 // slash\n"
        "d=4\n";
    
    ASSERT_TRUE(LoadFromString(iniData));
    
    EXPECT_STREQ(ini.GetValue("test", "a", nullptr), "1");
    EXPECT_STREQ(ini.GetValue("test", "b", nullptr), "2");
    EXPECT_STREQ(ini.GetValue("test", "c", nullptr), "3");
    EXPECT_STREQ(ini.GetValue("test", "d", nullptr), "4");
}

// Test: Empty value with comment
TEST_F(SimpleINITest, HandlesEmptyValueWithComment) {
    const char* iniData = 
        "[test]\n"
        "empty= ; just a comment\n";
    
    ASSERT_TRUE(LoadFromString(iniData));
    
    const char* value = ini.GetValue("test", "empty", nullptr);
    ASSERT_NE(value, nullptr);
    
    // Should be empty string after stripping comment and whitespace
    EXPECT_STREQ(value, "") 
        << "Empty value with only comment should result in empty string";
}
