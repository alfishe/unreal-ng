#include "stdafx.h"
#include "pch.h"

#include "emulator_test.h"

#include "common/modulelogger.h"
#include "common/timehelper.h"
#include "emulator/emulator.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/emulatormanager.h"
#include "base/featuremanager.h"
#include "emulator/mainloop.h"
#include "emulator/sound/soundmanager.h"
#include "common/filehelper.h"
#include <atomic>
#include <stdexcept>
#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtiminghelper.h"
#include "_helpers/testwaithelper.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include <memory>
#include <thread>
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "3rdparty/message-center/messagecenter.h"

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
    constexpr int iterations = 20;

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
            
            // Wait (bounded) for the worker thread to actually reach the
            // mainloop. GetState()==StateRun is set by the worker AFTER its
            // startup flag handling, so a Stop() issued afterwards is
            // guaranteed to be honoured - unlike IsRunning(), which
            // StartAsync() raises before the worker even starts
            auto runDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (emulator->GetState() != StateRun && std::chrono::steady_clock::now() < runDeadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            if (!emulator->IsRunning()) {
                std::cout << "Emulator " << i << " failed to start" << std::endl;
                continue;
            }
            
            std::cout << "Emulator " << i << " is running" << std::endl;
            
            std::cout << "Stopping emulator " << i << std::endl;
            emulator->Stop();
            
            // Verify it stopped (Stop() joins the async thread synchronously)
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
    const fs::path dir = fs::path(TestPathHelper::GetUniqueTestScratchPath(utf8Dir));
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

/// region <File loaded notifications>

TEST_F(Emulator_Test, FailedLoadsPostNotificationWithOkFalse)
{
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    std::atomic<int> receivedCount{0};
    std::string capturedKind;
    bool capturedOk = true;

    uint64_t obsId = mc.AddObserver(NC_FILE_LOADED, [&](int, Message* msg) {
        if (!msg) return;
        auto* payload = dynamic_cast<FileLoadedPayload*>(msg->obj);
        if (payload && payload->emulatorId == emu->GetContext()->emulatorId)
        {
            capturedKind = payload->kind;
            capturedOk = payload->ok;
            receivedCount.fetch_add(1);
        }
    });

    // Attempt nonexistent snapshot load
    EXPECT_FALSE(emu->LoadSnapshot("/nonexistent/path/test.sna"));
    EXPECT_TRUE(WaitForCondition([&] { return receivedCount.load() >= 1; }));
    EXPECT_EQ(receivedCount.load(), 1);
    EXPECT_EQ(capturedKind, "snapshot");
    EXPECT_FALSE(capturedOk);

    // Attempt nonexistent tape load
    EXPECT_FALSE(emu->LoadTape("/nonexistent/path/test.tap"));
    EXPECT_TRUE(WaitForCondition([&] { return receivedCount.load() >= 2; }));
    EXPECT_EQ(receivedCount.load(), 2);
    EXPECT_EQ(capturedKind, "tape");
    EXPECT_FALSE(capturedOk);

    // Attempt nonexistent disk load
    EXPECT_FALSE(emu->LoadDisk("/nonexistent/path/test.trd"));
    EXPECT_TRUE(WaitForCondition([&] { return receivedCount.load() >= 3; }));
    EXPECT_EQ(receivedCount.load(), 3);
    EXPECT_EQ(capturedKind, "disk");
    EXPECT_FALSE(capturedOk);

    mc.RemoveObserverById(NC_FILE_LOADED, obsId);
    EmulatorTestHelper::CleanupEmulator(emu);
}

TEST_F(Emulator_Test, LoadSnapshot_EmitsSingleResetNotification)
{
    Emulator* emu = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emu, nullptr);

    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    std::atomic<int> resetCount{0};
    uint64_t obsId = mc.AddObserver(NC_SYSTEM_RESET, [&](int, Message* msg) {
        if (!msg) return;
        resetCount.fetch_add(1);
    });

    // Ensure all prior messages from emulator creation/boot are processed (FIFO barrier)
    std::atomic<bool> synced{false};
    uint64_t syncObsId = mc.AddObserver("TEST_DRAIN_BARRIER", [&](int, Message*) {
        synced.store(true);
    });
    mc.Post("TEST_DRAIN_BARRIER", new SimpleTextPayload("drain"));
    EXPECT_TRUE(WaitForCondition([&] { return synced.load(); }));
    mc.RemoveObserverById("TEST_DRAIN_BARRIER", syncObsId);

    // 1. Loading SNA snapshot must emit exactly one NC_SYSTEM_RESET notification
    resetCount.store(0);
    const std::string snaPath = TestPathHelper::GetTestDataPath("loaders/sna/multifix.sna");
    EXPECT_TRUE(emu->LoadSnapshot(snaPath));
    EXPECT_TRUE(WaitForCondition([&] { return resetCount.load() == 1; }));
    EXPECT_EQ(resetCount.load(), 1);

    // 2. Loading Z80 snapshot must emit exactly one NC_SYSTEM_RESET notification
    resetCount.store(0);
    const std::string z80Path = TestPathHelper::GetTestDataPath("loaders/z80/BBG128.z80");
    EXPECT_TRUE(emu->LoadSnapshot(z80Path));
    EXPECT_TRUE(WaitForCondition([&] { return resetCount.load() == 1; }));
    EXPECT_EQ(resetCount.load(), 1);

    mc.RemoveObserverById(NC_SYSTEM_RESET, obsId);
    EmulatorTestHelper::CleanupEmulator(emu);
}

