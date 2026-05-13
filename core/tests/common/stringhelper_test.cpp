#include "pch.h"

#include "stringhelper_test.h"

#include "common/stringhelper.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <exception>

/// region <SetUp / TearDown>

void StringHelper_Test::SetUp()
{
}

void StringHelper_Test::TearDown()
{
}

/// endregion </SetUp / TearDown>

TEST_F(StringHelper_Test, Compare)
{
    std::wstring test[2][4] =
    {
        {
            L"TestString1",
            L"TestString2",
            L"test!string3  ",
            L"__123__Abc"
    },
        {
            L"TestString1",
            L"TestString2_",
            L"test!string3  ",
            L"__123__abc"
        }
    };

    for (int i = 0; i < sizeof(test[0]) / sizeof(test[0][0]); i++)
    {
        int result = StringHelper::Compare(test[0][i], test[1][i]);
        if (i % 2 == 0)
        {
            EXPECT_EQ(result, 0);
        }
        else
        {
            EXPECT_NE(result, 0);
        }
    }
}

TEST_F(StringHelper_Test, LTrim)
{
    // Original string contains both spaces and tabs
    std::string original = " \t    test string \t  ";
    std::string reference = "test string \t  ";

    std::string_view trimView = StringHelper::LTrim(original);
    std::string result = std::string(trimView);

    EXPECT_EQ(result, reference);

    if (StringHelper::Compare(result, reference) != 0)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
    }
}

TEST_F(StringHelper_Test, RTrim)
{
    // Original string contains both spaces and tabs
    std::string original = " \t    test string \t  ";
    std::string reference = " \t    test string";

    std::string_view trimView = StringHelper::RTrim(original);
    std::string result = std::string(trimView);

    EXPECT_EQ(result, reference);

    if (StringHelper::Compare(result, reference) != 0)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
    }
}

TEST_F(StringHelper_Test, Trim)
{
    // Original string contains both spaces and tabs
    std::string original = " \t    test string \t  ";
    std::string reference = "test string";

    std::string_view trimView = StringHelper::Trim(original);
    std::string result = std::string(trimView);

    EXPECT_EQ(result, reference);

    if (StringHelper::Compare(result, reference) != 0)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
    }
}

TEST_F(StringHelper_Test, FormatWithThousandsDelimiter)
{
    int64_t original = 123456789012;
    std::string reference = "123,456,789,012";

    std::string result = StringHelper::FormatWithThousandsDelimiter(original);

    EXPECT_EQ(result, reference);

    if (StringHelper::Compare(result, reference) != 0)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
    }
}

TEST_F(StringHelper_Test, StringToWideString)
{
    std::wstring reference = L"Test";

    std::wstring result = StringHelper::StringToWideString("Test");

    if (result != reference)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
    }
}

TEST_F(StringHelper_Test, WideStringToString)
{
    std::string reference = "Test";

    std::string result = StringHelper::WideStringToString(L"Test");

    if (result != reference)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
    }
}

