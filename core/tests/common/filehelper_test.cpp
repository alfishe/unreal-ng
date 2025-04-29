#include "pch.h"

#include "filehelper_test.h"

#include "common/filehelper.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <exception>

/// region <SetUp / TearDown>

void FileHelper_Test::SetUp() {}
void FileHelper_Test::TearDown() {}

/// endregion </SetUp / TearDown>

TEST_F(FileHelper_Test, GetExecutablePath)
{
    std::string exePath = FileHelper::GetExecutablePath();
    std::cout << "Executable path: " << exePath << std::endl;
    ASSERT_FALSE(exePath.empty());
    // The executable path should exist as a directory
    ASSERT_TRUE(FileHelper::FolderExists(exePath));
}

TEST_F(FileHelper_Test, GetFileExtension)
{
    struct TestCase
    {
        std::string filename;
        std::string expected;
    };

    std::vector<TestCase> cases =
            {
        {"test.txt", "txt"},
        {"archive.tar.gz", "gz"},
        {"no_extension", ""},
        {".hiddenfile", ""},
        {"folder/file.jpeg", "jpeg"},
        {"C:/path/to/file.exe", "exe"},
        {"/unix/path/file", ""},
        {"C:\\windows\\file.dll", "dll"},
        {"complex.name.with.many.dots.bin", "bin"}
    };

    for (const auto& test : cases)
    {
        std::string ext = FileHelper::GetFileExtension(test.filename);
        ASSERT_EQ(ext, test.expected) << "For: " << test.filename;
    }
}

TEST_F(FileHelper_Test, NormalizePath)
{
    std::string testPaths[4] =
    {
        "C:\\Program Files\\Unreal\\unreal.exe",
        "/opt/local/unreal/unreal",
        "/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",
        "/opt/mixed\\path/folder\\subfolder"
    };

    std::string referenceWindows[4] =
    {
        "C:\\Program Files\\Unreal\\unreal.exe",
        "\\opt\\local\\unreal\\unreal",
        "\\Volumes\\Disk\\Applications\\Unreal.app\\Contents\\MacOS\\unreal",
        "\\opt\\mixed\\path\\folder\\subfolder"
    };

    std::string referenceUnix[4] =
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

TEST_F(FileHelper_Test, AbsolutePath_NonPlatformSpecific)
{
    std::string testPaths[4] =
    {
        "/Users/dev/Projects/Test/unreal-ng/core/tests/cmake-build-debug/bin/../../../tests/loaders/trd/EyeAche.trd",
        "/opt/local/unreal/unreal",
        "/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",
        "\\opt\\mixed\\path\\folder\\subfolder"
    };

    std::string reference[4] =
    {
        "/Users/dev/Projects/Test/unreal-ng/core/tests/loaders/trd/EyeAche.trd",
        "\\opt\\local\\unreal\\unreal",
        "\\Volumes\\Disk\\Applications\\Unreal.app\\Contents\\MacOS\\unreal",
        "/opt/local/unreal/unreal"
    };

    for (int i = 0; i < sizeof(testPaths) / sizeof(testPaths[i]); i++)
    {
        string result = FileHelper::AbsolutePath(testPaths[i]);
        //bool isEqual = equal(result.begin(), result.end(), reference[i].begin(), reference[i].end());

        ASSERT_EQ(result, reference[i]);
    }
}

TEST_F(FileHelper_Test, AbsolutePath_ExistingPath)
{
#if defined _WIN32
    // Windows-specific test paths
    std::string tempDir = "C:\\Temp\\filehelper_test";
    std::string tempFile = tempDir + "\\test.txt";

    // Create test directory and file
    (void)system(("mkdir \"" + tempDir + "\"").c_str());
    (void)system(("type nul > \"" + tempFile + "\"").c_str());

    // Test absolute path resolution
    std::string result = FileHelper::AbsolutePath(tempFile);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(tempFile));

    // Test with relative path
    std::string relPath = ".\\filehelper_test\\test.txt";
    SetCurrentDirectory("C:\\Temp");
    result = FileHelper::AbsolutePath(relPath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(tempFile));

    // Test with Windows-specific UNC paths
    std::string uncPath = "\\\\localhost\\share\\test.txt";
    result = FileHelper::AbsolutePath(uncPath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(uncPath));

    // Cleanup
    (void)system(("rmdir /S /Q \"" + tempDir + "\"").c_str());
#else
    // Unix-specific test paths
    std::string tempDir = "/tmp/filehelper_test";
    std::string tempFile = tempDir + "/test.txt";

    // Create test directory and file
    (void)system(("mkdir -p " + tempDir).c_str());
    (void)system(("touch " + tempFile).c_str());

    // Test absolute path resolution
    std::string result = FileHelper::AbsolutePath(tempFile);
    ASSERT_FALSE(result.empty());
    std::string expected = tempFile;
#ifdef __APPLE__
    expected = "/private" + tempFile;
#endif
    ASSERT_EQ(result, expected);

    // Test with relative path
    std::string relPath = "./filehelper_test/test.txt";
    chdir("/tmp");
    result = FileHelper::AbsolutePath(relPath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(tempFile));

    // Test with symbolic links
    std::string linkPath = tempDir + "/link.txt";
    system(("ln -s " + tempFile + " " + linkPath).c_str());
    result = FileHelper::AbsolutePath(linkPath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(tempFile));

    // Cleanup
    (void)system(("rm -rf " + tempDir).c_str());
#endif
}

