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

/// @brief Comprehensive test for GetFileExtension to ensure extensions are returned WITHOUT dot prefix
/// This test covers snapshot formats (.sna, .z80), emulator files, edge cases, and special characters
TEST_F(FileHelper_Test, GetFileExtension)
{
    struct TestCase
    {
        std::string filename;
        std::string expected;
        std::string description;
    };

    std::vector<TestCase> cases =
    {
        // Standard file extensions
        {"test.txt", "txt", "Simple text file"},
        {"document.pdf", "pdf", "PDF document"},
        {"archive.zip", "zip", "Archive file"},
        {"program.exe", "exe", "Executable"},
        
        // Multi-dot extensions
        {"archive.tar.gz", "gz", "Compressed tar - should return last extension only"},
        {"backup.2024.01.08.bak", "bak", "Multiple dots in filename"},
        {"complex.name.with.many.dots.bin", "bin", "Many dots in filename"},
        
        // Snapshot formats (ZX Spectrum emulator)
        {"game.sna", "sna", "48K/128K snapshot format"},
        {"program.z80", "z80", "Z80 snapshot format"},
        {"Dizzy X.sna", "sna", "Snapshot with space in name"},
        {"/testdata/loaders/sna/action.sna", "sna", "Full path to snapshot"},
        {"/testdata/loaders/z80/dizzyx.z80", "z80", "Full path to Z80 snapshot"},
        
        // Other emulator file formats
        {"tape.tap", "tap", "Tape image"},
        {"tape.tzx", "tzx", "TZX tape format"},
        {"disk.trd", "trd", "TR-DOS disk image"},
        {"disk.scl", "scl", "SCL disk image"},
        {"symbols.sym", "sym", "Symbol file"},
        
        // Image formats
        {"photo.jpg", "jpg", "JPEG image"},
        {"photo.jpeg", "jpeg", "JPEG image (long ext)"},
        {"image.png", "png", "PNG image"},
        {"graphic.bmp", "bmp", "Bitmap image"},
        {"icon.ico", "ico", "Icon file"},
        
        // No extension cases
        {"no_extension", "", "Filename without extension"},
        {"Makefile", "", "Build file without extension"},
        {"README", "", "Readme without extension"},
        {"/unix/path/file", "", "Unix path without extension"},
        {"folder/subfolder/noext", "", "Nested path without extension"},
        
        // Hidden files (Unix/macOS)
        {".hiddenfile", "", "Hidden file without extension"},
        {".bashrc", "", "Hidden config file"},
        {".gitignore", "", "Git ignore file"},
        {".config.json", "json", "Hidden file with extension"},
        
        // Path variations
        {"folder/file.jpeg", "jpeg", "Relative path with extension"},
        {"C:/path/to/file.exe", "exe", "Windows absolute path"},
        {"/unix/absolute/path/file.sh", "sh", "Unix absolute path"},
        {"C:\\windows\\path\\file.dll", "dll", "Windows backslash path"},
        {"../relative/../path/file.cpp", "cpp", "Complex relative path"},
        
        // Case sensitivity
        {"FILE.TXT", "TXT", "Uppercase extension"},
        {"File.TxT", "TxT", "Mixed case extension"},
        {"test.SNA", "SNA", "Uppercase snapshot extension"},
        
        // Special characters in filename
        {"file-with-dash.txt", "txt", "Dash in filename"},
        {"file_with_underscore.log", "log", "Underscore in filename"},
        {"file with spaces.dat", "dat", "Spaces in filename"},
        {"file[brackets].bin", "bin", "Brackets in filename"},
        {"file(parens).tmp", "tmp", "Parentheses in filename"},
        
        // Edge cases
        {".", "", "Current directory"},
        {"..", "", "Parent directory"},
        {"...", "", "Triple dot"},
        {"file.", "", "Trailing dot only"},
        {"file..", "", "Trailing double dot"},
        {".file.", "", "Hidden file with trailing dot"},
        {"a.b.c.d.e", "e", "Many single-letter segments"},
        
        // Empty and single character extensions
        {"test.a", "a", "Single letter extension"},
        {"test.z80", "z80", "Three letter extension"},
        {"test.jpeg", "jpeg", "Four letter extension"},
        
        // Numbers in extensions
        {"backup.001", "001", "Numeric extension"},
        {"split.7z", "7z", "Extension starting with number"},
        {"file.mp3", "mp3", "Extension with number"}
    };

    // Test each case
    for (const auto& test : cases)
    {
        std::string ext = FileHelper::GetFileExtension(test.filename);
        
        // Verify extension is returned WITHOUT dot prefix
        ASSERT_FALSE(ext.empty() && ext[0] == '.') 
            << "Extension should NOT start with dot for: " << test.filename;
        
        // Verify expected result
        ASSERT_EQ(ext, test.expected) 
            << "For: " << test.filename << " (" << test.description << ")";
        
        // Additional verification: if extension is not empty, it should not contain a dot
        if (!ext.empty())
        {
            ASSERT_EQ(ext.find('.'), std::string::npos)
                << "Extension should not contain dots for: " << test.filename;
        }
    }
    
    // Summary output for debugging
    std::cout << "GetFileExtension: Verified " << cases.size() << " test cases successfully" << std::endl;
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
        "/opt/local/unreal/unreal",  // On Unix/macOS, paths stay Unix-style
        "/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",  // On Unix/macOS, paths stay Unix-style
        "/opt/mixed/path/folder/subfolder"  // On Unix/macOS, backslashes converted to forward slashes
    };

    for (int i = 0; i < sizeof(testPaths) / sizeof(testPaths[i]); i++)
    {
        string result = FileHelper::AbsolutePath(testPaths[i]);
        // On Unix/macOS, paths should be normalized to forward slashes
        ASSERT_EQ(result, reference[i]) << "Test path: " << testPaths[i];
    }
}

