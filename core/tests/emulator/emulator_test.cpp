#include "stdafx.h"
#include "pch.h"

#include "emulator_test.h"

#include "common/modulelogger.h"
#include "common/timehelper.h"
#include "emulator/emulator.h"
#include "common/filehelper.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"

#include <cctype>
#include <utility>
#include <vector>

/// region <SetUp / TearDown>

void Emulator_Test::SetUp()
{

}

void Emulator_Test::TearDown()
{
}

/// endregion </Setup / TearDown>

/// region <Helper methods>
void Emulator_Test::DestroyEmulator()
{
    if (_cpu != nullptr)
    {
        delete _cpu;
        _cpu = nullptr;
    }

    if (_context != nullptr)
    {
        delete _context;
        _context = nullptr;
    }
}
/// endregion </Helper methods>

/// region <Emulator re-entrability tests>
TEST_F(Emulator_Test, MultiInstance)
{
    constexpr int iterations = 100;

    // Profiling accumulators (microseconds)
    uint64_t totalConstruct = 0, totalInit = 0, totalStop = 0, totalRelease = 0, totalDelete = 0;

    int successCounter = 0;
    for (int i = 0; i < iterations; i++)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        Emulator* emulator = new Emulator(LoggerLevel::LogError);
        auto t1 = std::chrono::high_resolution_clock::now();

        if (emulator)
        {
            if (emulator->Init())
            {
                auto t2 = std::chrono::high_resolution_clock::now();
                emulator->Stop();
                auto t3 = std::chrono::high_resolution_clock::now();
                emulator->Release();
                auto t4 = std::chrono::high_resolution_clock::now();

                totalInit += std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
                totalStop += std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
                totalRelease += std::chrono::duration_cast<std::chrono::microseconds>(t4 - t3).count();

                successCounter++;
            }

            auto t5 = std::chrono::high_resolution_clock::now();
            delete emulator;
            auto t6 = std::chrono::high_resolution_clock::now();

            totalConstruct += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            totalDelete += std::chrono::duration_cast<std::chrono::microseconds>(t6 - t5).count();
        }
    }

    GTEST_LOG_(INFO) << "Profiling (" << iterations << " iterations):";
    GTEST_LOG_(INFO) << "  Construct: " << totalConstruct / 1000 << " ms (" << totalConstruct / iterations << " us/iter)";
    GTEST_LOG_(INFO) << "  Init:      " << totalInit / 1000 << " ms (" << totalInit / iterations << " us/iter)";
    GTEST_LOG_(INFO) << "  Stop:      " << totalStop / 1000 << " ms (" << totalStop / iterations << " us/iter)";
    GTEST_LOG_(INFO) << "  Release:   " << totalRelease / 1000 << " ms (" << totalRelease / iterations << " us/iter)";
    GTEST_LOG_(INFO) << "  Delete:    " << totalDelete / 1000 << " ms (" << totalDelete / iterations << " us/iter)";

    if (successCounter != iterations)
    {
        FAIL() << "Iterations made:" << iterations << " successful: " << successCounter << std::endl;
    }
}

TEST_F(Emulator_Test, MultiInstanceRun)
{
    int successCount = 0;
    const int numInstances = 5;

    for (int i = 0; i < numInstances; ++i) {
        std::cout << "Creating emulator instance " << i << std::endl;
        auto emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
        
        try {
            std::cout << "Initializing emulator " << i << std::endl;
            if (!emulator->Init()) {
                std::cout << "Failed to initialize emulator " << i << std::endl;
                continue;
            }
            
            std::cout << "Starting emulator " << i << std::endl;
            emulator->StartAsync();  // Use StartAsync instead of Start to avoid blocking
            
            // Give the thread time to start
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            if (!emulator->IsRunning()) {
                std::cout << "Emulator " << i << " failed to start" << std::endl;
                continue;
            }
            
            std::cout << "Emulator " << i << " is running" << std::endl;
            
            // Let it run for a short time
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            std::cout << "Stopping emulator " << i << std::endl;
            emulator->Stop();
            
            // Give it time to stop
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Verify it stopped
            if (emulator->IsRunning()) {
                std::cout << "Emulator " << i << " failed to stop" << std::endl;
                continue;
            }
            
            std::cout << "Emulator " << i << " stopped successfully" << std::endl;
            emulator->Release();  // Clean up resources
            successCount++;
            
        } catch (const std::exception& e) {
            std::cout << "Exception in emulator " << i << ": " << e.what() << std::endl;
        } catch (...) {
            std::cout << "Unknown exception in emulator " << i << std::endl;
        }
    }
    
    std::cout << "Test completed. Success count: " << successCount << std::endl;
    EXPECT_GE(successCount, 3) << "At least 3 instances should run successfully";
}
/// endregion </Emulator re-entrability tests>