/// endregion </File loaded notifications>

/// region <Realtime scheduling propagation tests>

/// @brief EmulatorManager must flag exactly one instance for real-time
/// scheduling: the selected one, or the sole instance while nothing is
/// selected (the GUI creates its single instance through the manager but
/// never calls SetSelectedEmulatorId). Non-active instances must never hold
/// the request - a bank of headless WebAPI instances must not compete with
/// the audible one for real-time priority. Exceeds the 50 ms budget: two
/// full Emulator::Init()s (ROM + device bring-up), same cost class as
/// MultiInstance above; no emulator thread is started - the flag updates
/// are synchronous manager operations.
TEST(Emulator_RealtimeScheduling_Test, SelectionDrivesRealtimeFlags)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();

    // Start from a clean selection - earlier tests in this binary may have
    // left one pointing at an instance they already removed
    ASSERT_TRUE(manager->SetSelectedEmulatorId(""));

    std::shared_ptr<Emulator> first = manager->CreateEmulatorWithModel("rt-flags-1", "48K", LoggerLevel::LogNone);
    std::shared_ptr<Emulator> second = manager->CreateEmulatorWithModel("rt-flags-2", "48K", LoggerLevel::LogNone);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    MainLoop* firstLoop = first->GetContext()->pMainLoop;
    MainLoop* secondLoop = second->GetContext()->pMainLoop;
    ASSERT_NE(firstLoop, nullptr);
    ASSERT_NE(secondLoop, nullptr);

    // Sole-instance fallback only applies while the instance IS alone
    EXPECT_FALSE(firstLoop->IsRealtimeRequested()) << "Second instance must cancel the sole-instance request";
    EXPECT_FALSE(secondLoop->IsRealtimeRequested());

    // Explicit selection grants the request to exactly one instance
    ASSERT_TRUE(manager->SetSelectedEmulatorId(second->GetId()));
    EXPECT_FALSE(firstLoop->IsRealtimeRequested());
    EXPECT_TRUE(secondLoop->IsRealtimeRequested());

    // Live switch between two existing instances (both may be running)
    ASSERT_TRUE(manager->SetSelectedEmulatorId(first->GetId()));
    EXPECT_TRUE(firstLoop->IsRealtimeRequested());
    EXPECT_FALSE(secondLoop->IsRealtimeRequested());

    // Clearing the selection must drop every request
    ASSERT_TRUE(manager->SetSelectedEmulatorId(""));
    EXPECT_FALSE(firstLoop->IsRealtimeRequested());
    EXPECT_FALSE(secondLoop->IsRealtimeRequested());

    // Removing the selected instance re-evaluates: the sole survivor takes
    // over the request via the fallback (the GUI-like single-instance end state)
    ASSERT_TRUE(manager->SetSelectedEmulatorId(second->GetId()));
    const std::string secondId = second->GetId();
    second.reset();
    ASSERT_TRUE(manager->RemoveEmulator(secondId));
    EXPECT_TRUE(firstLoop->IsRealtimeRequested()) << "Sole survivor should take over the realtime request";

    // Cleanup
    const std::string firstId = first->GetId();
    first.reset();
    ASSERT_TRUE(manager->RemoveEmulator(firstId));
    ASSERT_TRUE(manager->SetSelectedEmulatorId(""));
}

/// endregion </Realtime scheduling propagation tests>


/// region <Blank disks>

