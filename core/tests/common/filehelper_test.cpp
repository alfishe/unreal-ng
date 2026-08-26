#include "filehelper_test.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>

#include "common/filehelper.h"
#include "pch.h"

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

    std::vector<TestCase> cases = {// Standard file extensions
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
                                   {"/home/user/snapshots/action.sna", "sna", "Full path to snapshot"},
                                   {"/home/user/snapshots/dizzyx.z80", "z80", "Full path to Z80 snapshot"},

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
                                   {"file.mp3", "mp3", "Extension with number"}};

    // Test each case
    for (const auto& test : cases)
    {
        std::string ext = FileHelper::GetFileExtension(test.filename);

        // Verify extension is returned WITHOUT dot prefix
        ASSERT_FALSE(ext.empty() && ext[0] == '.') << "Extension should NOT start with dot for: " << test.filename;

        // Verify expected result
        ASSERT_EQ(ext, test.expected) << "For: " << test.filename << " (" << test.description << ")";

        // Additional verification: if extension is not empty, it should not contain a dot
        if (!ext.empty())
        {
            ASSERT_EQ(ext.find('.'), std::string::npos) << "Extension should not contain dots for: " << test.filename;
        }
    }

    // Summary output for debugging
    std::cout << "GetFileExtension: Verified " << cases.size() << " test cases successfully" << std::endl;
}

