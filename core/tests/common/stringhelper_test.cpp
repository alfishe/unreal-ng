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

    std:: string result = StringHelper::WideStringToString(L"Test");

    if (result != reference)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
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
        std::string& str5 = str3;  // reference to existing string
        const std::string& str6 = str4;  // const reference
        
        std::string result = StringHelper::Format(format, str1, str2, str3, str4, str5, str6);
        std::string reference = "[char array] [const char ptr] [std::string] [const std::string] [std::string] [const std::string]";
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