/// CreateBlankDisk: the format follows the machine unless asked (+3DOS on a +3, unformatted for the
/// machine's own FORMAT elsewhere), the image is owned like a loaded one and a second disk replaces it
TEST(Emulator_BlankDisk_Test, FormatFollowsTheMachine)
{
    Emulator* plus3 = EmulatorTestHelper::CreateStandardEmulator("PLUS3", LoggerLevel::LogError);
    ASSERT_NE(plus3, nullptr);
    EmulatorContext* context = plus3->GetContext();

    Emulator::BlankDiskResult created;
    ASSERT_TRUE(plus3->CreateBlankDisk(0, Emulator::BlankDiskFormat::Auto, 0, 0, nullptr, &created));
    EXPECT_EQ(created.format, Emulator::BlankDiskFormat::Plus3);
    EXPECT_EQ(created.cylinders, 40);
    EXPECT_EQ(created.sides, 1);

    DiskImage* image = context->coreState.diskDrives[0]->getDiskImage();
    ASSERT_NE(image, nullptr) << "owned by the core like a loaded image";
    EXPECT_EQ(context->coreState.diskDrives[0]->getDiskImage(), image);
    EXPECT_EQ(context->coreState.diskFilePaths[0], "<blank>");
    DiskImage::Track* track = image->getTrackForCylinderAndSide(39, 0);
    ASSERT_NE(track, nullptr);
    ASSERT_EQ(track->sectorCount(), 9u) << "+3DOS: 9 sectors per track";
    EXPECT_EQ(track->sectors()[0].dataSize, 512);
    EXPECT_EQ(track->sectors()[0].data[0], 0xE5);

    // A second disk replaces the first (the first image is released)
    ASSERT_TRUE(plus3->CreateBlankDisk(0, Emulator::BlankDiskFormat::Unformatted, 80, 2, nullptr, &created));
    EXPECT_EQ(created.format, Emulator::BlankDiskFormat::Unformatted);
    EXPECT_NE(context->coreState.diskDrives[0]->getDiskImage(), nullptr);
    EXPECT_EQ(context->coreState.diskDrives[0]->getDiskImage()->getCylinders(), 80);
    EmulatorTestHelper::CleanupEmulator(plus3);

    Emulator* pentagon = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(pentagon, nullptr);
    ASSERT_TRUE(pentagon->CreateBlankDisk(1, Emulator::BlankDiskFormat::Auto, 0, 0, nullptr, &created));
    EXPECT_EQ(created.format, Emulator::BlankDiskFormat::Unformatted);
    EXPECT_EQ(created.cylinders, 80);
    EXPECT_EQ(created.sides, 2);
    EmulatorTestHelper::CleanupEmulator(pentagon);
}

TEST(Emulator_BlankDisk_Test, RefusesWhatNoDriveTakes)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    std::string error;
    EXPECT_FALSE(emulator->CreateBlankDisk(4, Emulator::BlankDiskFormat::Auto, 0, 0, &error));
    EXPECT_NE(error.find("invalid drive"), std::string::npos) << error;
    EXPECT_FALSE(emulator->CreateBlankDisk(0, Emulator::BlankDiskFormat::Unformatted, 42, 0, &error));
    EXPECT_NE(error.find("cylinders"), std::string::npos) << error;
    EXPECT_FALSE(emulator->CreateBlankDisk(0, Emulator::BlankDiskFormat::Plus3, 40, 3, &error));
    EXPECT_NE(error.find("sides"), std::string::npos) << error;
    EXPECT_EQ(emulator->GetContext()->coreState.diskDrives[0]->getDiskImage(), nullptr) << "nothing inserted on a refusal";

    Emulator::BlankDiskFormat format = Emulator::BlankDiskFormat::Auto;
    EXPECT_TRUE(Emulator::ParseBlankDiskFormat("PLUS3", format));
    EXPECT_EQ(format, Emulator::BlankDiskFormat::Plus3);
    EXPECT_TRUE(Emulator::ParseBlankDiskFormat("unformatted", format));
    EXPECT_EQ(format, Emulator::BlankDiskFormat::Unformatted);
    EXPECT_FALSE(Emulator::ParseBlankDiskFormat("trdos", format));

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// endregion </Blank disks>

/// region <Breakpoints during a direct run (Emulator::OnBreakpointHit)>

// A control thread stepping a paused emulator (WebAPI /step, CLI, DeZog, Lua, Python) must never park on a
// breakpoint: nothing would resume it. The breakpoint ends the run instead, and the caller learns which
namespace
{
// $8000 LD A,1 / $8002 LD ($9000),A / $8005 LD A,2 / $8007 XOR A / $8008 OUT ($FE),A / $800A NOP / $800B JR $
constexpr uint16_t kProgram = 0x8000;
constexpr uint16_t kLdA2 = 0x8005;
constexpr uint16_t kTarget = 0x9000;
const uint8_t kProgramBytes[] = {0x3E, 0x01, 0x32, 0x00, 0x90, 0x3E, 0x02, 0xAF, 0xD3, 0xFE, 0x00, 0x18, 0xFE};

std::unique_ptr<Emulator> DirectRunEmulator()
{
    auto emulator = std::make_unique<Emulator>(LoggerLevel::LogError);
    if (!emulator->Init())
        return nullptr;
    emulator->DebugOn();
    // Memory breakpoints need the breakpoints feature too (Memory's feature cache)
    EmulatorContext* context = emulator->GetContext();
    context->pFeatureManager->setFeature(Features::kDebugMode, true);
    context->pFeatureManager->setFeature(Features::kBreakpoints, true);
    Memory* memory = emulator->GetMemory();
    memory->UpdateFeatureCache();
    for (size_t i = 0; i < sizeof(kProgramBytes); ++i)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(kProgram + i), kProgramBytes[i]);
    Z80State* z80 = emulator->GetZ80State();
    z80->pc = kProgram;
    z80->iff1 = z80->iff2 = 0;  // no frame interrupt in the way
    return emulator;
}

uint16_t AddBreakpoint(Emulator& emulator, BreakpointTypeEnum type, uint8_t access, uint16_t address)
{
    auto* breakpoint = new BreakpointDescriptor();
    breakpoint->type = type;
    if (type == BreakpointTypeEnum::BRK_IO)
        breakpoint->ioType = access;
    else
        breakpoint->memoryType = access;
    breakpoint->z80address = address;
    return emulator.GetBreakpointManager()->AddBreakpoint(breakpoint);
}
}  // namespace