/// region <Lifecycle tests>

/// @brief An emulator that was Release()d through one owner (EmulatorManager::RemoveEmulator) and destroyed
/// later through another (a UI widget's lingering shared_ptr) must not touch the ModuleLogger that died with
/// the context. Regression for the shutdown access violation in ~Emulator -> Release() -> MLOG*/SetState().
TEST(Emulator_Lifecycle_Test, ReleaseThenLateDestroy_DoesNotTouchFreedContext)
{
    std::shared_ptr<Emulator> owner = std::make_shared<Emulator>(LoggerLevel::LogDebug);  // LogDebug: MLOGDEBUG path is live
    ASSERT_TRUE(owner->Init());
    std::shared_ptr<Emulator> lingering = owner;  // e.g. DeviceScreen::_emulator

    owner->Release();  // what RemoveEmulator does before erasing its map entry
    owner.reset();

    // Late calls through the lingering reference must be harmless
    EXPECT_NO_THROW(lingering->GetState());
    EXPECT_NO_THROW(lingering->Release());  // idempotent, logs through a (now null) logger
    EXPECT_NO_THROW(lingering.reset());     // ~Emulator: must not Release() again into freed memory
}

/// endregion </Lifecycle tests>

/// region <Path shape tests>

#ifdef _WIN32
namespace
{
    /// Rewrite a local Windows path "X:\a\b" (or "X:/a/b") into the localhost admin-share UNC form:
    /// "//localhost/X$/a/b" (forwardSlashes) or "\\localhost\X$\a\b". Empty string when there is no drive letter.
    std::string ToLocalhostUNC(const std::string& localPath, bool forwardSlashes)
    {
        if (localPath.size() < 2 || !isalpha(static_cast<unsigned char>(localPath[0])) || localPath[1] != ':')
            return std::string();

        const char sep = forwardSlashes ? '/' : '\\';
        std::string result = {sep, sep};
        result += "localhost";
        result += sep;
        result += static_cast<char>(toupper(static_cast<unsigned char>(localPath[0])));
        result += '$';

        std::string rest = localPath.substr(2);
        if (rest.empty() || (rest[0] != '\\' && rest[0] != '/'))
            rest.insert(rest.begin(), sep);
        for (char c : rest)
            result += (c == '\\' || c == '/') ? sep : c;

        return result;
    }

    /// True when \\localhost\X$ for the drive of @p localPath is reachable (may be denied for non-admin users / CI).
    bool IsLocalhostAdminShareAccessible(const std::string& localPath)
    {
        std::string uncRoot = ToLocalhostUNC(localPath.substr(0, 2) + "\\", false);
        return !uncRoot.empty() && FileHelper::FolderExists(uncRoot);
    }
}  // namespace
#endif  // _WIN32

/// @brief Emulator::LoadSnapshot must accept every valid spelling of a snapshot path the host OS understands.
///
/// Regression for: a snapshot dropped from a macOS Samba share onto the Windows build arrived as
/// "//172.16.17.10/Macintosh HD/.../earshaver-1.sna" and was rejected with
/// "Snapshot file not found: '\172.16.17.10\Macintosh HD\...'" - the UNC prefix was mangled on the way to
/// FileExists(). Here the same file is loaded through every alternative spelling of its own path:
///   Windows: forward slashes, mixed separators, UNC admin share "//localhost/X$/..." and "\\localhost\X$\..."
///            (UNC variants are skipped when the admin share is not reachable, e.g. non-admin CI runner)
///   POSIX:   leading "//" (POSIX keeps it significant), backslash-separated, mixed separators
TEST(Emulator_PathShapes_Test, LoadSnapshot_AllPathSpellings)
{
    const std::string local = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    ASSERT_TRUE(FileHelper::FileExists(local)) << "Test data missing: " << local;

    std::vector<std::pair<std::string, std::string>> spellings;  // {description, path}

    spellings.push_back({"native", local});
    spellings.push_back({"forward slashes", FileHelper::NormalizePath(local, '/')});
    spellings.push_back({"backslashes", FileHelper::NormalizePath(local, '\\')});

    // Mixed separators: alternate '/' and '\' at every separator position
    {
        std::string mixed = local;
        bool forward = true;
        for (char& c : mixed)
        {
            if (c == '/' || c == '\\')
            {
                c = forward ? '/' : '\\';
                forward = !forward;
            }
        }
        spellings.push_back({"mixed separators", mixed});
    }

#ifdef _WIN32
    if (IsLocalhostAdminShareAccessible(local))
    {
        spellings.push_back({"UNC admin share, forward slashes", ToLocalhostUNC(local, true)});
        spellings.push_back({"UNC admin share, backslashes", ToLocalhostUNC(local, false)});
    }
    else
    {
        std::cout << "  (UNC admin share \\\\localhost\\X$ not reachable - UNC spellings skipped)" << std::endl;
    }
#else
    spellings.push_back({"double leading slash", "/" + FileHelper::NormalizePath(local, '/')});
#endif

    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
    ASSERT_NE(emu, nullptr);

    for (const auto& spelling : spellings)
    {
        EXPECT_TRUE(emu->LoadSnapshot(spelling.second)) << "LoadSnapshot failed for " << spelling.first << ": " << spelling.second;
    }

    EmulatorTestHelper::CleanupEmulator(emu);
}

