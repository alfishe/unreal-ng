#include "pch.h"

#include "filehelper_test.h"

#include "common/filehelper.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <exception>


using namespace std;

void FileHelper_Test::SetUp()
{

}

void FileHelper_Test::TearDown()
{

}

TEST_F(FileHelper_Test, NormalizePath)
{
	wstring testPaths[4] =
	{

		L"C:\\Program Files\\Unreal\\unreal.exe",
		L"/opt/local/unreal/unreal",
		L"/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",
		L"/opt/mixed\\path/folder\\subfolder"
	};

	wstring referenceWindows[4] =
	{
		L"C:\\Program Files\\Unreal\\unreal.exe",
		L"\\opt\\local\\unreal\\unreal",
		L"\\Volumes\\Disk\\Applications\\Unreal.app\\Contents\\MacOS\\unreal",
		L"\\opt\\mixed\\path\\folder\\subfolder"
	};

	wstring referenceUnix[4] =
	{
		L"C:/Program Files/Unreal/unreal.exe",
		L"/opt/local/unreal/unreal",
		L"/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",
		L"/opt/mixed/path/folder/subfolder"
	};

	for (int i = 0; i < sizeof(testPaths) / sizeof(testPaths[i]); i++)
	{
		wstring result = FileHelper::NormalizePath(testPaths[i], L'\\');
		bool isEqual = equal(result.begin(), result.end(), referenceWindows[i].begin(), referenceWindows[i].end());
		ASSERT_TRUE(isEqual);

		result = FileHelper::NormalizePath(testPaths[i], L'/');
		isEqual = equal(result.begin(), result.end(), referenceUnix[i].begin(), referenceUnix[i].end());
		ASSERT_TRUE(isEqual);
	}
}