TEST(Emulator_DirectRunBreakpoint_Test, ExecutionBreakpointStopsBeforeItsInstruction)
{
    auto emulator = DirectRunEmulator();
    ASSERT_NE(emulator, nullptr);
    const uint16_t id = AddBreakpoint(*emulator, BreakpointTypeEnum::BRK_MEMORY, BRK_MEM_EXECUTE, kLdA2);

    EXPECT_EQ(emulator->RunNCPUCycles(10, false), 2u);
    Z80State* z80 = emulator->GetZ80State();
    EXPECT_EQ(z80->pc, kLdA2);
    EXPECT_EQ(z80->a, 1) << "LD A,2 must not have run";
    const auto& stop = emulator->LastDirectStop();
    EXPECT_TRUE(stop.hit);
    EXPECT_EQ(stop.breakpointId, id);
    EXPECT_EQ(stop.address, kLdA2);
    EXPECT_EQ(stop.kind, BreakpointHitKind::Execute);

    // Stepping on from the breakpoint it stopped at runs LD A,2; nothing stops the next steps
    emulator->RunSingleCPUCycle(false);
    EXPECT_EQ(z80->pc, kLdA2 + 2);
    EXPECT_EQ(z80->a, 2);
    EXPECT_FALSE(emulator->LastDirectStop().hit);
    EXPECT_EQ(emulator->RunNCPUCycles(2, false), 2u);
    emulator->Release();
}

TEST(Emulator_DirectRunBreakpoint_Test, BreakpointAtTheStartIsHitWhenNotStoppedThere)
{
    // A fresh machine sits at $8000 without having stopped there: the breakpoint fires first,
    // then the next step passes it
    auto emulator = DirectRunEmulator();
    ASSERT_NE(emulator, nullptr);
    AddBreakpoint(*emulator, BreakpointTypeEnum::BRK_MEMORY, BRK_MEM_EXECUTE, kProgram);
    Z80State* z80 = emulator->GetZ80State();

    emulator->RunSingleCPUCycle(false);
    EXPECT_EQ(z80->pc, kProgram);
    EXPECT_TRUE(emulator->LastDirectStop().hit);

    emulator->RunSingleCPUCycle(false);
    EXPECT_EQ(z80->pc, kProgram + 2);
    EXPECT_FALSE(emulator->LastDirectStop().hit);
    emulator->Release();
}

/// A hit-count breakpoint counts each arrival once: stepping on from where it stopped is not another hit
TEST(Emulator_DirectRunBreakpoint_Test, HitCountIgnoresSteppingOnFromTheStop)
{
    auto emulator = DirectRunEmulator();
    ASSERT_NE(emulator, nullptr);
    constexpr uint16_t kLoop = kProgram + 11;  // JR $: an endless loop on itself
    BreakpointSpec spec;
    spec.access = BRK_MEM_EXECUTE;
    spec.address = kLoop;
    spec.hitMode = BRK_HIT_EQUAL;
    spec.hitTarget = 3;
    std::string error;
    BreakpointManager& brk = *emulator->GetBreakpointManager();
    const uint16_t id = brk.AddBreakpoint(spec, error);
    ASSERT_NE(id, BRK_INVALID) << error;
    const BreakpointDescriptor& bp = *brk.GetAllBreakpoints().at(id);

    // Six instructions, then the loop: arrivals 1 and 2 run on, the 3rd stops before the JR
    EXPECT_EQ(emulator->RunNCPUCycles(100, false), 8u);
    EXPECT_TRUE(emulator->LastDirectStop().hit);
    EXPECT_EQ(bp.hitCount, 3u);

    emulator->RunSingleCPUCycle(false);  // the JR runs: leaving the stop is not an arrival
    EXPECT_EQ(bp.hitCount, 3u);
    EXPECT_FALSE(emulator->LastDirectStop().hit);
    emulator->RunSingleCPUCycle(false);  // the next arrival counts, and the 4th does not stop
    EXPECT_EQ(bp.hitCount, 4u);
    EXPECT_FALSE(emulator->LastDirectStop().hit);
    emulator->Release();
}

