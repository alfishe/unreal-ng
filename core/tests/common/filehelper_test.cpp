#include "pch.h"

#include "filehelper_test.h"

#include "common/filehelper.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <exception>

using namespace std;

/// region <SetUp / TearDown>

void FileHelper_Test::SetUp()
{

}

void FileHelper_Test::TearDown()
{

}

/// endregion </SetUp / TearDown>

TEST_F(FileHelper_Test, NormalizePath)
{
	string testPaths[4] =
	{

		"C:\\Program Files\\Unreal\\unreal.exe",
		"/opt/local/unreal/unreal",
		"/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",
		"/opt/mixed\\path/folder\\subfolder"
	};

	string referenceWindows[4] =
	{
		"C:\\Program Files\\Unreal\\unreal.exe",
		"\\opt\\local\\unreal\\unreal",
		"\\Volumes\\Disk\\Applications\\Unreal.app\\Contents\\MacOS\\unreal",
		"\\opt\\mixed\\path\\folder\\subfolder"
	};

	string referenceUnix[4] =
	{
		"C:/Program Files/Unreal/unreal.exe",
		"/opt/local/unreal/unreal",
		"/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",
		"/opt/mixed/path/folder/subfolder"
	};

	for (int i = 0; i < sizeof(testPaths) / sizeof(testPaths[i]); i++)
	{
		string result = FileHelper::NormalizePath(testPaths[i], '\\');
		bool isEqual = equal(result.begin(), result.end(), referenceWindows[i].begin(), referenceWindows[i].end());
		ASSERT_TRUE(isEqual);

		result = FileHelper::NormalizePath(testPaths[i], L'/');
		isEqual = equal(result.begin(), result.end(), referenceUnix[i].begin(), referenceUnix[i].end());
		ASSERT_TRUE(isEqual);
	}
}