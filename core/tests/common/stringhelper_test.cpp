#include "pch.h"

#include "stringhelper_test.h"

#include "common/stringhelper.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <exception>

using namespace std;

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
	wstring test[2][4] =
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
    string original = " \t    test string \t  ";
    string reference = "test string \t  ";

    string_view trimView = StringHelper::LTrim(original);
    string result = string(trimView);

    EXPECT_EQ(result, reference);

    if (StringHelper::Compare(result, reference) != 0)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
    }
}

TEST_F(StringHelper_Test, RTrim)
{
    // Original string contains both spaces and tabs
    string original = " \t    test string \t  ";
    string reference = " \t    test string";

    string_view trimView = StringHelper::RTrim(original);
    string result = string(trimView);

    EXPECT_EQ(result, reference);

    if (StringHelper::Compare(result, reference) != 0)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
    }
}

TEST_F(StringHelper_Test, Trim)
{
    // Original string contains both spaces and tabs
    string original = " \t    test string \t  ";
    string reference = "test string";

    string_view trimView = StringHelper::Trim(original);
    string result = string(trimView);

    EXPECT_EQ(result, reference);

    if (StringHelper::Compare(result, reference) != 0)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
    }
}

TEST_F(StringHelper_Test, FormatWithThousandsDelimiter)
{
    int64_t original = 123456789012;
    string reference = "123,456,789,012";

    string result = StringHelper::FormatWithThousandsDelimiter(original);

    EXPECT_EQ(result, reference);

    if (StringHelper::Compare(result, reference) != 0)
    {
        FAIL() << "Expected result: '" << reference << "', found: '" << result << "'";
    }
}