TEST(Emulator_DirectRunBreakpoint_Test, MemoryAndPortBreakpointsEndTheRunAfterTheirInstruction)
{
    auto emulator = DirectRunEmulator();
    ASSERT_NE(emulator, nullptr);
    const uint16_t write = AddBreakpoint(*emulator, BreakpointTypeEnum::BRK_MEMORY, BRK_MEM_WRITE, kTarget);
    const uint16_t out = AddBreakpoint(*emulator, BreakpointTypeEnum::BRK_IO, BRK_IO_OUT, 0xFE);
    Z80State* z80 = emulator->GetZ80State();

    EXPECT_EQ(emulator->RunNCPUCycles(10, false), 2u) << "LD ($9000),A completes, then the run ends";
    EXPECT_EQ(z80->pc, kLdA2);
    EXPECT_EQ(emulator->GetMemory()->DirectReadFromZ80Memory(kTarget), 1);
    EXPECT_EQ(emulator->LastDirectStop().breakpointId, write);
    EXPECT_EQ(emulator->LastDirectStop().kind, BreakpointHitKind::MemoryWrite);
    EXPECT_EQ(emulator->LastDirectStop().address, kTarget);

    EXPECT_EQ(emulator->RunNCPUCycles(10, false), 3u) << "LD A,2 / XOR A / OUT ($FE),A";
    EXPECT_EQ(z80->pc, 0x800A);
    EXPECT_EQ(emulator->LastDirectStop().breakpointId, out);
    EXPECT_EQ(emulator->LastDirectStop().kind, BreakpointHitKind::PortOut);
    emulator->Release();
}

TEST(Emulator_DirectRunBreakpoint_Test, StepFromABreakpointPauseNeverParksTheCaller)
{
    // The emulation thread runs into an execution breakpoint and parks there (the emulator's own run). A step
    // from a control thread then used to park the caller on the same breakpoint forever (WebAPI's workers);
    // now it runs the instruction and returns. The pause carries its cause, the step's end its outcome
    auto emulator = DirectRunEmulator();
    ASSERT_NE(emulator, nullptr);
    const uint16_t id = AddBreakpoint(*emulator, BreakpointTypeEnum::BRK_MEMORY, BRK_MEM_EXECUTE, kLdA2);

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    std::atomic<int> pauseCause{-1};
    std::atomic<int> pauseAddress{-1};
    std::atomic<int> stepPayloads{0};
    std::atomic<int> stepStopped{-1};
    const std::string emulatorId = emulator->GetId();
    const uint64_t stateObserver = messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, [&](int, Message* message) {
        auto* payload = message ? dynamic_cast<EmulatorStateChangePayload*>(message->obj) : nullptr;
        if (payload && payload->_payloadNumber == StatePaused && payload->emulatorId.toString() == emulatorId)
        {
            pauseCause.store(static_cast<int>(payload->pauseCause));
            pauseAddress.store(payload->address);
        }
    });
    const uint64_t stepObserver = messageCenter.AddObserver(NC_EXECUTION_CPU_STEP, [&](int, Message* message) {
        auto* payload = message ? dynamic_cast<CpuStepPayload*>(message->obj) : nullptr;
        if (payload && payload->emulatorId.toString() == emulatorId)
        {
            stepStopped.store(payload->stopped ? 1 : 0);
            stepPayloads.fetch_add(1);
        }
    });

    emulator->StartAsync();
    Z80State* z80 = emulator->GetZ80State();
    ASSERT_TRUE(TestWait::For([&] { return emulator->IsPaused() && z80->pc == kLdA2; }, std::chrono::seconds(2)));
    EXPECT_TRUE(TestWait::For([&] { return pauseCause.load() >= 0; }, std::chrono::milliseconds(500)));
    EXPECT_EQ(pauseCause.load(), static_cast<int>(PauseCause::Breakpoint));
    EXPECT_EQ(pauseAddress.load(), kLdA2);

    std::atomic<bool> returned{false};
    std::thread control([&] {
        emulator->RunSingleCPUCycle(false);
        returned.store(true);
    });
    const bool done = TestWait::For([&] { return returned.load(); }, std::chrono::seconds(2));
    if (!done)
        emulator->Resume();  // unpark the caller so the thread can be joined
    control.join();
    EXPECT_TRUE(done) << "the step parked on the breakpoint";
    EXPECT_EQ(z80->pc, kLdA2 + 2);
    EXPECT_FALSE(emulator->LastDirectStop().hit);
    EXPECT_TRUE(TestWait::For([&] { return stepPayloads.load() > 0; }, std::chrono::milliseconds(500)));
    EXPECT_EQ(stepStopped.load(), 0);
    EXPECT_EQ(emulator->GetBreakpointManager()->GetBreakpointById(id) != nullptr, true);

    messageCenter.RemoveObserverById(NC_EMULATOR_STATE_CHANGE, stateObserver);
    messageCenter.RemoveObserverById(NC_EXECUTION_CPU_STEP, stepObserver);
    emulator->Stop();
    emulator->Release();
}

/// endregion </Breakpoints during a direct run>

/// region <Host audio during direct runs>