TEST_F(FileHelper_Test, AbsolutePath_NonExistentPath)
{
#if defined _WIN32
    // Windows-specific test paths
    std::string tempDir = "C:\\Temp\\filehelper_test";
    std::string nonExistentFile = tempDir + "\\nonexistent.txt";

    // Create test directory
    (void)system(("mkdir \"" + tempDir + "\"").c_str());

    // Test absolute path resolution for non-existent file
    std::string result = FileHelper::AbsolutePath(nonExistentFile);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(nonExistentFile));

    // Test with Windows drive letter variations
    std::string drivePath = "D:\\nonexistent\\file.txt";
    result = FileHelper::AbsolutePath(drivePath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(drivePath));

    // Cleanup
    (void)system(("rmdir /S /Q \"" + tempDir + "\"").c_str());
#else
    // Unix-specific test paths
    std::string tempDir = "/tmp/filehelper_test";
    std::string nonExistentFile = tempDir + "/nonexistent.txt";

    (void)system(("rm -rf " + tempDir).c_str());
    (void)system(("mkdir -p " + tempDir).c_str());

    // Test absolute path resolution for non-existent file
    std::string result = FileHelper::AbsolutePath(nonExistentFile);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(nonExistentFile));

    // Test with non-existent nested path
    std::string nestedPath = tempDir + "/subdir/file.txt";
    result = FileHelper::AbsolutePath(nestedPath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(nestedPath));

    // Test with root-level non-existent path
    std::string rootPath = "/nonexistent/file.txt";
    result = FileHelper::AbsolutePath(rootPath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(rootPath));

    // Cleanup
    (void)system(("rm -rf " + tempDir).c_str());
#endif
}

TEST_F(FileHelper_Test, AbsolutePath_PathNormalization)
{
#if defined _WIN32
    // Windows-specific path normalization
    std::string tempDir = "C:\\Temp\\filehelper_test";
    std::string mixedSepPath = tempDir + "/test.txt";

    // Create test directory
    (void)system(("mkdir \"" + tempDir + "\"").c_str());
    (void)system(("type nul > \"" + tempDir + "\\test.txt\"").c_str());

    // Test forward slash to backslash conversion
    std::string result = FileHelper::AbsolutePath(mixedSepPath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(tempDir + "\\test.txt"));

    // Test with Windows-specific path features
    std::string shortPath = "C:\\PROGRA~1\\test.txt";
    result = FileHelper::AbsolutePath(shortPath);
    ASSERT_FALSE(result.empty());
    
    // Test case sensitivity handling
    std::string mixedCasePath = "C:\\Temp\\FILEHELPER_TEST\\test.txt";
    result = FileHelper::AbsolutePath(mixedCasePath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(tempDir + "\\test.txt"));

    // Cleanup
    (void)system(("rmdir /S /Q \"" + tempDir + "\"").c_str());
#else
    // Unix-specific path normalization
    std::string tempDir = "/tmp/filehelper_test";
    std::string tempFile = tempDir + "/test.txt";

    (void)system(("rm -rf " + tempDir).c_str());
    (void)system(("mkdir -p " + tempDir).c_str());
    (void)system(("touch " + tempFile).c_str());

    // Test backslash to forward slash conversion
    std::string mixedSepPath = tempDir + "\\test.txt";
    std::string result = FileHelper::AbsolutePath(mixedSepPath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(tempDir + "/test.txt"));

    // Test with redundant separators
    std::string redundantPath = tempDir + "//test.txt";
    result = FileHelper::AbsolutePath(redundantPath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(tempDir + "/test.txt"));

    // Test case sensitivity
    std::string casePath = tempDir + "/TEST.txt";
    result = FileHelper::AbsolutePath(casePath);
    ASSERT_FALSE(result.empty());
    ASSERT_EQ(result, PlatformPath(tempDir + "/TEST.txt"));

    // Cleanup
    (void)system(("rm -rf " + tempDir).c_str());
#endif
}