TEST_F(FileHelper_Test, AbsolutePath_ExistingPath)
{
#if defined _WIN32
    // Windows-specific test paths
    std::string tempDir = "C:\\Temp\\filehelper_test";
    std::string tempFile = tempDir + "\\test.txt";

    // Create test directory and file
    int ret = system(("mkdir \"" + tempDir + "\"").c_str());
    ASSERT_EQ(ret, 0);
    ret = system(("type nul > \"" + tempFile + "\"").c_str());
    ASSERT_EQ(ret, 0);

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
    ret = system(("rmdir /S /Q \"" + tempDir + "\"").c_str());
    ASSERT_EQ(ret, 0);
#else
    // Unix-specific test paths
    std::string tempDir = "/tmp/filehelper_test";
    std::string tempFile = tempDir + "/test.txt";

    // Create test directory and file
    int ret = system(("mkdir -p " + tempDir).c_str());
    ASSERT_EQ(ret, 0);
    ret = system(("touch " + tempFile).c_str());
    ASSERT_EQ(ret, 0);

    // Test absolute path resolution
    std::string result = FileHelper::AbsolutePath(tempFile);
    ASSERT_FALSE(result.empty());
    // On macOS, /tmp resolves to /private/tmp, so normalize both paths for comparison
    std::string expected = PlatformPath(tempFile);
    std::string normalizedResult = PlatformPath(result);
    ASSERT_EQ(normalizedResult, expected);

    // Test with relative path
    std::string relPath = "./filehelper_test/test.txt";
    ret = chdir("/tmp");
    ASSERT_EQ(ret, 0);
    result = FileHelper::AbsolutePath(relPath);
    ASSERT_FALSE(result.empty());
    // Normalize both paths for comparison (macOS /tmp -> /private/tmp)
    ASSERT_EQ(PlatformPath(result), PlatformPath(tempFile));

    // Test with symbolic links
    std::string linkPath = tempDir + "/link.txt";
    ret = system(("ln -s " + tempFile + " " + linkPath).c_str());
    ASSERT_EQ(ret, 0);
    result = FileHelper::AbsolutePath(linkPath);
    ASSERT_FALSE(result.empty());
    // Normalize both paths for comparison (macOS /tmp -> /private/tmp)
    ASSERT_EQ(PlatformPath(result), PlatformPath(tempFile));

    // Cleanup
    ret = system(("rm -rf " + tempDir).c_str());
    ASSERT_EQ(ret, 0);
#endif
}