/// API run_frames and every other direct run go at full host speed: on every machine, nothing reaches the host
/// audio callback for their duration (a SoundManager::HostOutputHold), and delivery resumes once they return.
/// Turbo mode holds it the same way, once however often it is enabled
class EmulatorHostAudio_Test : public ::testing::TestWithParam<const char*>
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    size_t _calls = 0;

    static void Count(void* obj, int16_t*, size_t) { ++*static_cast<size_t*>(obj); }

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(GetParam(), LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create " << GetParam();
        _context = _emulator->GetContext();
        ASSERT_NE(_context->pSoundManager, nullptr);
        _context->pAudioManagerObj.store(&_calls, std::memory_order_release);
        _context->pAudioCallback.store(&Count, std::memory_order_release);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

INSTANTIATE_TEST_SUITE_P(Models, EmulatorHostAudio_Test, ::testing::Values("48K", "PENTAGON", "TSL", "SPRINTER"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });

TEST_P(EmulatorHostAudio_Test, DirectRunsHoldHostOutput)
{
    SoundManager* sound = _context->pSoundManager;
    const uint64_t heldBefore = sound->hostFramesHeld();

    _emulator->RunNFrames(3);
    EXPECT_EQ(_calls, 0u) << "run_frames handed audio to the host";
    EXPECT_GE(sound->hostFramesHeld() - heldBefore, 3u) << "run_frames frames did not pass the host boundary held";
    EXPECT_FALSE(sound->isHostOutputHeld()) << "the hold outlived run_frames";

    _emulator->RunTStates(_context->config.frame + 100);
    _emulator->RunUntilScanline(10);
    _emulator->RunSingleCPUCycle();
    EXPECT_EQ(_calls, 0u) << "a direct run handed audio to the host";
    EXPECT_FALSE(sound->isHostOutputHeld());

    // Control: a frame end outside a direct run (the paced main loop) is delivered
    sound->handleFrameStart();
    sound->handleFrameEnd();
    EXPECT_EQ(_calls, 1u) << "delivery did not resume after the direct runs";
}

TEST_P(EmulatorHostAudio_Test, TurboHoldsHostOutputOnce)
{
    SoundManager* sound = _context->pSoundManager;
    _emulator->EnableTurboMode();
    _emulator->EnableTurboMode();  // a repeated enable must not stack a second hold
    EXPECT_TRUE(sound->isHostOutputHeld());
    _emulator->DisableTurboMode();
    EXPECT_FALSE(sound->isHostOutputHeld()) << "turbo left the host output held";
    EXPECT_FALSE(sound->isMuted());
    _emulator->DisableTurboMode();
    EXPECT_FALSE(sound->isHostOutputHeld());
}

TEST_P(EmulatorHostAudio_Test, ExceptionInsideADirectRunReleasesTheHold)
{
    // A direct run that throws half-way (a predicate, a device) unwinds through its scope: the hold goes with it
    SoundManager* sound = _context->pSoundManager;
    unsigned steps = 0;
    EXPECT_THROW(_emulator->RunUntilCondition(
                     [&](const Z80State&) -> bool {
                         EXPECT_TRUE(sound->isHostOutputHeld()) << "the run is not held while it runs";
                         if (++steps == 3)
                             throw std::runtime_error("predicate failed");
                         return false;
                     },
                     _context->config.frame * 4),
                 std::runtime_error);
    EXPECT_FALSE(sound->isHostOutputHeld()) << "an exception inside a direct run leaked its hold";
    EXPECT_FALSE(_emulator->IsDirectStepping());
    EXPECT_EQ(sound->hostOutputHolds(SoundManager::HostHoldReason::DirectRun), 0);

    sound->handleFrameStart();
    sound->handleFrameEnd();
    EXPECT_EQ(_calls, 1u) << "delivery did not resume after the failed run";
}

TEST_P(EmulatorHostAudio_Test, TurboNeverTouchesTheUserMute)
{
    // Turbo holds the host output; it used to mute() / unmute() the user's master mute as well, so leaving turbo
    // unmuted a user who had muted, and a TTD replay that saved the mute during turbo restored it set for good
    SoundManager* sound = _context->pSoundManager;
    sound->mute();
    _emulator->EnableTurboMode();
    _emulator->DisableTurboMode();
    EXPECT_TRUE(sound->isMuted()) << "leaving turbo unmuted the user";
    sound->unmute();
    _emulator->EnableTurboMode();
    EXPECT_FALSE(sound->isMuted());
    EXPECT_EQ(sound->hostOutputHolds(SoundManager::HostHoldReason::Turbo), 1);
    _emulator->DisableTurboMode();
    EXPECT_FALSE(sound->isHostOutputHeld());
}

namespace
{
/// The host audio callback as a frontend sees it: frames handed over, and of those, frames with any sound
struct HostAudioSink
{
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> audible{0};

    static void Collect(void* obj, int16_t* samples, size_t count)
    {
        auto* sink = static_cast<HostAudioSink*>(obj);
        sink->calls.fetch_add(1, std::memory_order_relaxed);
        if (std::any_of(samples, samples + count, [](int16_t v) { return v != 0; }))
            sink->audible.fetch_add(1, std::memory_order_relaxed);
    }
};

/// DI; loop: OUT (#FE),#10; delay; OUT (#FE),#00; delay; JR loop - a beeper square wave, forever
constexpr uint8_t kBeeperSquare[] = {0xF3, 0x3E, 0x10, 0xD3, 0xFE, 0x06, 0x40, 0x10, 0xFE,
                                     0xAF, 0xD3, 0xFE, 0x06, 0x40, 0x10, 0xFE, 0x18, 0xEF};
constexpr uint16_t kBeeperAt = 0x8000;
}  // namespace

/// The owner's report: a machine playing sound, paused via automation, resumed - and silent. Every pause / step /
/// turbo / leak path, then a resume: the speakers get audible frames again. Drives the real, paced main loop:
/// each check waits for a few emulated frames at 20 ms each (~0.5 s in all), the only way to see what a running
/// machine hands the host
class EmulatorHostAudioResume_Test : public ::testing::Test
{
protected:
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    SoundManager* _sound = nullptr;
    HostAudioSink _sink;

    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("host-audio-resume", "PENTAGON",
                                                                            LoggerLevel::LogError);
        ASSERT_TRUE(_emulator);
        _context = _emulator->GetContext();
        _sound = _context->pSoundManager;
        ASSERT_NE(_sound, nullptr);
        for (size_t i = 0; i < sizeof(kBeeperSquare); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(kBeeperAt + i), kBeeperSquare[i]);
        _emulator->GetZ80State()->pc = kBeeperAt;
        _context->pAudioManagerObj.store(&_sink, std::memory_order_release);
        _context->pAudioCallback.store(&HostAudioSink::Collect, std::memory_order_release);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
            _emulator.reset();
        }
    }

    /// The speakers get sound: audible frames keep arriving, and nothing holds the output
    void ExpectAudible(const char* after)
    {
        const uint64_t audible = _sink.audible.load();
        EXPECT_TRUE(TestWait::For([&] { return _sink.audible.load() >= audible + 3; }, std::chrono::seconds(3)))
            << "no sound after " << after << " (host frames " << _sink.calls.load() << ", audible "
            << _sink.audible.load() << ", held " << _sound->isHostOutputHeld() << ")";
        EXPECT_FALSE(_sound->isHostOutputHeld()) << after;
        for (size_t i = 0; i < SoundManager::kHostHoldReasons; i++)
            EXPECT_EQ(_sound->hostOutputHolds(static_cast<SoundManager::HostHoldReason>(i)), 0) << after;
        EXPECT_FALSE(_sound->isMuted()) << after;
    }
};