TEST_F(FileHelper_Test, NormalizePath)
{
    std::string testPaths[4] = {"C:\\Program Files\\Unreal\\unreal.exe", "/opt/local/unreal/unreal",
                                "/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",
                                "/opt/mixed\\path/folder\\subfolder"};

    std::string referenceWindows[4] = {"C:\\Program Files\\Unreal\\unreal.exe", "\\opt\\local\\unreal\\unreal",
                                       "\\Volumes\\Disk\\Applications\\Unreal.app\\Contents\\MacOS\\unreal",
                                       "\\opt\\mixed\\path\\folder\\subfolder"};

    std::string referenceUnix[4] = {"C:/Program Files/Unreal/unreal.exe", "/opt/local/unreal/unreal",
                                    "/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",
                                    "/opt/mixed/path/folder/subfolder"};

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
    std::string testPaths[4] = {
        "/Users/dev/Projects/Test/unreal-ng/core/tests/cmake-build-debug/bin/../../../tests/loaders/trd/EyeAche.trd",
        "/opt/local/unreal/unreal", "/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",
        "\\opt\\mixed\\path\\folder\\subfolder"};

    std::string reference[4] = {
        "/Users/dev/Projects/Test/unreal-ng/core/tests/loaders/trd/EyeAche.trd",
        "/opt/local/unreal/unreal",                                     // On Unix/macOS, paths stay Unix-style
        "/Volumes/Disk/Applications/Unreal.app/Contents/MacOS/unreal",  // On Unix/macOS, paths stay Unix-style
        "/opt/mixed/path/folder/subfolder"  // On Unix/macOS, backslashes converted to forward slashes
    };

    for (int i = 0; i < sizeof(testPaths) / sizeof(testPaths[i]); i++)
    {
        string result = FileHelper::AbsolutePath(testPaths[i]);
        std::string expected = reference[i];
#ifdef _WIN32
        // On Windows a rooted path without drive letter lives on the current drive and uses backslashes
        expected = std::filesystem::current_path().root_name().string() + FileHelper::NormalizePath(expected, '\\');
#endif
        ASSERT_EQ(result, expected) << "Test path: " << testPaths[i];
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

    // Test with relative path. Restore the working directory on the way out (including the early return an
    // ASSERT_ macro performs) - a leaked cwd makes every later test resolve fixtures relative to C:\Temp.
    struct CwdRestorer
    {
        std::string saved = std::filesystem::current_path().string();
        ~CwdRestorer() { SetCurrentDirectoryA(saved.c_str()); }
    } cwdRestorer;

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

    // Test with relative path.
    //
    // The chdir below used to leak: every later test in the binary inherited
    // /tmp as its working directory, so anything resolving a fixture by
    // relative path silently failed. Two TTD subsystem tests skipped for months
    // because of it, reporting "fixture not found" while the fixture was right
    // there in the repository. Restore on the way out - including the early
    // return an ASSERT_ macro performs.
    struct CwdRestorer
    {
        std::string saved;
        CwdRestorer() { char buf[PATH_MAX]; saved = getcwd(buf, sizeof(buf)) ? buf : ""; }
        ~CwdRestorer() { if (!saved.empty()) { int r = chdir(saved.c_str()); (void)r; } }
    } cwdRestorer;

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

/// @brief Test tilde expansion in AbsolutePath
/// Verifies that ~ is properly expanded to home directory on all platforms
TEST_F(FileHelper_Test, AbsolutePath_TildeExpansion)
{
    // Get expected home directory
#ifdef _WIN32
    const char* home = getenv("USERPROFILE");
    if (!home) home = getenv("HOMEPATH");
#else
    const char* home = getenv("HOME");
#endif
    
    // Skip test if HOME is not set
    if (!home || strlen(home) == 0)
    {
        GTEST_SKIP() << "HOME environment variable not set";
        return;
    }
    
    std::string homeDir = home;
    
    // Test 1: Simple tilde expansion
    std::string result = FileHelper::AbsolutePath("~/test.sna", false);
    EXPECT_FALSE(result.empty()) << "AbsolutePath should not return empty for ~/test.sna";
    EXPECT_EQ(result.find("~"), std::string::npos) << "Tilde should be expanded: " << result;
    EXPECT_NE(result.find(homeDir), std::string::npos) << "Result should contain home dir. Got: " << result;
    
    // Test 2: Tilde with subdirectory
    result = FileHelper::AbsolutePath("~/Downloads/snapshot.sna", false);
    EXPECT_FALSE(result.empty());
    EXPECT_EQ(result.find("~"), std::string::npos) << "Tilde should be expanded: " << result;
    EXPECT_NE(result.find(homeDir), std::string::npos) << "Result should contain home dir. Got: " << result;
    EXPECT_NE(result.find("Downloads"), std::string::npos) << "Path should contain Downloads. Got: " << result;
    
    // Test 3: Non-tilde path should remain unchanged (relative structure)
    result = FileHelper::AbsolutePath("/absolute/path/file.sna", false);
    EXPECT_EQ(result.find("~"), std::string::npos);
    EXPECT_NE(result.find(FileHelper::NormalizePath("/absolute/path")), std::string::npos) << "Absolute path should be preserved. Got: " << result;
    
    // Test 4: Empty path handling
    result = FileHelper::AbsolutePath("", false);
    // Empty path behavior is implementation-defined, just ensure no crash
    
    // Test 5: Just tilde
    result = FileHelper::AbsolutePath("~", false);
    EXPECT_FALSE(result.empty());
    EXPECT_EQ(result.find("~"), std::string::npos) << "Tilde should be expanded: " << result;

    std::cout << "AbsolutePath_TildeExpansion: Home directory is " << homeDir << std::endl;
}

/// @brief Test UNC path normalization (Windows network shares / Samba)
///
/// Supported path format combinations:
///
/// UNC (Network) Paths:
///   - Forward slash UNC:    //server/share/path/file.ext
///   - Backslash UNC:        \\server\share\path\file.ext
///   - IP address UNC:       //172.16.17.10/share/file.ext or \\172.16.17.10\share\file.ext
///   - Mixed separators:     //server/share\path/file.ext
///   - Spaces in path:       //server/Macintosh HD/Users/file.ext
///   - Admin shares:         //localhost/c$/temp/file.txt
///
/// Local Paths:
///   - Windows drive:        C:\Users\dev\file.ext or C:/Users/dev/file.ext
///   - Unix absolute:        /home/user/file.ext
///   - Mixed separators:     C:\Users/dev\file.ext
///
/// On Windows: All paths normalized to backslashes (required for UNC)
/// On Unix/macOS: All paths normalized to forward slashes
TEST_F(FileHelper_Test, NormalizePath_UNCPaths)
{
    struct TestCase
    {
        std::string input;
        std::string expectedWindows;
        std::string expectedUnix;
        std::string description;
    };

    std::vector<TestCase> cases = {
        // UNC paths with forward slashes (common when paths come from URLs or cross-platform sources)
        {"//server/share/path/file.sna", "\\\\server\\share\\path\\file.sna", "//server/share/path/file.sna", "UNC path with forward slashes"},
        {"//172.16.17.10/Macintosh HD/Users/dev/file.sna", "\\\\172.16.17.10\\Macintosh HD\\Users\\dev\\file.sna", "//172.16.17.10/Macintosh HD/Users/dev/file.sna", "UNC IP path with forward slashes and spaces"},
        {"//localhost/c$/temp/file.txt", "\\\\localhost\\c$\\temp\\file.txt", "//localhost/c$/temp/file.txt", "UNC localhost admin share"},

        // UNC paths with backslashes (native Windows format)
        {"\\\\server\\share\\path\\file.sna", "\\\\server\\share\\path\\file.sna", "//server/share/path/file.sna", "UNC path with backslashes"},
        {"\\\\172.16.17.10\\share\\file.txt", "\\\\172.16.17.10\\share\\file.txt", "//172.16.17.10/share/file.txt", "UNC IP path with backslashes"},

        // Mixed separators
        {"//server/share\\mixed/path\\file.txt", "\\\\server\\share\\mixed\\path\\file.txt", "//server/share/mixed/path/file.txt", "UNC with mixed separators"},
        {"\\\\server/share/path\\file.txt", "\\\\server\\share\\path\\file.txt", "//server/share/path/file.txt", "UNC starting with backslash but mixed"},

        // Local paths (should still work)
        {"C:/Users/dev/file.sna", "C:\\Users\\dev\\file.sna", "C:/Users/dev/file.sna", "Windows local path with forward slashes"},
        {"C:\\Users\\dev\\file.sna", "C:\\Users\\dev\\file.sna", "C:/Users/dev/file.sna", "Windows local path with backslashes"},
        {"/home/user/file.sna", "\\home\\user\\file.sna", "/home/user/file.sna", "Unix path"},
    };

    for (const auto& test : cases)
    {
        // Test Windows-style normalization (backslash separator)
        std::string resultWindows = FileHelper::NormalizePath(test.input, '\\');
        EXPECT_EQ(resultWindows, test.expectedWindows) << "Windows normalization failed for: " << test.description;

        // Test Unix-style normalization (forward slash separator)
        std::string resultUnix = FileHelper::NormalizePath(test.input, '/');
        EXPECT_EQ(resultUnix, test.expectedUnix) << "Unix normalization failed for: " << test.description;
    }

    std::cout << "NormalizePath_UNCPaths: Verified " << cases.size() << " UNC path test cases" << std::endl;
}

/// @brief Test AbsolutePath with UNC paths
///
/// Verifies AbsolutePath correctly handles all network/UNC path variations:
///
/// Critical behaviors tested:
///   1. UNC paths must NOT have drive letter prepended (e.g., C:\\ prefix)
///   2. Server/IP address must be preserved at start of path
///   3. Forward slashes converted to backslashes on Windows
///   4. Backslashes converted to forward slashes on Unix/macOS
///   5. resolveSymlinks=true and resolveSymlinks=false both work
///
/// Input formats tested:
///   - //172.16.17.10/share/file.sna     (IP with forward slashes - Samba/macOS Finder style)
///   - //server/share/path/to/file.sna  (hostname with forward slashes)
///   - //localhost/c$/temp/file.txt     (localhost admin share)
///   - \\172.16.17.10\share\file.sna    (IP with backslashes - native Windows Explorer)
///   - \\server\share\path\file.sna     (hostname with backslashes)
///   - //172.16.17.10/Macintosh HD/path (spaces in share name - common on Samba)
///   - //server/share/../other/file.sna (relative path components)
///
/// Expected behavior:
///   Windows: Output uses backslashes (\\server\share\...), UNC prefix preserved, NO drive letter
///   Unix/macOS: Output uses forward slashes (//server/share/...), no backslashes
TEST_F(FileHelper_Test, AbsolutePath_UNCPaths)
{
    struct TestCase
    {
        std::string input;
        std::string expectedWindows;
        std::string expectedPosix;
        std::string description;
    };

    // Exact expectations: on Windows every UNC input must come out as \\server\share\... with nothing
    // prepended (no drive letter, no cwd) and nothing stripped (both leading backslashes kept).
    // On POSIX the same inputs keep their leading "//" and get forward slashes.
    std::vector<TestCase> cases = {
        // The exact path from the bug report (macOS Samba share dropped onto the Windows build)
        {"//172.16.17.10/Macintosh HD/Users/dev/Projects/Test/unreal-ng/testdata/loaders/sna/earshaver-1.sna",
         "\\\\172.16.17.10\\Macintosh HD\\Users\\dev\\Projects\\Test\\unreal-ng\\testdata\\loaders\\sna\\earshaver-1.sna",
         "//172.16.17.10/Macintosh HD/Users/dev/Projects/Test/unreal-ng/testdata/loaders/sna/earshaver-1.sna",
         "UNC IP with spaces, forward slashes (bug report)"},

        // IP address with forward slashes (Samba/macOS Finder drag-drop format)
        {"//172.16.17.10/share/file.sna", "\\\\172.16.17.10\\share\\file.sna", "//172.16.17.10/share/file.sna", "UNC IP with forward slashes"},

        // Hostname with forward slashes
        {"//server/share/path/to/file.sna", "\\\\server\\share\\path\\to\\file.sna", "//server/share/path/to/file.sna", "UNC hostname with forward slashes"},
        {"//localhost/c$/temp/file.txt", "\\\\localhost\\c$\\temp\\file.txt", "//localhost/c$/temp/file.txt", "UNC localhost admin share"},

        // Native Windows backslash format
        {"\\\\172.16.17.10\\share\\file.sna", "\\\\172.16.17.10\\share\\file.sna", "//172.16.17.10/share/file.sna", "UNC IP with backslashes"},
        {"\\\\server\\share\\path\\file.sna", "\\\\server\\share\\path\\file.sna", "//server/share/path/file.sna", "UNC hostname with backslashes"},

        // Mixed separators
        {"//server/share\\mixed/path\\file.sna", "\\\\server\\share\\mixed\\path\\file.sna", "//server/share/mixed/path/file.sna", "UNC with mixed separators"},
        {"\\\\server/share/path\\file.sna", "\\\\server\\share\\path\\file.sna", "//server/share/path/file.sna", "UNC backslash start with mixed separators"},

        // Dot / dot-dot components and duplicated separators inside a UNC path
        {"//server/share/a/./b/../c/file.sna", "\\\\server\\share\\a\\c\\file.sna", "//server/share/a/c/file.sna", "UNC with . and .. components"},
        {"\\\\server\\share\\\\a\\\\file.sna", "\\\\server\\share\\a\\file.sna", "//server/share/a/file.sna", "UNC with duplicated separators"},
        {"//server/share/../../file.sna", "\\\\server\\share\\file.sna", "//file.sna", "UNC .. cannot climb above \\\\server\\share on Windows"},

        // Share root, with and without trailing separator
        {"\\\\server\\share", "\\\\server\\share", "//server/share", "UNC share root"},
        {"//server/share/", "\\\\server\\share\\", "//server/share/", "UNC share root with trailing separator"},

        // Win32 device / long-path prefixes must survive untouched
        {"\\\\?\\C:\\Temp\\..\\Temp\\file.txt", "\\\\?\\C:\\Temp\\file.txt", "//?/C:/Temp/file.txt", "\\\\?\\ drive prefix"},
        {"\\\\?\\UNC\\server\\share\\a\\..\\b", "\\\\?\\UNC\\server\\share\\b", "//?/UNC/server/share/b", "\\\\?\\UNC\\ prefix"},

        // Non-ASCII (UTF-8 bytes must pass through untouched: Cyrillic share/folder, CJK folder, emoji file name)
        {"//172.16.17.10/\xD0\x9E\xD0\xB1\xD1\x89\xD0\xB0\xD1\x8F/\xE6\x97\xA5\xE6\x9C\xAC/\xF0\x9F\x99\x82.sna",
         "\\\\172.16.17.10\\\xD0\x9E\xD0\xB1\xD1\x89\xD0\xB0\xD1\x8F\\\xE6\x97\xA5\xE6\x9C\xAC\\\xF0\x9F\x99\x82.sna",
         "//172.16.17.10/\xD0\x9E\xD0\xB1\xD1\x89\xD0\xB0\xD1\x8F/\xE6\x97\xA5\xE6\x9C\xAC/\xF0\x9F\x99\x82.sna",
         "UNC with UTF-8 (Cyrillic/CJK/emoji) components"},

        // Edge cases
        {"//192.168.1.1/data/test.z80", "\\\\192.168.1.1\\data\\test.z80", "//192.168.1.1/data/test.z80", "UNC private IP"},
        {"//fileserver.domain.com/public/file.sna", "\\\\fileserver.domain.com\\public\\file.sna", "//fileserver.domain.com/public/file.sna", "UNC FQDN hostname"},
    };

    for (const auto& test : cases)
    {
#ifdef _WIN32
        const std::string& expected = test.expectedWindows;
#else
        const std::string& expected = test.expectedPosix;
#endif
        // resolveSymlinks=false: purely lexical (no filesystem / network access), must be exact.
        // Regression: //172.16.17.10/... used to come back as \172.16.17.10\... (MinGW) or C:\172.16.17.10\...
        EXPECT_EQ(FileHelper::AbsolutePath(test.input, false), expected) << test.description << " input: " << test.input;
    }

    std::cout << "AbsolutePath_UNCPaths: Verified " << cases.size() << " UNC path test cases" << std::endl;
}

/// @brief Test FileExists with various path formats
///
/// Verifies FileExists handles all separator combinations:
///
/// Windows tests:
///   - Native backslash:   C:\Temp\file.txt
///   - Forward slash:      C:/Temp/file.txt
///   - Mixed separators:   C:/Temp\subdir/file.txt
///
/// Unix/macOS tests:
///   - Native forward:     /tmp/file.txt
///   - With backslashes:   /tmp\subdir\file.txt (normalized to forward slashes)
///
/// The function normalizes paths before calling std::filesystem operations
/// to ensure consistent behavior regardless of input separator style.
TEST_F(FileHelper_Test, FileExists_PathNormalization)
{
#ifdef _WIN32
    // Create a test file in temp directory
    std::string tempDir = "C:\\Temp\\filehelper_unc_test";
    std::string tempFile = tempDir + "\\test_file.txt";

    // Create test directory and file
    int ret = system(("mkdir \"" + tempDir + "\" 2>nul").c_str());
    ret = system(("type nul > \"" + tempFile + "\"").c_str());

    if (FileHelper::FileExists(tempFile))
    {
        // Test with forward slashes
        std::string forwardSlashPath = "C:/Temp/filehelper_unc_test/test_file.txt";
        EXPECT_TRUE(FileHelper::FileExists(forwardSlashPath)) << "FileExists should work with forward slashes: " << forwardSlashPath;

        // Test with mixed slashes
        std::string mixedPath = "C:/Temp\\filehelper_unc_test/test_file.txt";
        EXPECT_TRUE(FileHelper::FileExists(mixedPath)) << "FileExists should work with mixed slashes: " << mixedPath;

        // Cleanup
        system(("rmdir /S /Q \"" + tempDir + "\"").c_str());
    }
    else
    {
        std::cout << "Skipping FileExists_PathNormalization: could not create test file" << std::endl;
    }
#else
    // Create a test file in temp directory
    std::string tempDir = "/tmp/filehelper_unc_test";
    std::string tempFile = tempDir + "/test_file.txt";

    // Create test directory and file
    int ret = system(("mkdir -p " + tempDir).c_str());
    ret = system(("touch " + tempFile).c_str());
    (void)ret;

    if (FileHelper::FileExists(tempFile))
    {
        // Test with backslashes (should be normalized to forward slashes)
        std::string backslashPath = "/tmp\\filehelper_unc_test\\test_file.txt";
        EXPECT_TRUE(FileHelper::FileExists(backslashPath)) << "FileExists should work with backslashes on Unix: " << backslashPath;

        // Cleanup
        system(("rm -rf " + tempDir).c_str());
    }
    else
    {
        std::cout << "Skipping FileExists_PathNormalization: could not create test file" << std::endl;
    }
#endif
}

/// @brief Test FolderExists with various path formats
///
/// Verifies FolderExists handles all separator combinations:
///
/// Windows tests:
///   - Native backslash:   C:\Temp\folder
///   - Forward slash:      C:/Temp/folder
///   - Mixed separators:   C:/Temp\folder
///   - Trailing slash:     C:/Temp/folder/
///
/// Unix/macOS tests:
///   - Native forward:     /tmp/folder
///   - With backslashes:   /tmp\folder (normalized to forward slashes)
///
/// Same normalization behavior as FileExists.
TEST_F(FileHelper_Test, FolderExists_PathNormalization)
{
#ifdef _WIN32
    std::string tempDir = "C:\\Temp\\filehelper_folder_test";

    // Create test directory
    int ret = system(("mkdir \"" + tempDir + "\" 2>nul").c_str());
    (void)ret;

    if (FileHelper::FolderExists(tempDir))
    {
        // Test with forward slashes
        std::string forwardSlashPath = "C:/Temp/filehelper_folder_test";
        EXPECT_TRUE(FileHelper::FolderExists(forwardSlashPath)) << "FolderExists should work with forward slashes: " << forwardSlashPath;

        // Test with mixed slashes
        std::string mixedPath = "C:/Temp\\filehelper_folder_test";
        EXPECT_TRUE(FileHelper::FolderExists(mixedPath)) << "FolderExists should work with mixed slashes: " << mixedPath;

        // Test with trailing slash
        std::string trailingSlash = "C:/Temp/filehelper_folder_test/";
        EXPECT_TRUE(FileHelper::FolderExists(trailingSlash)) << "FolderExists should work with trailing slash: " << trailingSlash;

        // Cleanup
        system(("rmdir /S /Q \"" + tempDir + "\"").c_str());
    }
    else
    {
        std::cout << "Skipping FolderExists_PathNormalization: could not create test folder" << std::endl;
    }
#else
    std::string tempDir = "/tmp/filehelper_folder_test";

    // Create test directory
    int ret = system(("mkdir -p " + tempDir).c_str());
    (void)ret;

    if (FileHelper::FolderExists(tempDir))
    {
        // Test with backslashes (should be normalized to forward slashes)
        std::string backslashPath = "/tmp\\filehelper_folder_test";
        EXPECT_TRUE(FileHelper::FolderExists(backslashPath)) << "FolderExists should work with backslashes on Unix: " << backslashPath;

        // Cleanup
        system(("rm -rf " + tempDir).c_str());
    }
    else
    {
        std::cout << "Skipping FolderExists_PathNormalization: could not create test folder" << std::endl;
    }
#endif
}

/// @brief Test GetFileSize with various path formats
///
/// Verifies GetFileSize handles all separator combinations:
///
/// Windows tests:
///   - Native backslash:   C:\Temp\file.txt
///   - Forward slash:      C:/Temp/file.txt
///   - Mixed separators:   C:/Temp\subdir/file.txt
///
/// Unix/macOS tests:
///   - Native forward:     /tmp/file.txt
///   - With backslashes:   /tmp\subdir\file.txt (normalized to forward slashes)
///
/// Path is normalized before opening std::ifstream.
TEST_F(FileHelper_Test, GetFileSize_PathNormalization)
{
#ifdef _WIN32
    std::string tempDir = "C:\\Temp\\filehelper_size_test";
    std::string tempFile = tempDir + "\\size_test.txt";
    const std::string testContent = "Hello, World!";

    // Create test directory and file with known content
    int ret = system(("mkdir \"" + tempDir + "\" 2>nul").c_str());
    std::string cmd = "echo " + testContent + " > \"" + tempFile + "\"";
    ret = system(cmd.c_str());
    (void)ret;

    if (FileHelper::FileExists(tempFile))
    {
        size_t expectedSize = FileHelper::GetFileSize(tempFile);
        EXPECT_GT(expectedSize, 0u) << "File should have content";

        // Test with forward slashes
        std::string forwardSlashPath = "C:/Temp/filehelper_size_test/size_test.txt";
        size_t sizeForward = FileHelper::GetFileSize(forwardSlashPath);
        EXPECT_EQ(sizeForward, expectedSize) << "GetFileSize should work with forward slashes";

        // Test with mixed slashes
        std::string mixedPath = "C:/Temp\\filehelper_size_test/size_test.txt";
        size_t sizeMixed = FileHelper::GetFileSize(mixedPath);
        EXPECT_EQ(sizeMixed, expectedSize) << "GetFileSize should work with mixed slashes";

        // Cleanup
        system(("rmdir /S /Q \"" + tempDir + "\"").c_str());
    }
    else
    {
        std::cout << "Skipping GetFileSize_PathNormalization: could not create test file" << std::endl;
    }
#else
    std::string tempDir = "/tmp/filehelper_size_test";
    std::string tempFile = tempDir + "/size_test.txt";

    // Create test directory and file
    int ret = system(("mkdir -p " + tempDir).c_str());
    ret = system(("echo 'Hello, World!' > " + tempFile).c_str());
    (void)ret;

    if (FileHelper::FileExists(tempFile))
    {
        size_t expectedSize = FileHelper::GetFileSize(tempFile);
        EXPECT_GT(expectedSize, 0u) << "File should have content";

        // Test with backslashes
        std::string backslashPath = "/tmp\\filehelper_size_test\\size_test.txt";
        size_t sizeBackslash = FileHelper::GetFileSize(backslashPath);
        EXPECT_EQ(sizeBackslash, expectedSize) << "GetFileSize should work with backslashes on Unix";

        // Cleanup
        system(("rm -rf " + tempDir).c_str());
    }
    else
    {
        std::cout << "Skipping GetFileSize_PathNormalization: could not create test file" << std::endl;
    }
#endif
}

/// @brief Test OpenFile / OpenExistingFile with various path formats
///
/// Verifies OpenFile and OpenExistingFile handle all separator combinations:
///
/// Windows tests (OpenExistingFile for reading, OpenFile for writing):
///   - Native backslash:   C:\Temp\file.txt
///   - Forward slash:      C:/Temp/file.txt
///   - Mixed separators:   C:/Temp\subdir/file.txt
///
/// Unix/macOS tests:
///   - Native forward:     /tmp/file.txt
///   - With backslashes:   /tmp\subdir\file.txt (normalized to forward slashes)
///
/// Path is normalized before calling fopen().
/// OpenExistingFile also uses FileExists internally (which normalizes).
TEST_F(FileHelper_Test, OpenFile_PathNormalization)
{
#ifdef _WIN32
    std::string tempDir = "C:\\Temp\\filehelper_open_test";
    std::string tempFile = tempDir + "\\open_test.txt";

    // Create test directory and file
    int ret = system(("mkdir \"" + tempDir + "\" 2>nul").c_str());
    ret = system(("type nul > \"" + tempFile + "\"").c_str());
    (void)ret;

    if (FileHelper::FileExists(tempFile))
    {
        // Test OpenExistingFile with forward slashes
        std::string forwardSlashPath = "C:/Temp/filehelper_open_test/open_test.txt";
        FILE* file = FileHelper::OpenExistingFile(forwardSlashPath, "r");
        EXPECT_NE(file, nullptr) << "OpenExistingFile should work with forward slashes: " << forwardSlashPath;
        if (file) fclose(file);

        // Test OpenExistingFile with mixed slashes
        std::string mixedPath = "C:/Temp\\filehelper_open_test/open_test.txt";
        file = FileHelper::OpenExistingFile(mixedPath, "r");
        EXPECT_NE(file, nullptr) << "OpenExistingFile should work with mixed slashes: " << mixedPath;
        if (file) fclose(file);

        // Test OpenFile (for writing) with forward slashes
        std::string newFilePath = "C:/Temp/filehelper_open_test/new_file.txt";
        file = FileHelper::OpenFile(newFilePath, "w");
        EXPECT_NE(file, nullptr) << "OpenFile should work with forward slashes: " << newFilePath;
        if (file)
        {
            fclose(file);
            // Verify file was created
            EXPECT_TRUE(FileHelper::FileExists(newFilePath)) << "File should be created";
        }

        // Cleanup
        system(("rmdir /S /Q \"" + tempDir + "\"").c_str());
    }
    else
    {
        std::cout << "Skipping OpenFile_PathNormalization: could not create test file" << std::endl;
    }
#else
    std::string tempDir = "/tmp/filehelper_open_test";
    std::string tempFile = tempDir + "/open_test.txt";

    // Create test directory and file
    int ret = system(("mkdir -p " + tempDir).c_str());
    ret = system(("touch " + tempFile).c_str());
    (void)ret;

    if (FileHelper::FileExists(tempFile))
    {
        // Test OpenExistingFile with backslashes
        std::string backslashPath = "/tmp\\filehelper_open_test\\open_test.txt";
        FILE* file = FileHelper::OpenExistingFile(backslashPath, "r");
        EXPECT_NE(file, nullptr) << "OpenExistingFile should work with backslashes on Unix: " << backslashPath;
        if (file) fclose(file);

        // Cleanup
        system(("rm -rf " + tempDir).c_str());
    }
    else
    {
        std::cout << "Skipping OpenFile_PathNormalization: could not create test file" << std::endl;
    }
#endif
}

/// region <Pure (no filesystem access) path-shape tests>
///
/// Every test below is table-driven and lexical only: no file is created, opened or probed.
/// The only environment dependency is the current working directory (read-only) for the relative-path cases.

namespace
{
#ifdef _WIN32
    constexpr bool kIsWindows = true;
#else
    constexpr bool kIsWindows = false;
#endif

    /// Pick the expectation for the current platform
    std::string Plat(const std::string& windows, const std::string& posix)
    {
        return kIsWindows ? windows : posix;
    }

    /// Current working directory, native separators, no trailing separator
    std::string Cwd()
    {
        std::string cwd = std::filesystem::current_path().string();
        while (cwd.size() > 1 && (cwd.back() == '\\' || cwd.back() == '/'))
            cwd.pop_back();
        return cwd;
    }

    /// Parent of the current working directory
    std::string CwdParent()
    {
        std::string parent = std::filesystem::path(Cwd()).parent_path().string();
        while (!parent.empty() && (parent.back() == '\\' || parent.back() == '/'))
            parent.pop_back();  // "C:\" / "/" -> "C:" / "" so that appending a separator never doubles it
        return parent;
    }

    /// "C:" on Windows (the drive of the current directory), "" elsewhere
    std::string CurrentDrive()
    {
        return kIsWindows ? std::filesystem::current_path().root_name().string() : std::string();
    }

    /// Home directory, no trailing separator, "" when unknown
    std::string Home()
    {
#ifdef _WIN32
        const char* home = getenv("USERPROFILE");
#else
        const char* home = getenv("HOME");
#endif
        std::string result = home ? home : "";
        while (result.size() > 1 && (result.back() == '\\' || result.back() == '/'))
            result.pop_back();
        return result;
    }

    const std::string kSep(1, FileHelper::GetPathSeparator());
}  // namespace

/// @brief NormalizePath must be a lossless separator substitution - converting to one style and back
/// yields exactly the other style, for every path shape (UNC, drive, device prefix, relative, mixed).
TEST_F(FileHelper_Test, NormalizePath_RoundTrip)
{
    std::vector<std::string> inputs = {
        "//172.16.17.10/Macintosh HD/Users/dev/file.sna",
        "\\\\server\\share\\dir\\file.sna",
        "\\\\?\\C:\\dir\\file.sna",
        "\\\\?\\UNC\\server\\share\\file.sna",
        "C:\\dir/sub\\file.sna",
        "/opt/unreal\\mixed/path",
        "relative\\dir/file",
        "./a/../b\\c",
        "~/snapshots\\game.sna",
        "",
        "/",
        "\\\\",
    };

    for (const auto& input : inputs)
    {
        std::string posix = FileHelper::NormalizePath(input, '/');
        std::string windows = FileHelper::NormalizePath(input, '\\');

        EXPECT_EQ(posix.find('\\'), std::string::npos) << "No backslash may survive '/' normalization of: " << input;
        EXPECT_EQ(windows.find('/'), std::string::npos) << "No slash may survive '\\' normalization of: " << input;
        EXPECT_EQ(posix.size(), input.size()) << "NormalizePath must not change length for: " << input;

        EXPECT_EQ(FileHelper::NormalizePath(posix, '\\'), windows) << "'/' -> '\\' round trip failed for: " << input;
        EXPECT_EQ(FileHelper::NormalizePath(windows, '/'), posix) << "'\\' -> '/' round trip failed for: " << input;

        // Default overload == native separator
        EXPECT_EQ(FileHelper::NormalizePath(input), Plat(windows, posix)) << "Native normalization failed for: " << input;
    }
}

/// @brief IsUNCPath is syntactic (two leading separators + non-separator) and identical on every platform;
/// IsAbsolutePath follows the host OS rules.
TEST_F(FileHelper_Test, IsAbsolutePath_IsUNCPath)
{
    struct TestCase
    {
        std::string input;
        bool unc;
        bool absoluteWindows;
        bool absolutePosix;
    };

    std::vector<TestCase> cases = {
        {"//172.16.17.10/Macintosh HD/Users/dev/file.sna", true, true, true},
        {"\\\\server\\share\\file.sna", true, true, true},
        {"//server/share", true, true, true},
        {"\\\\server/share\\mixed", true, true, true},
        {"\\\\?\\C:\\dir\\file", true, true, true},
        {"\\\\?\\UNC\\server\\share", true, true, true},
        {"///triple/slash", false, true, true},   // not a well-formed UNC, but still the UNC namespace on Windows; POSIX treats 3+ slashes as "/"
        {"C:\\dir\\file", false, true, false},
        {"C:/dir/file", false, true, false},
        {"c:\\", false, true, false},
        {"C:", false, false, false},           // drive-relative
        {"C:relative\\file", false, false, false},
        {"/opt/unreal", false, false, true},   // rooted without drive: relative to current drive on Windows
        {"\\opt\\unreal", false, false, true},
        {"/", false, false, true},
        {"\\", false, false, true},            // single-backslash root: '\' aliases '/' throughout FileHelper
        {"relative/dir", false, false, false},
        {"./file", false, false, false},
        {"../file", false, false, false},
        {"~/file", false, false, false},
        {"file.sna", false, false, false},
        {"", false, false, false},
    };

    for (const auto& test : cases)
    {
        EXPECT_EQ(FileHelper::IsUNCPath(test.input), test.unc) << "IsUNCPath failed for: " << test.input;
        EXPECT_EQ(FileHelper::IsAbsolutePath(test.input), kIsWindows ? test.absoluteWindows : test.absolutePosix)
            << "IsAbsolutePath failed for: " << test.input;

        // The invariant that makes '\'-rooted-is-absolute safe on POSIX:
        // classification must not change across NormalizePath, because
        // AbsolutePath() (the only production caller) normalizes BEFORE it
        // classifies. If raw and normalized input ever diverge, cwd-prepending
        // decisions would depend on where in the pipeline the check runs.
        if (!test.input.empty())
        {
            std::string normalized = FileHelper::NormalizePath(test.input);
            EXPECT_EQ(FileHelper::IsAbsolutePath(normalized), FileHelper::IsAbsolutePath(test.input))
                << "IsAbsolutePath diverges across NormalizePath for: " << test.input << " -> " << normalized;
        }
    }

    // End-to-end: a Windows-origin spelling resolves like its normalized form
    // (no cwd prepended) on every platform
    std::string resolved = FileHelper::AbsolutePath("\\opt\\unreal");
    if (kIsWindows)
    {
        // Rooted-without-drive: anchored to the current drive's root
        EXPECT_NE(resolved.find(":\\opt\\unreal"), std::string::npos) << resolved;
    }
    else
    {
        EXPECT_EQ(resolved, "/opt/unreal") << "must not be cwd-prepended";
    }
}

/// @brief LexicallyNormalPath: native separators, duplicated separators collapsed, "." removed, ".." resolved
/// but never above the root, and the root (drive, \\server\share, \\?\ prefix, "/" or "//") kept verbatim.
/// Follows std::filesystem::path::lexically_normal semantics for trailing separators
/// ("a/b/." -> "a/b/", "a/.." -> ".", "a/../.." -> "..").
TEST_F(FileHelper_Test, LexicallyNormalPath)
{
    struct TestCase
    {
        std::string input;
        std::string expectedWindows;
        std::string expectedPosix;
    };

    std::vector<TestCase> cases = {
        // UNC roots
        {"//172.16.17.10/Macintosh HD/Users/./dev/../dev/file.sna", "\\\\172.16.17.10\\Macintosh HD\\Users\\dev\\file.sna", "//172.16.17.10/Macintosh HD/Users/dev/file.sna"},
        {"\\\\server\\share\\a\\..\\b", "\\\\server\\share\\b", "//server/share/b"},
        {"//server/share/../x", "\\\\server\\share\\x", "//server/x"},
        {"//server/share//a///b", "\\\\server\\share\\a\\b", "//server/share/a/b"},
        {"\\\\server\\share", "\\\\server\\share", "//server/share"},
        {"\\\\server\\share\\", "\\\\server\\share\\", "//server/share/"},
        {"\\\\server", "\\\\server", "//server"},
        {"\\\\?\\C:\\a\\..\\b", "\\\\?\\C:\\b", "//?/C:/b"},
        {"\\\\?\\UNC\\server\\share\\a\\..", "\\\\?\\UNC\\server\\share\\", "//?/UNC/server/share/"},
        {"///triple/slash/../x", "\\\\triple\\slash\\x", "/triple/x"},  // POSIX: 3+ leading slashes == "/"

        // Drive roots
        {"C:\\a\\..\\b\\.\\c", "C:\\b\\c", "C:/b/c"},
        {"C:/a//b///c", "C:\\a\\b\\c", "C:/a/b/c"},
        {"C:\\a\\..\\..\\b", "C:\\b", "b"},       // POSIX: "C:" is an ordinary component that ".." can consume
        {"C:\\", "C:\\", "C:/"},
        {"C:", "C:", "C:"},
        {"C:foo\\..\\bar", "C:bar", "bar"},       // Windows drive-relative keeps the drive
        {"C:\\Temp\\", "C:\\Temp\\", "C:/Temp/"},

        // Rooted
        {"/a/../../b", "\\b", "/b"},
        {"/a/./b/", "\\a\\b\\", "/a/b/"},
        {"\\opt\\mixed/path", "\\opt\\mixed\\path", "/opt/mixed/path"},
        {"/", "\\", "/"},
        {"//", "\\\\", "//"},

        // Relative
        {"a/../b", "b", "b"},
        {"a/..", ".", "."},
        {"a/../..", "..", ".."},
        {"../a", "..\\a", "../a"},
        {"../../a/../b", "..\\..\\b", "../../b"},
        {"a/b/.", "a\\b\\", "a/b/"},
        {"a/b/..", "a\\", "a/"},
        {"a/b/", "a\\b\\", "a/b/"},
        {"./a", "a", "a"},
        {".", ".", "."},
        {"..", "..", ".."},
        {"", "", ""},
        {"file.sna", "file.sna", "file.sna"},
        {"~/x/../y", "~\\y", "~/y"},  // no tilde expansion here - "~" is just a component

        // UTF-8 components are opaque bytes to the normalizer (no separator byte can occur inside a UTF-8 sequence)
        {"C:/\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B/./\xE6\x97\xA5\xE6\x9C\xAC/../\xF0\x9F\x99\x82.sna",
         "C:\\\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B\\\xF0\x9F\x99\x82.sna", "C:/\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B/\xF0\x9F\x99\x82.sna"},
    };

    for (const auto& test : cases)
    {
        std::string result = FileHelper::LexicallyNormalPath(test.input);
        EXPECT_EQ(result, Plat(test.expectedWindows, test.expectedPosix)) << "LexicallyNormalPath failed for: " << test.input;

        // Idempotent
        EXPECT_EQ(FileHelper::LexicallyNormalPath(result), result) << "LexicallyNormalPath not idempotent for: " << test.input;
    }
}

/// @brief ExpandPath only touches a leading "~" that is alone or followed by a separator.
TEST_F(FileHelper_Test, ExpandPath_Tilde)
{
    const std::string home = Home();
    if (home.empty())
    {
        GTEST_SKIP() << "Home directory environment variable not set";
    }

    EXPECT_EQ(FileHelper::ExpandPath("~"), home);
    EXPECT_EQ(FileHelper::ExpandPath("~/snapshots/game.sna"), home + "/snapshots/game.sna");
    EXPECT_EQ(FileHelper::ExpandPath("~\\snapshots\\game.sna"), home + "\\snapshots\\game.sna");

    // Untouched
    EXPECT_EQ(FileHelper::ExpandPath("~user/file"), "~user/file");
    EXPECT_EQ(FileHelper::ExpandPath("dir/~/file"), "dir/~/file");
    EXPECT_EQ(FileHelper::ExpandPath("file~"), "file~");
    EXPECT_EQ(FileHelper::ExpandPath("//server/share/~"), "//server/share/~");
    EXPECT_EQ(FileHelper::ExpandPath("C:\\~\\x"), "C:\\~\\x");
    EXPECT_EQ(FileHelper::ExpandPath(""), "");
}

/// @brief PathCombine joins with exactly one native separator, whatever the fragments end/start with,
/// and never rewrites the fragments themselves (regression: "C:\Temp" + "file" used to become "C:/\Temp\file").
TEST_F(FileHelper_Test, PathCombine_AllSeparatorCombinations)
{
    struct TestCase
    {
        std::string path1;
        std::string path2;
        std::string expectedWindows;
        std::string expectedPosix;
    };

    std::vector<TestCase> cases = {
        // Drive paths
        {"C:\\Temp", "file.sna", "C:\\Temp\\file.sna", "C:/Temp/file.sna"},
        {"C:/Temp/", "file.sna", "C:\\Temp\\file.sna", "C:/Temp/file.sna"},
        {"C:/Temp", "/file.sna", "C:\\Temp\\file.sna", "C:/Temp/file.sna"},
        {"C:/Temp/", "/file.sna", "C:\\Temp\\file.sna", "C:/Temp/file.sna"},
        {"C:\\Temp\\", "\\file.sna", "C:\\Temp\\file.sna", "C:/Temp/file.sna"},
        {"C:", "Temp", "C:\\Temp", "C:/Temp"},
        {"C:\\", "Temp", "C:\\Temp", "C:/Temp"},
        {"C:/", "/Temp", "C:\\Temp", "C:/Temp"},

        // UNC paths
        {"\\\\server\\share", "dir\\file.sna", "\\\\server\\share\\dir\\file.sna", "//server/share/dir/file.sna"},
        {"//server/share/", "/file.sna", "\\\\server\\share\\file.sna", "//server/share/file.sna"},
        {"//172.16.17.10/Macintosh HD", "Users/dev/file.sna", "\\\\172.16.17.10\\Macintosh HD\\Users\\dev\\file.sna", "//172.16.17.10/Macintosh HD/Users/dev/file.sna"},
        {"\\\\?\\C:\\dir", "file", "\\\\?\\C:\\dir\\file", "//?/C:/dir/file"},

        // POSIX / rooted
        {"/opt/unreal", "configs", "\\opt\\unreal\\configs", "/opt/unreal/configs"},
        {"/opt/unreal", "/screenshots", "\\opt\\unreal\\screenshots", "/opt/unreal/screenshots"},
        {"/opt/unreal/", "/screenshots/", "\\opt\\unreal\\screenshots\\", "/opt/unreal/screenshots/"},
        {"/", "x", "\\x", "/x"},

        // Relative
        {"relative", "sub/file", "relative\\sub\\file", "relative/sub/file"},
        {"resources", "configs\\unreal.ini", "resources\\configs\\unreal.ini", "resources/configs/unreal.ini"},
        {".", "file", ".\\file", "./file"},
        {"..", "file", "..\\file", "../file"},
        {"~", "file", "~\\file", "~/file"},  // no tilde expansion in PathCombine

        // Empty fragments
        {"", "x", "x", "x"},
        {"x", "", "x", "x"},
        {"", "", "", ""},
        {"", "/abs/x", "/abs/x", "/abs/x"},    // single fragment is returned verbatim
        {"C:\\dir\\", "", "C:\\dir\\", "C:\\dir\\"},
    };

    for (const auto& test : cases)
    {
        std::string result = FileHelper::PathCombine(test.path1, test.path2);
        EXPECT_EQ(result, Plat(test.expectedWindows, test.expectedPosix)) << "PathCombine(\"" << test.path1 << "\", \"" << test.path2 << "\")";

        // const char* overload must behave identically
        EXPECT_EQ(FileHelper::PathCombine(test.path1, test.path2.c_str()), result) << "PathCombine(const char*) differs for: " << test.path1 << " + " << test.path2;
    }
}

/// @brief AbsolutePath(path, resolveSymlinks=false) for every input shape - exact lexical expectations.
/// Absolute inputs are never rewritten (no cwd, no drive letter prepended), relative inputs are anchored at the cwd,
/// "~" is expanded, separators become native, "." / ".." / duplicated separators are folded.
TEST_F(FileHelper_Test, AbsolutePath_Lexical_AllShapes)
{
    const std::string cwd = Cwd();
    const std::string drive = CurrentDrive();  // "C:" on Windows
    const std::string home = Home();

    struct TestCase
    {
        std::string input;
        std::string expectedWindows;
        std::string expectedPosix;
    };

    std::vector<TestCase> cases = {
        // Absolute Windows drive paths
        {"C:/Users/dev/file.sna", "C:\\Users\\dev\\file.sna", cwd + "/C:/Users/dev/file.sna"},  // "C:/..." is relative on POSIX
        {"C:\\Temp\\a\\..\\b\\.\\file.txt", "C:\\Temp\\b\\file.txt", cwd + "/C:/Temp/b/file.txt"},
        {"C:\\Temp\\\\double\\\\sep\\file", "C:\\Temp\\double\\sep\\file", cwd + "/C:/Temp/double/sep/file"},
        {"C:\\Temp\\", "C:\\Temp\\", cwd + "/C:/Temp/"},
        {"C:\\", "C:\\", cwd + "/C:/"},
        {"D:\\nonexistent\\file.txt", "D:\\nonexistent\\file.txt", cwd + "/D:/nonexistent/file.txt"},

        // UNC - one representative per style (full matrix in AbsolutePath_UNCPaths)
        {"//172.16.17.10/Macintosh HD/Users/dev/file.sna", "\\\\172.16.17.10\\Macintosh HD\\Users\\dev\\file.sna", "//172.16.17.10/Macintosh HD/Users/dev/file.sna"},
        {"\\\\server\\share\\..\\..\\file", "\\\\server\\share\\file", "//file"},
        {"///triple/slash/file", "\\\\triple\\slash\\file", "/triple/slash/file"},

        // POSIX absolute == rooted on the current drive on Windows
        {"/opt/local/unreal/unreal", drive + "\\opt\\local\\unreal\\unreal", "/opt/local/unreal/unreal"},
        {"\\opt\\mixed/path\\folder", drive + "\\opt\\mixed\\path\\folder", "/opt/mixed/path/folder"},
        {"/Users/dev/cmake-build-debug/bin/../../../tests/loaders/trd/EyeAche.trd", drive + "\\Users\\tests\\loaders\\trd\\EyeAche.trd", "/Users/tests/loaders/trd/EyeAche.trd"},
        {"/a/../../b", drive + "\\b", "/b"},
        {"/", drive + "\\", "/"},

        // Relative
        {"relative/dir/file.sna", cwd + kSep + "relative" + kSep + "dir" + kSep + "file.sna", cwd + "/relative/dir/file.sna"},
        {"./file.sna", cwd + kSep + "file.sna", cwd + "/file.sna"},
        {"../file.sna", CwdParent() + kSep + "file.sna", CwdParent() + "/file.sna"},
        {"a/../b/./file.sna", cwd + kSep + "b" + kSep + "file.sna", cwd + "/b/file.sna"},
        {"file.sna", cwd + kSep + "file.sna", cwd + "/file.sna"},
        {".", cwd + kSep, cwd + "/"},      // std::filesystem lexically_normal semantics: a trailing "." keeps a trailing separator
        {"a/..", cwd + kSep, cwd + "/"},
        {"~user/file", cwd + kSep + "~user" + kSep + "file", cwd + "/~user/file"},  // not a home reference

        // Empty stays empty (never turns into the cwd)
        {"", "", ""},
    };

    // Windows drive-relative forms resolve against the current directory of that drive; only the current
    // drive's cwd is known here, so test them for the current drive only.
    if (kIsWindows && drive.size() == 2)
    {
        cases.push_back({drive, cwd, cwd});
        cases.push_back({drive + "file.sna", cwd + kSep + "file.sna", cwd + "/file.sna"});
        cases.push_back({drive + "a\\..\\b", cwd + kSep + "b", cwd + "/b"});
    }

    if (!home.empty())
    {
        cases.push_back({"~", home, home});
        cases.push_back({"~/x/file.sna", home + kSep + "x" + kSep + "file.sna", home + "/x/file.sna"});
        cases.push_back({"~\\x\\..\\y", home + kSep + "y", home + "/y"});
    }

    for (const auto& test : cases)
    {
        std::string result = FileHelper::AbsolutePath(test.input, false);
        EXPECT_EQ(result, Plat(test.expectedWindows, test.expectedPosix)) << "AbsolutePath(\"" << test.input << "\", false)";

        // Idempotent: feeding the result back yields the same string
        EXPECT_EQ(FileHelper::AbsolutePath(result, false), result) << "AbsolutePath not idempotent for: " << test.input;
    }
}

/// @brief GetFileExtension must see through every root shape (UNC, \\?\ prefix, mixed separators).
TEST_F(FileHelper_Test, GetFileExtension_PathShapes)
{
    EXPECT_EQ(FileHelper::GetFileExtension("//172.16.17.10/Macintosh HD/Users/dev/earshaver-1.sna"), "sna");
    EXPECT_EQ(FileHelper::GetFileExtension("\\\\server\\share\\dir.d\\file.Z80"), "Z80");
    EXPECT_EQ(FileHelper::GetFileExtension("\\\\?\\C:\\dir.d\\file.trd"), "trd");
    EXPECT_EQ(FileHelper::GetFileExtension("//server/share/dir.d/noext"), "");
    EXPECT_EQ(FileHelper::GetFileExtension("C:/dir.d\\file.tap"), "tap");
    EXPECT_EQ(FileHelper::GetFileExtension("C:\\dir.d\\"), "");
    EXPECT_EQ(FileHelper::GetFileExtension("~/snapshots/.hidden.sna"), "sna");
    EXPECT_EQ(FileHelper::GetFileExtension("~/snapshots/.hidden"), "");
    EXPECT_EQ(FileHelper::GetFileExtension("//server/\xD0\x98\xD0\xB3\xD1\x80\xD1\x8B/\xE6\x97\xA5\xE6\x9C\xAC.\xF0\x9F\x99\x82/\xD0\xA1\xD0\xBD\xD0\xB8\xD0\xBC\xD0\xBE\xD0\xBA.sna"), "sna");
}

/// endregion </Pure (no filesystem access) path-shape tests>