TEST_F(StringHelper_Test, ReplaceAll)
{
    // Test case 1: Simple replacement
    {
        std::string original = "Hello World";
        std::string expected = "Hello Universe";
        std::string result = StringHelper::ReplaceAll(original, "World", "Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 2: Multiple replacements
    {
        std::string original = "Hello World World";
        std::string expected = "Hello Universe Universe";
        std::string result = StringHelper::ReplaceAll(original, "World", "Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 3: Case-sensitive replacement
    {
        std::string original = "Hello World";
        std::string expected = "Hello World";
        std::string result = StringHelper::ReplaceAll(original, "world", "Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 4: Empty string
    {
        std::string original = "";
        std::string expected = "";
        std::string result = StringHelper::ReplaceAll(original, "World", "Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 5: Empty replacement string
    {
        std::string original = "Hello World";
        std::string expected = "Hello";
        std::string result = StringHelper::ReplaceAll(original, " World", "");
        EXPECT_EQ(result, expected);
    }

    // Test case 6: Replace with empty string
    {
        std::string original = "Hello World";
        std::string expected = "Hello ";
        std::string result = StringHelper::ReplaceAll(original, "World", "");
        EXPECT_EQ(result, expected);
    }

    // Test case 7: Replace with empty string in empty string
    {
        std::string original = "";
        std::string expected = "";
        std::string result = StringHelper::ReplaceAll(original, "World", "");
        EXPECT_EQ(result, expected);
    }

    // Test case 8: Replace with empty string in string without match
    {
        std::string original = "Hello World";
        std::string expected = "Hello World";
        std::string result = StringHelper::ReplaceAll(original, "Universe", "");
        EXPECT_EQ(result, expected);
    }

    // Test case 9: Replace with empty string in string with multiple matches
    {
        std::string original = "Hello World World";
        std::string expected = "Hello  ";
        std::string result = StringHelper::ReplaceAll(original, "World", "");
        EXPECT_EQ(result, expected);
    }

    // Test case 10: Replace with string containing spaces
    {
        std::string original = "Hello World";
        std::string expected = "Hello   Universe";
        std::string result = StringHelper::ReplaceAll(original, "World", "  Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 11: Replace with string containing special characters
    {
        std::string original = "Hello World";
        std::string expected = "Hello Universe!";
        std::string result = StringHelper::ReplaceAll(original, "World", "Universe!");
        EXPECT_EQ(result, expected);
    }

    // Test case 12: Replace with string containing escape characters
    {
        std::string original = "Hello World";
        std::string expected = "Hello \n Universe";
        std::string result = StringHelper::ReplaceAll(original, "World", "\n Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 13: Replace with string containing actual newline
    {
        std::string original = "Hello World";
        std::string expected = "Hello \n Universe";
        std::string result = StringHelper::ReplaceAll(original, "World", "\n Universe");
        EXPECT_EQ(result, expected);
    }
}

TEST_F(StringHelper_Test, WideStringReplaceAll)
{
    // Test case 1: wstring simple replacement
    {
        std::wstring original = L"Hello World";
        std::wstring expected = L"Hello Universe";
        std::wstring result = StringHelper::ReplaceAll(original, L"World", L"Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 2: wstring multiple replacements
    {
        std::wstring original = L"Hello World World";
        std::wstring expected = L"Hello Universe Universe";
        std::wstring result = StringHelper::ReplaceAll(original, L"World", L"Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 3: wstring case-sensitive replacement
    {
        std::wstring original = L"Hello World";
        std::wstring expected = L"Hello World";
        std::wstring result = StringHelper::ReplaceAll(original, L"world", L"Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 4: wstring empty string
    {
        std::wstring original = L"";
        std::wstring expected = L"";
        std::wstring result = StringHelper::ReplaceAll(original, L"World", L"Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 5: wstring empty replacement string
    {
        std::wstring original = L"Hello World";
        std::wstring expected = L"Hello";
        std::wstring result = StringHelper::ReplaceAll(original, L" World", L"");
        EXPECT_EQ(result, expected);
    }

    // Test case 6: wstring replace with empty string
    {
        std::wstring original = L"Hello World";
        std::wstring expected = L"Hello ";
        std::wstring result = StringHelper::ReplaceAll(original, L"World", L"");
        EXPECT_EQ(result, expected);
    }

    // Test case 7: wstring replace with empty string in empty string
    {
        std::wstring original = L"";
        std::wstring expected = L"";
        std::wstring result = StringHelper::ReplaceAll(original, L"World", L"");
        EXPECT_EQ(result, expected);
    }

    // Test case 8: wstring replace with empty string in string without match
    {
        std::wstring original = L"Hello World";
        std::wstring expected = L"Hello World";
        std::wstring result = StringHelper::ReplaceAll(original, L"Universe", L"");
        EXPECT_EQ(result, expected);
    }

    // Test case 9: wstring replace with empty string in string with multiple matches
    {
        std::wstring original = L"Hello World World";
        std::wstring expected = L"Hello  ";
        std::wstring result = StringHelper::ReplaceAll(original, L"World", L"");
        EXPECT_EQ(result, expected);
    }

    // Test case 10: wstring replace with string containing spaces
    {
        std::wstring original = L"Hello World";
        std::wstring expected = L"Hello   Universe";
        std::wstring result = StringHelper::ReplaceAll(original, L"World", L"  Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 11: wstring replace with string containing special characters
    {
        std::wstring original = L"Hello World";
        std::wstring expected = L"Hello Universe!";
        std::wstring result = StringHelper::ReplaceAll(original, L"World", L"Universe!");
        EXPECT_EQ(result, expected);
    }

    // Test case 12: wstring replace with string containing escape characters
    {
        std::wstring original = L"Hello World";
        std::wstring expected = L"Hello \n Universe";
        std::wstring result = StringHelper::ReplaceAll(original, L"World", L"\n Universe");
        EXPECT_EQ(result, expected);
    }

    // Test case 13: wstring replace with string containing actual newline
    {
        std::wstring original = L"Hello World";
        std::wstring expected = L"Hello \n Universe";
        std::wstring result = StringHelper::ReplaceAll(original, L"World", L"\n Universe");
        EXPECT_EQ(result, expected);
    }
}

TEST_F(StringHelper_Test, Format)
{
    // Test with const char* format and const char* parameter
    {
        const char* format = "Hello, %s!";
        std::string result = StringHelper::Format(format, "World");
        std::string reference = "Hello, World!";
        EXPECT_EQ(reference, result) << "Test case 1 failed";
    }

    // Test with std::string format and std::string parameter
    {
        std::string format = "Hello, %s!";
        std::string param = "World";
        std::string result = StringHelper::Format(format, param);
        std::string reference = "Hello, World!";
        EXPECT_EQ(reference, result) << "Test case 2 failed";
    }

    // Test with multiple string parameters (mix of const char* and std::string)
    {
        std::string format = "%s says %s to %s";
        std::string name = "John";
        std::string result = StringHelper::Format(format, name, "hello", std::string("Jane"));
        std::string reference = "John says hello to Jane";
        EXPECT_EQ(reference, result) << "Test case 3 failed";
    }

    // Test with integer parameters
    {
        std::string format = "Number: %d, Hex: 0x%X";
        std::string result = StringHelper::Format(format, 42, 255);
        std::string reference = "Number: 42, Hex: 0xFF";
        EXPECT_EQ(reference, result) << "Test case 4 failed";
    }

    // Test with floating point parameters
    {
        std::string format = "Float: %.2f, Scientific: %.2e";
        std::string result = StringHelper::Format(format, 3.14159, 0.00001);
        std::string reference = "Float: 3.14, Scientific: 1.00e-05";
        EXPECT_EQ(reference, result) << "Test case 5 failed";
    }

    // Test with mixed parameter types
    {
        std::string format = "Name: %s, Age: %d, Height: %.1f, Title: %s";
        std::string name = "Alice";
        std::string title = "Engineer";
        std::string result = StringHelper::Format(format, name, 30, 5.8, title);
        std::string reference = "Name: Alice, Age: 30, Height: 5.8, Title: Engineer";
        EXPECT_EQ(reference, result) << "Test case 6 failed";
    }

    // Test with mixed string types (char*, const char*, std::string, and std::string&)
    {
        std::string format = "[%s] [%s] [%s] [%s] [%s] [%s]";
        char str1[] = "char array";
        const char* str2 = "const char ptr";
        std::string str3 = "std::string";
        const std::string str4 = "const std::string";
        std::string& str5 = str3;        // reference to existing string
        const std::string& str6 = str4;  // const reference

        std::string result = StringHelper::Format(format, str1, str2, str3, str4, str5, str6);
        std::string reference =
            "[char array] [const char ptr] [std::string] [const std::string] [std::string] [const std::string]";
        EXPECT_EQ(reference, result) << "Test case 6a failed";
    }

    // Test with mixed string types and other parameters
    {
        std::string format = "User: %s (role: %s), ID: %d, Groups: [%s, %s], Access: %s, Level: %.1f%%";
        char username[] = "john_doe";
        const char* role = "admin";
        std::string group1 = "users";
        const std::string group2 = "developers";
        const std::string& access = group2;  // const reference
        
        std::string result = StringHelper::Format(format, username, role, 12345, group1, group2, access, 99.9);
        std::string reference = "User: john_doe (role: admin), ID: 12345, Groups: [users, developers], Access: developers, Level: 99.9%";
        EXPECT_EQ(reference, result) << "Test case 6b failed";
    }

    // Test with string modifications
    {
        std::string format = "Original: [%s], Modified: [%s], Ptr: [%s]";
        std::string mutable_str = "initial";
        const char* ptr = "pointer";
        
        std::string result = StringHelper::Format(format, mutable_str, (mutable_str += "_modified"), ptr);
        std::string reference = "Original: [initial], Modified: [initial_modified], Ptr: [pointer]";
        EXPECT_EQ(reference, result) << "Test case 6c failed";
    }

    // Test with empty strings
    {
        std::string format = "Empty: '%s', NonEmpty: '%s'";
        std::string empty;
        std::string result = StringHelper::Format(format, empty, "test");
        std::string reference = "Empty: '', NonEmpty: 'test'";
        EXPECT_EQ(reference, result) << "Test case 7 failed";
    }

    // Test with special characters in strings
    {
        std::string format = "Special: %s, Percent: %%s";
        std::string special = "Quote\"Tab\tNewline\n";
        std::string result = StringHelper::Format(format, special);
        std::string reference = "Special: Quote\"Tab\tNewline\n, Percent: %s";
        EXPECT_EQ(reference, result) << "Test case 8 failed";
    }
}

TEST_F(StringHelper_Test, ToHex)
{
    // Test case 1: Default hex conversion (should be lowercase)
    {
        uint8_t value = 0x1A;
        std::string expected = "1a";
        std::string result = StringHelper::ToHex(value);
        EXPECT_EQ(result, expected) << "Default ToHex(uint8_t) should be lowercase";
    }

    // Test case 2: Explicit lowercase
    {
        uint8_t value = 0x1A;
        std::string expected = "1a";
        std::string result = StringHelper::ToHex(value, false);
        EXPECT_EQ(result, expected) << "Explicit ToHex(uint8_t, false) should be lowercase";
    }

    // Test case 3: Explicit uppercase
    {
        uint8_t value = 0x1A;
        std::string expected = "1A";
        std::string result = StringHelper::ToHex(value, true);
        EXPECT_EQ(result, expected) << "Explicit ToHex(uint8_t, true) should be uppercase";
    }

    // Test case 4: 16-bit value, default (should be lowercase)
    {
        uint16_t value = 0x1A2B;
        std::string expected = "1a2b";
        std::string result = StringHelper::ToHex(value);
        EXPECT_EQ(result, expected) << "Default ToHex(uint16_t) should be lowercase";
    }

    // Test case 5: 16-bit value, explicit lowercase
    {
        uint16_t value = 0x1A2B;
        std::string expected = "1a2b";
        std::string result = StringHelper::ToHex(value, false);
        EXPECT_EQ(result, expected) << "Explicit ToHex(uint16_t, false) should be lowercase";
    }

    // Test case 6: 16-bit value, explicit uppercase
    {
        uint16_t value = 0x1A2B;
        std::string expected = "1A2B";
        std::string result = StringHelper::ToHex(value, true);
        EXPECT_EQ(result, expected) << "Explicit ToHex(uint16_t, true) should be uppercase";
    }

    // Test case 7: 32-bit value, explicit uppercase
    {
        uint32_t value = 0x1A2B3C4D;
        std::string expected = "1A2B3C4D";
        std::string result = StringHelper::ToHex(value, true);
        EXPECT_EQ(result, expected) << "Explicit ToHex(uint32_t, true) should be uppercase";
    }

    // Test case 8: 32-bit value, default (should be lowercase)
    {
        uint32_t value = 0x1A2B3C4D;
        std::string expected = "1a2b3c4d";
        std::string result = StringHelper::ToHex(value);
        EXPECT_EQ(result, expected) << "Default ToHex(uint32_t) should be lowercase";
    }

    // Test case 9: 32-bit value, explicit lowercase
    {
        uint32_t value = 0x1A2B3C4D;
        std::string expected = "1a2b3c4d";
        std::string result = StringHelper::ToHex(value, false);
        EXPECT_EQ(result, expected) << "Explicit ToHex(uint32_t, false) should be lowercase";
    }

    // Test case 10: 64-bit value, explicit uppercase
    {
        uint64_t value = 0x1A2B3C4D5E6F7A8B;
        std::string expected = "1A2B3C4D5E6F7A8B";
        std::string result = StringHelper::ToHex(value, true);
        EXPECT_EQ(result, expected) << "Explicit ToHex(uint64_t, true) should be uppercase";
    }

    // Test case 11: 64-bit value, default (should be lowercase)
    {
        uint64_t value = 0x1A2B3C4D5E6F7A8B;
        std::string expected = "1a2b3c4d5e6f7a8b";
        std::string result = StringHelper::ToHex(value);
        EXPECT_EQ(result, expected) << "Default ToHex(uint64_t) should be lowercase";
    }

    // Test case 12: 64-bit value, explicit lowercase
    {
        uint64_t value = 0x1A2B3C4D5E6F7A8B;
        std::string expected = "1a2b3c4d5e6f7a8b";
        std::string result = StringHelper::ToHex(value, false);
        EXPECT_EQ(result, expected) << "Explicit ToHex(uint64_t, false) should be lowercase";
    }

    // Test case 13: char value, default (should be lowercase)
    {
        char value = 0x1A;
        std::string expected = "1a";
        std::string result = StringHelper::ToHex(value);
        EXPECT_EQ(result, expected) << "Default ToHex(char) should be lowercase";
    }

    // Test case 14: char value, explicit lowercase
    {
        char value = 0x1A;
        std::string expected = "1a";
        std::string result = StringHelper::ToHex(value, false);
        EXPECT_EQ(result, expected) << "Explicit ToHex(char, false) should be lowercase";
    }

    // Test case 15: char value, explicit uppercase
    {
        char value = 0x1A;
        std::string expected = "1A";
        std::string result = StringHelper::ToHex(value, true);
        EXPECT_EQ(result, expected) << "Explicit ToHex(char, true) should be uppercase";
    }
}

TEST_F(StringHelper_Test, ToHexWithPrefix)
{
    // Test case 1: Basic hex with default prefix (uppercase)
    {
        uint8_t value = 0x1A;
        std::string expected = "0x1A";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 2: Basic hex with prefix (lowercase)
    {
        uint8_t value = 0x1A;
        std::string expected = "0x1a";
        std::string result = StringHelper::ToHexWithPrefix(value, "0x", false);
        EXPECT_EQ(result, expected);
    }

    // Test case 3: Custom prefix "hex:" (uppercase)
    {
        uint8_t value = 0x1A;
        std::string expected = "hex:1A";
        std::string result = StringHelper::ToHexWithPrefix(value, "hex:");
        EXPECT_EQ(result, expected);
    }

    // Test case 4: Custom prefix "hex:" (lowercase)
    {
        uint8_t value = 0x1A;
        std::string expected = "hex:1a";
        std::string result = StringHelper::ToHexWithPrefix(value, "hex:", false);
        EXPECT_EQ(result, expected);
    }

    // Test case 5: 32-bit value with custom prefix "addr:" (uppercase)
    {
        uint32_t value = 0x1A2B3C4D;
        std::string expected = "addr:1A2B3C4D";
        std::string result = StringHelper::ToHexWithPrefix(value, "addr:");
        EXPECT_EQ(result, expected);
    }

    // Test case 6: 32-bit value with custom prefix "addr:" (lowercase)
    {
        uint32_t value = 0x1A2B3C4D;
        std::string expected = "addr:1a2b3c4d";
        std::string result = StringHelper::ToHexWithPrefix(value, "addr:", false);
        EXPECT_EQ(result, expected);
    }

    // Test case 7: ZX Spectrum assembler prefix "#" (uppercase)
    {
        uint8_t value = 0xFF;
        std::string expected = "#FF";
        std::string result = StringHelper::ToHexWithPrefix(value, "#");
        EXPECT_EQ(result, expected);
    }

    // Test case 8: ZX Spectrum assembler prefix "#" (lowercase)
    {
        uint8_t value = 0xFF;
        std::string expected = "#ff";
        std::string result = StringHelper::ToHexWithPrefix(value, "#", false);
        EXPECT_EQ(result, expected);
    }

    // Test case 9: Alternative prefix "$" (6502/Commodore convention)
    {
        uint16_t value = 0xC000;
        std::string expected = "$C000";
        std::string result = StringHelper::ToHexWithPrefix(value, "$");
        EXPECT_EQ(result, expected);
    }

    // Test case 10: Empty prefix
    {
        uint8_t value = 0xAB;
        std::string expected = "AB";
        std::string result = StringHelper::ToHexWithPrefix(value, "");
        EXPECT_EQ(result, expected);
    }

    // Test case 11: int8_t (signed byte)
    {
        int8_t value = -1;  // 0xFF in two's complement
        std::string expected = "0xFF";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 12: int16_t (signed word)
    {
        int16_t value = -256;  // 0xFF00 in two's complement
        std::string expected = "0xFF00";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 13: int32_t (signed dword)
    {
        int32_t value = -1;  // 0xFFFFFFFF in two's complement
        std::string expected = "0xFFFFFFFF";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 14: int64_t (signed qword)
    {
        int64_t value = -1;  // 0xFFFFFFFFFFFFFFFF in two's complement
        std::string expected = "0xFFFFFFFFFFFFFFFF";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 15: uint16_t edge case (0x0000)
    {
        uint16_t value = 0x0000;
        std::string expected = "0x0000";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 16: uint16_t edge case (0xFFFF)
    {
        uint16_t value = 0xFFFF;
        std::string expected = "0xFFFF";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 17: uint32_t edge case (0x00000000)
    {
        uint32_t value = 0x00000000;
        std::string expected = "0x00000000";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 18: uint32_t edge case (0xFFFFFFFF)
    {
        uint32_t value = 0xFFFFFFFF;
        std::string expected = "0xFFFFFFFF";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 19: uint64_t edge case (0x0000000000000000)
    {
        uint64_t value = 0x0000000000000000;
        std::string expected = "0x0000000000000000";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 20: uint64_t edge case (0xFFFFFFFFFFFFFFFF)
    {
        uint64_t value = 0xFFFFFFFFFFFFFFFF;
        std::string expected = "0xFFFFFFFFFFFFFFFF";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 21: uint64_t with mixed case
    {
        uint64_t value = 0x123456789ABCDEF0;
        std::string expected = "0x123456789ABCDEF0";
        std::string result = StringHelper::ToHexWithPrefix(value);
        EXPECT_EQ(result, expected);
    }

    // Test case 22: uint64_t with mixed case (lowercase)
    {
        uint64_t value = 0x123456789ABCDEF0;
        std::string expected = "0x123456789abcdef0";
        std::string result = StringHelper::ToHexWithPrefix(value, "0x", false);
        EXPECT_EQ(result, expected);
    }

    // Test case 23: Long prefix string
    {
        uint8_t value = 0x42;
        std::string expected = "VALUE:42";
        std::string result = StringHelper::ToHexWithPrefix(value, "VALUE:");
        EXPECT_EQ(result, expected);
    }

    // Test case 24: Single character prefix "h" (Intel hex convention)
    {
        uint16_t value = 0x1234;
        std::string expected = "h1234";
        std::string result = StringHelper::ToHexWithPrefix(value, "h");
        EXPECT_EQ(result, expected);
    }
}