TEST_F(EmulatorHostAudioResume_Test, EveryPausePathThenResumePlaysAgain)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    const std::string id = _emulator->GetId();
    _emulator->StartAsync();
    ExpectAudible("start");

    // WebAPI POST /pause, /resume and MCP emulator_control pause / resume (it forwards to them): EmulatorManager
    ASSERT_TRUE(manager->PauseEmulator(id));
    _emulator->RunNFrames(2);            // run_frames while paused
    _emulator->RunSingleCPUCycle();      // step
    _emulator->RunNCPUCycles(5);         // step_n
    _emulator->RunTStates(1000);         // run_tstates
    EXPECT_FALSE(_sound->isHostOutputHeld()) << "a direct run left the host output held";
    ASSERT_TRUE(manager->ResumeEmulator(id));
    ExpectAudible("WebAPI / MCP pause, steps, resume");

    // CLI pause / resume and the GUI: Emulator::Pause / Resume
    _emulator->Pause();
    _emulator->RunFrame();
    _emulator->Resume();
    ExpectAudible("CLI / GUI pause, run_frame, resume");

    // A step on a running machine pauses it itself; the resume after it plays again
    _emulator->RunSingleCPUCycle();
    ASSERT_TRUE(_emulator->IsPaused());
    _emulator->Resume();
    ExpectAudible("step on a running machine, resume");

    // Turbo on and off while running, and across a pause
    _emulator->EnableTurboMode();
    EXPECT_TRUE(_sound->isHostOutputHeld());
    _emulator->DisableTurboMode();
    ExpectAudible("turbo on / off");
    _emulator->Pause();
    _emulator->EnableTurboMode();
    _emulator->DisableTurboMode();
    _emulator->Resume();
    ExpectAudible("turbo on / off while paused, resume");

    // A holder that leaked its hold (the regression): the resume finds it without its reason and drops it
    _emulator->Pause();
    auto leaked = std::make_unique<SoundManager::HostOutputHold>(_sound, SoundManager::HostHoldReason::DirectRun);
    _emulator->Resume();
    ExpectAudible("a leaked hold, resume");
    EXPECT_EQ(_sound->hostOutputStaleHoldsCleared(), 1u);
    leaked.reset();  // its late release takes nothing from anyone
    EXPECT_FALSE(_sound->isHostOutputHeld());
}