/// @brief Non-ASCII paths: every std::string path in core is UTF-8 (QString::toStdString(), Lua, Python, web API
/// all hand over UTF-8). On Windows that must reach the OS as UTF-16 - the narrow CRT/Win32 calls would read the
/// bytes in the ANSI code page and fail for anything outside it. The snapshot is copied into
/// <temp>/unreal-ng-Снимки-日本語-🙂/Снимок.sna (directory created through std::filesystem's u8 path ctor,
/// independent of FileHelper) and then used through FileHelper, LoadSnapshot and SaveSnapshot via its UTF-8 spelling.
TEST(Emulator_PathShapes_Test, LoadAndSaveSnapshot_NonAsciiUtf8Path)
{
    namespace fs = std::filesystem;
    auto u8path = [](const std::string& utf8) { return fs::path(reinterpret_cast<const char8_t*>(utf8.c_str())); };
    auto u8str = [](const fs::path& p) { std::u8string s = p.u8string(); return std::string(reinterpret_cast<const char*>(s.c_str()), s.size()); };

    const std::string local = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    ASSERT_TRUE(FileHelper::FileExists(local)) << "Test data missing: " << local;

    const std::string utf8Dir = "unreal-ng-\xD0\xA1\xD0\xBD\xD0\xB8\xD0\xBC\xD0\xBA\xD0\xB8-\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E-\xF0\x9F\x99\x82";  // Снимки-日本語-🙂
    const std::string utf8Name = "\xD0\xA1\xD0\xBD\xD0\xB8\xD0\xBC\xD0\xBE\xD0\xBA.sna";                                                            // Снимок.sna
    const std::string utf8Copy = "\xD0\x9A\xD0\xBE\xD0\xBF\xD0\xB8\xD1\x8F.sna";                                                                    // Копия.sna

    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec) / u8path(utf8Dir);
    ASSERT_FALSE(ec) << "temp_directory_path failed";
    fs::create_directories(dir, ec);
    ASSERT_FALSE(ec) << "create_directories failed for " << u8str(dir);
    fs::copy_file(u8path(local), dir / u8path(utf8Name), fs::copy_options::overwrite_existing, ec);
    ASSERT_FALSE(ec) << "copy_file failed into " << u8str(dir);

    // The UTF-8 std::string spelling the GUI would hand over
    const std::string sep(1, FileHelper::GetPathSeparator());
    const std::string utf8Path = u8str(dir) + sep + utf8Name;
    const std::string utf8SavePath = u8str(dir) + sep + utf8Copy;

    // FileHelper primitives
    EXPECT_TRUE(FileHelper::FileExists(utf8Path)) << utf8Path;
    EXPECT_TRUE(FileHelper::FolderExists(u8str(dir))) << u8str(dir);
    EXPECT_EQ(FileHelper::GetFileSize(utf8Path), static_cast<size_t>(fs::file_size(dir / u8path(utf8Name))));
    FILE* f = FileHelper::OpenExistingFile(utf8Path, "rb");
    EXPECT_NE(f, nullptr) << "OpenExistingFile failed for " << utf8Path;
    if (f)
        fclose(f);

    // AbsolutePath with symlink/case resolution must give the file back in UTF-8, not mangled
    std::string resolved = FileHelper::AbsolutePath(utf8Path);
    EXPECT_NE(resolved.find(utf8Name), std::string::npos) << "AbsolutePath lost the UTF-8 file name: " << resolved;
    EXPECT_TRUE(FileHelper::FileExists(resolved)) << resolved;

    // Emulator load + save through non-ASCII paths
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
    ASSERT_NE(emu, nullptr);
    EXPECT_TRUE(emu->LoadSnapshot(utf8Path)) << "LoadSnapshot failed for " << utf8Path;
    EXPECT_TRUE(emu->SaveSnapshot(utf8SavePath)) << "SaveSnapshot failed for " << utf8SavePath;
    EXPECT_TRUE(fs::exists(dir / u8path(utf8Copy), ec)) << "Saved file not found under its UTF-8 name";
    EXPECT_TRUE(emu->LoadSnapshot(utf8SavePath)) << "Reloading the saved snapshot failed for " << utf8SavePath;
    EmulatorTestHelper::CleanupEmulator(emu);

    fs::remove_all(dir, ec);
}

/// endregion </Path shape tests>

