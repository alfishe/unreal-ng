#include "pch.h"

#include "stringhelper_test.h"

#include "common/stringhelper.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <exception>

using namespace std;

void StringHelper_Test::SetUp()
{
}

void StringHelper_Test::TearDown()
{
}

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