/// A step over a CALL resumes the machine to a temporary breakpoint, so it runs paced to real time - but it is a
/// debugger step, and every step is silent: the beeper a stepped-over subroutine plays must not reach the speakers.
/// The hold ends with the step; a normal resume plays again. (~0.15 s: the subroutine is a real, paced run)
TEST_F(EmulatorHostAudioResume_Test, StepOverASoundingSubroutineIsSilent)
{
    // DI; CALL #8010; JR $   ...   #8010: a short beeper square wave (the subroutine), then RET
    const uint8_t program[] = {0xF3, 0xCD, 0x10, 0x80, 0x18, 0xFE};
    const uint8_t subroutine[] = {0x11, 0x00, 0x02,                                // LD DE,#0200
                                  0x3E, 0x10, 0xD3, 0xFE, 0x06, 0x20, 0x10, 0xFE,  // OUT (#FE),#10; delay
                                  0xAF, 0xD3, 0xFE, 0x06, 0x20, 0x10, 0xFE,        // OUT (#FE),0; delay
                                  0x1B, 0x7A, 0xB3, 0x20, 0xEB,                    // DEC DE; LD A,D; OR E; JR NZ
                                  0xC9};
    for (size_t i = 0; i < sizeof(program); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    for (size_t i = 0; i < sizeof(subroutine); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8010 + i), subroutine[i]);

    _emulator->DebugOn();
    _emulator->StartAsync();
    _emulator->Pause();
    Z80State* z80 = _emulator->GetZ80State();
    z80->pc = 0x8001;  // the CALL
    z80->sp = 0xFF00;

    const uint64_t audibleBefore = _sink.audible.load();
    _emulator->StepOver();
    EXPECT_TRUE(_sound->isHostOutputHeld()) << "the stepped-over run is not held";
    ASSERT_TRUE(TestWait::For([&] { return _emulator->IsPaused() && z80->pc == 0x8004; }, std::chrono::seconds(5)))
        << "the step over did not stop after the CALL, pc " << z80->pc;
    EXPECT_EQ(_sink.audible.load(), audibleBefore) << "the subroutine's beeper reached the speakers during a step";
    EXPECT_FALSE(_sound->isHostOutputHeld()) << "the hold outlived the step";
    EXPECT_EQ(_sound->hostOutputHolds(SoundManager::HostHoldReason::DirectRun), 0);
}

/// StepOver registers a handler on the shared BREAKPOINT topic that captures its emulator and FeatureManager. It must
/// be gone with the emulator: a breakpoint event of another instance with a colliding id used to reach the dead
/// handler, which locked the destroyed FeatureManager's mutex and aborted the process
TEST(EmulatorStepOverObserver_Test, DestroyedEmulatorLeavesNoHandlerBehind)
{
    MessageCenter& center = MessageCenter::DefaultMessageCenter();
    const size_t observersBefore = center.ObserverCount(NC_EXECUTION_BREAKPOINT);
    {
        auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("stepover-observer", "PENTAGON",
                                                                                LoggerLevel::LogError);
        ASSERT_TRUE(emulator);
        const uint8_t program[] = {0xF3, 0xCD, 0x10, 0x80, 0x18, 0xFE};  // DI; CALL #8010; JR $
        for (size_t i = 0; i < sizeof(program); i++)
            emulator->GetContext()->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        emulator->GetContext()->pMemory->DirectWriteToZ80Memory(0x8010, 0xC9);  // RET
        emulator->DebugOn();
        emulator->StartAsync();
        emulator->Pause();
        emulator->GetZ80State()->pc = 0x8001;  // the CALL
        emulator->GetZ80State()->sp = 0xFF00;
        emulator->StepOver();  // registers the handler, resumes to the temporary breakpoint
        EXPECT_GT(center.ObserverCount(NC_EXECUTION_BREAKPOINT), observersBefore) << "StepOver registered no handler: the test checks nothing";
        emulator->Stop();
        EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
    }
    EXPECT_EQ(center.ObserverCount(NC_EXECUTION_BREAKPOINT), observersBefore) << "a step-over handler outlived its emulator";
}

TEST(EmulatorHostAudioTTD_Test, SeekThenResumeIsHeardAgain)
{
    // A TTD seek replays from a checkpoint at host speed under the replay hold; afterwards nothing holds the
    // output and the user's mute is as it was
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    SoundManager* sound = context->pSoundManager;
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    ASSERT_NE(ttd, nullptr);
    FeatureManager* features = emulator->GetFeatureManager();
    features->setFeature(Features::kDebugMode, true);
    features->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();

    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(4);
    ttd->StopRecording();
    const uint64_t replayHolds = sound->hostOutputHoldsTaken(SoundManager::HostHoldReason::TtdReplay);
    ASSERT_TRUE(ttd->SeekTo({2, 1000}));
    EXPECT_GT(sound->hostOutputHoldsTaken(SoundManager::HostHoldReason::TtdReplay), replayHolds)
        << "the seek replayed without the replay hold";
    EXPECT_FALSE(sound->isHostOutputHeld()) << "the seek left the host output held";
    EXPECT_FALSE(sound->isMuted()) << "the seek left the user's mute set";

    size_t calls = 0;
    context->pAudioManagerObj.store(&calls, std::memory_order_release);
    context->pAudioCallback.store([](void* obj, int16_t*, size_t) { ++*static_cast<size_t*>(obj); },
                                  std::memory_order_release);
    sound->handleFrameStart();
    sound->handleFrameEnd();
    EXPECT_EQ(calls, 1u) << "a frame after the seek did not reach the host";
    context->pAudioCallback.store(nullptr, std::memory_order_release);
    context->pAudioManagerObj.store(nullptr, std::memory_order_release);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// endregion </Host audio during direct runs>