TEST_F(FileHelper_Test, AbsolutePath_NonExistentPath)
{
#if defined _WIN32
    // Windows-specific test paths
    std::string tempDir = "C:\\Temp\\filehelper_test";
    std::string nonExistentFile = tempDir + "\\nonexistent.txt";

    // Create test directory
    int ret = system(("mkdir \"" + tempDir + "\"").c_str());
    ASSERT_EQ(ret, 0);

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
    ret = system(("rmdir /S /Q \"" + tempDir + "\"").c_str());
    ASSERT_EQ(ret, 0);
#else
    // Unix-specific test paths
    std::string tempDir = "/tmp/filehelper_test";
    std::string nonExistentFile = tempDir + "/nonexistent.txt";

    int ret = system(("rm -rf " + tempDir).c_str());
    ASSERT_EQ(ret, 0);
    ret = system(("mkdir -p " + tempDir).c_str());
    ASSERT_EQ(ret, 0);

    // Test absolute path resolution for non-existent file
    std::string result = FileHelper::AbsolutePath(nonExistentFile);
    ASSERT_FALSE(result.empty());
    // Normalize paths for comparison - PlatformPath may resolve parent directory
    std::string expected = PlatformPath(nonExistentFile);
    ASSERT_EQ(PlatformPath(result), expected);

    // Test with non-existent nested path
    std::string nestedPath = tempDir + "/subdir/file.txt";
    result = FileHelper::AbsolutePath(nestedPath);
    ASSERT_FALSE(result.empty());
    expected = PlatformPath(nestedPath);
    ASSERT_EQ(PlatformPath(result), expected);

    // Test with root-level non-existent path
    std::string rootPath = "/nonexistent/file.txt";
    result = FileHelper::AbsolutePath(rootPath);
    ASSERT_FALSE(result.empty());
    expected = PlatformPath(rootPath);
    ASSERT_EQ(PlatformPath(result), expected);

    // Cleanup
    ret = system(("rm -rf " + tempDir).c_str());
    ASSERT_EQ(ret, 0);
#endif
}

TEST_F(FileHelper_Test, AbsolutePath_PathNormalization)
{
#if defined _WIN32
    // Windows-specific path normalization
    std::string tempDir = "C:\\Temp\\filehelper_test";
    std::string mixedSepPath = tempDir + "/test.txt";

    // Create test directory
    int ret = system(("mkdir \"" + tempDir + "\"").c_str());
    ASSERT_EQ(ret, 0);
    ret = system(("type nul > \"" + tempDir + "\\test.txt\"").c_str());
    ASSERT_EQ(ret, 0);

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
    ret = system(("rmdir /S /Q \"" + tempDir + "\"").c_str());
    ASSERT_EQ(ret, 0);
#else
    // Unix-specific path normalization
    std::string tempDir = "/tmp/filehelper_test";
    std::string tempFile = tempDir + "/test.txt";

    int ret = system(("rm -rf " + tempDir).c_str());
    ASSERT_EQ(ret, 0);
    ret = system(("mkdir -p " + tempDir).c_str());
    ASSERT_EQ(ret, 0);
    ret = system(("touch " + tempFile).c_str());
    ASSERT_EQ(ret, 0);

    // Test backslash to forward slash conversion
    std::string mixedSepPath = tempDir + "\\test.txt";
    std::string result = FileHelper::AbsolutePath(mixedSepPath);
    ASSERT_FALSE(result.empty());
    // Normalize both paths for comparison (macOS /tmp -> /private/tmp)
    std::string expected = PlatformPath(tempDir + "/test.txt");
    ASSERT_EQ(PlatformPath(result), expected);

    // Test with redundant separators
    std::string redundantPath = tempDir + "//test.txt";
    result = FileHelper::AbsolutePath(redundantPath);
    ASSERT_FALSE(result.empty());
    // Normalize both paths for comparison (macOS /tmp -> /private/tmp)
    expected = PlatformPath(tempDir + "/test.txt");
    ASSERT_EQ(PlatformPath(result), expected);

    // Test case sensitivity
    std::string casePath = tempDir + "/TEST.txt";
    result = FileHelper::AbsolutePath(casePath);
    ASSERT_FALSE(result.empty());
    expected = PlatformPath(tempDir + "/TEST.txt");
    ASSERT_EQ(PlatformPath(result), expected);

    // Cleanup
    ret = system(("rm -rf " + tempDir).c_str());
    ASSERT_EQ(ret, 0);
#endif
}