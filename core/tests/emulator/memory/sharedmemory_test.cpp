#include "sharedmemory_test.h"

#include <chrono>
#include <cstring>
#include <thread>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

// Platform-specific includes for shared memory verification
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/// region <SetUp / TearDown>

void SharedMemory_Test::SetUp()
{
    // Create a fully initialized emulator for each test
    _emulator = new Emulator(LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr);

    bool initialized = _emulator->Init();
    ASSERT_TRUE(initialized) << "Failed to initialize emulator";
}

void SharedMemory_Test::TearDown()
{
    if (_emulator != nullptr)
    {
        _emulator->Stop();
        _emulator->Release();
        delete _emulator;
        _emulator = nullptr;
    }
}

/// endregion </Setup / TearDown>

/// region <Helper methods>

Memory* SharedMemory_Test::GetMemory()
{
    if (_emulator == nullptr)
        return nullptr;

    EmulatorContext* context = _emulator->GetContext();
    if (context == nullptr)
        return nullptr;

    return context->pMemory;
}

FeatureManager* SharedMemory_Test::GetFeatureManager()
{
    if (_emulator == nullptr)
        return nullptr;

    return _emulator->GetFeatureManager();
}

bool SharedMemory_Test::IsSharedMemoryActive()
{
    Memory* memory = GetMemory();
    if (memory == nullptr)
        return false;

    // Check if the internal filepath is set (indicates shared memory is in use)
    return !memory->GetMappedMemoryFilepath().empty();
}

void SharedMemory_Test::WriteTestPattern(uint8_t* base, size_t size, uint8_t seed)
{
    if (base == nullptr)
        return;

    for (size_t i = 0; i < size; i++)
    {
        base[i] = static_cast<uint8_t>((i + seed) & 0xFF);
    }
}

bool SharedMemory_Test::VerifyTestPattern(const uint8_t* base, size_t size, uint8_t seed)
{
    if (base == nullptr)
        return false;

    for (size_t i = 0; i < size; i++)
    {
        uint8_t expected = static_cast<uint8_t>((i + seed) & 0xFF);
        if (base[i] != expected)
        {
            return false;
        }
    }
    return true;
}

/// endregion </Helper methods>

/// region <Feature Default State Tests>

TEST_F(SharedMemory_Test, DefaultStateIsDisabled)
{
    // Verify that sharedmemory feature is OFF by default
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    bool isEnabled = fm->isEnabled(Features::kSharedMemory);
    EXPECT_FALSE(isEnabled) << "sharedmemory feature should be OFF by default";

    // Verify that shared memory is not in use
    EXPECT_FALSE(IsSharedMemoryActive()) << "Shared memory should not be active when feature is disabled";
}

TEST_F(SharedMemory_Test, MemoryAllocatedAsHeapByDefault)
{
    // When sharedmemory is disabled, memory should be heap-allocated
    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Memory should exist
    EXPECT_NE(memory->RAMBase(), nullptr);
    EXPECT_NE(memory->ROMBase(), nullptr);

    // But shared memory filepath should be empty
    EXPECT_TRUE(memory->GetMappedMemoryFilepath().empty());
}

/// endregion </Feature Default State Tests>

/// region <Feature Enable Tests>

TEST_F(SharedMemory_Test, EnableFeatureAllocatesSharedMemory)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Store original memory base addresses
    uint8_t* originalRAMBase = memory->RAMBase();
    uint8_t* originalROMBase = memory->ROMBase();

    // Enable the feature
    fm->setFeature(Features::kSharedMemory, true);

    // Trigger feature cache update
    memory->UpdateFeatureCache();

    // Verify the feature is now enabled
    EXPECT_TRUE(fm->isEnabled(Features::kSharedMemory));

    // Verify shared memory is now active
    EXPECT_TRUE(IsSharedMemoryActive()) << "Shared memory should be active after enabling feature";

    // Memory base addresses may have changed but should still be valid
    EXPECT_NE(memory->RAMBase(), nullptr);
    EXPECT_NE(memory->ROMBase(), nullptr);
}

TEST_F(SharedMemory_Test, EnableFeaturePreservesMemoryContent)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Write a test pattern to memory before enabling
    const size_t testSize = 1024;  // Test first 1KB
    const uint8_t testSeed = 0x42;
    WriteTestPattern(memory->RAMBase(), testSize, testSeed);

    // Verify the pattern was written
    ASSERT_TRUE(VerifyTestPattern(memory->RAMBase(), testSize, testSeed));

    // Enable the feature
    fm->setFeature(Features::kSharedMemory, true);
    memory->UpdateFeatureCache();

    // Verify memory content was preserved after transition
    EXPECT_TRUE(VerifyTestPattern(memory->RAMBase(), testSize, testSeed))
        << "Memory content should be preserved when enabling shared memory";
}

/// endregion </Feature Enable Tests>

/// region <Feature Disable Tests>

TEST_F(SharedMemory_Test, DisableFeatureDeallocatesSharedMemory)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // First enable the feature
    fm->setFeature(Features::kSharedMemory, true);
    memory->UpdateFeatureCache();
    ASSERT_TRUE(IsSharedMemoryActive());

    // Now disable the feature
    fm->setFeature(Features::kSharedMemory, false);
    memory->UpdateFeatureCache();

    // Verify the feature is now disabled
    EXPECT_FALSE(fm->isEnabled(Features::kSharedMemory));

    // Verify shared memory is no longer active
    EXPECT_FALSE(IsSharedMemoryActive()) << "Shared memory should not be active after disabling feature";
}

TEST_F(SharedMemory_Test, DisableFeaturePreservesMemoryContent)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Enable the feature first
    fm->setFeature(Features::kSharedMemory, true);
    memory->UpdateFeatureCache();
    ASSERT_TRUE(IsSharedMemoryActive());

    // Write a test pattern to shared memory
    const size_t testSize = 1024;
    const uint8_t testSeed = 0x5A;
    WriteTestPattern(memory->RAMBase(), testSize, testSeed);
    ASSERT_TRUE(VerifyTestPattern(memory->RAMBase(), testSize, testSeed));

    // Now disable the feature
    fm->setFeature(Features::kSharedMemory, false);
    memory->UpdateFeatureCache();

    // Verify memory content was preserved after transition back to heap
    EXPECT_TRUE(VerifyTestPattern(memory->RAMBase(), testSize, testSeed))
        << "Memory content should be preserved when disabling shared memory";
}

/// endregion </Feature Disable Tests>

/// region <Feature Toggle Tests>

TEST_F(SharedMemory_Test, RapidToggleDoesNotCrash)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Rapidly toggle the feature
    for (int i = 0; i < 10; i++)
    {
        fm->setFeature(Features::kSharedMemory, true);
        memory->UpdateFeatureCache();

        fm->setFeature(Features::kSharedMemory, false);
        memory->UpdateFeatureCache();
    }

    // Memory should still be valid and accessible
    EXPECT_NE(memory->RAMBase(), nullptr);
    EXPECT_NE(memory->ROMBase(), nullptr);
}

/// endregion </Feature Toggle Tests>

/// region <Memory Base Address Consistency Tests>

TEST_F(SharedMemory_Test, DerivedPointersRemainConsistent)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Get initial memory layout
    uint8_t* initialRAMBase = memory->RAMBase();
    uint8_t* initialCacheBase = memory->CacheBase();
    uint8_t* initialMiscBase = memory->MiscBase();
    uint8_t* initialROMBase = memory->ROMBase();

    // Verify initial relative positions (these should always be consistent)
    ptrdiff_t initialCacheOffset = initialCacheBase - initialRAMBase;
    ptrdiff_t initialMiscOffset = initialMiscBase - initialRAMBase;
    ptrdiff_t initialROMOffset = initialROMBase - initialRAMBase;

    // Enable shared memory
    fm->setFeature(Features::kSharedMemory, true);
    memory->UpdateFeatureCache();

    // Get new memory layout
    uint8_t* newRAMBase = memory->RAMBase();
    uint8_t* newCacheBase = memory->CacheBase();
    uint8_t* newMiscBase = memory->MiscBase();
    uint8_t* newROMBase = memory->ROMBase();

    // Relative positions should be the same
    EXPECT_EQ(newCacheBase - newRAMBase, initialCacheOffset) << "Cache offset should remain consistent";
    EXPECT_EQ(newMiscBase - newRAMBase, initialMiscOffset) << "Misc offset should remain consistent";
    EXPECT_EQ(newROMBase - newRAMBase, initialROMOffset) << "ROM offset should remain consistent";
}

/// endregion </Memory Base Address Consistency Tests>

/// region <Cleanup Tests>

TEST_F(SharedMemory_Test, SharedMemoryCleanedUpOnEmulatorDestroy)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Enable shared memory
    fm->setFeature(Features::kSharedMemory, true);
    memory->UpdateFeatureCache();

    // Get the shared memory name for later verification
    std::string shmName = memory->GetMappedMemoryFilepath();
    ASSERT_FALSE(shmName.empty());

    // Destroy the emulator (TearDown will do this, but we do it explicitly here)
    _emulator->Stop();
    _emulator->Release();
    delete _emulator;
    _emulator = nullptr;

    // Small delay to ensure cleanup completes
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Verify the shared memory region no longer exists
#ifndef _WIN32
    // On POSIX systems, try to open the shared memory - should fail
    int fd = shm_open(shmName.c_str(), O_RDONLY, 0);
    if (fd >= 0)
    {
        close(fd);
        FAIL() << "Shared memory should be unlinked after emulator destruction: " << shmName;
    }
    // Expected: shm_open fails because the shared memory was unlinked
#endif
}

/// endregion </Cleanup Tests>

/// region <External Access Tests - Cross Platform>

/// @brief Cross-platform helper to open shared memory from "outside" (read-only)
/// @param shmName The shared memory name (platform-specific format)
/// @param size Expected size of the shared memory region
/// @param outData Pointer to receive the mapped memory address
/// @param outHandle Platform-specific handle for cleanup (Windows only)
/// @return true if successful, false otherwise
static bool OpenSharedMemoryExternal(const std::string& shmName, size_t size, void** outData,
                                     void** outHandle = nullptr)
{
    if (outData == nullptr)
        return false;

#ifdef _WIN32
    // Windows: Use OpenFileMapping + MapViewOfFile (read-only)
    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, shmName.c_str());
    if (hMapFile == NULL)
    {
        return false;
    }

    *outData = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, size);
    if (*outData == NULL)
    {
        CloseHandle(hMapFile);
        return false;
    }

    if (outHandle)
        *outHandle = hMapFile;
    return true;

#else
    // POSIX (macOS/Linux): Use shm_open + mmap (read-only)
    int fd = shm_open(shmName.c_str(), O_RDONLY, 0);
    if (fd < 0)
    {
        return false;
    }

    *outData = mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);  // fd can be closed after mmap

    if (*outData == MAP_FAILED)
    {
        *outData = nullptr;
        return false;
    }

    return true;
#endif
}

/// @brief Cross-platform helper to close externally opened shared memory
/// @param data The mapped memory address
/// @param size Size of the mapped region
/// @param handle Platform-specific handle (Windows only)
static void CloseSharedMemoryExternal(void* data, size_t size, void* handle = nullptr)
{
    if (data == nullptr)
        return;

#ifdef _WIN32
    UnmapViewOfFile(data);
    if (handle)
        CloseHandle(static_cast<HANDLE>(handle));
#else
    munmap(data, size);
#endif
}

TEST_F(SharedMemory_Test, ExternalProcessCanOpenSharedMemory)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Enable shared memory
    fm->setFeature(Features::kSharedMemory, true);
    memory->UpdateFeatureCache();
    ASSERT_TRUE(IsSharedMemoryActive());

    // Get the shared memory name
    std::string shmName = memory->GetMappedMemoryFilepath();
    ASSERT_FALSE(shmName.empty()) << "Shared memory name should not be empty";

    // Calculate expected size
    const size_t expectedSize = (MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES) * PAGE_SIZE;

    // Try to open the shared memory externally (simulating external process, read-only)
    void* externalData = nullptr;
    void* handle = nullptr;
    bool opened = OpenSharedMemoryExternal(shmName, expectedSize, &externalData, &handle);

    EXPECT_TRUE(opened) << "External process should be able to open shared memory: " << shmName;
    EXPECT_NE(externalData, nullptr) << "Mapped memory should not be null";

    // Clean up
    if (externalData != nullptr)
    {
        CloseSharedMemoryExternal(externalData, expectedSize, handle);
    }
}

TEST_F(SharedMemory_Test, ExternalProcessCanReadWrittenData)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Enable shared memory
    fm->setFeature(Features::kSharedMemory, true);
    memory->UpdateFeatureCache();
    ASSERT_TRUE(IsSharedMemoryActive());

    // Write a distinctive test pattern to emulator memory
    const size_t testSize = 256;
    const uint8_t testSeed = 0xDE;
    WriteTestPattern(memory->RAMBase(), testSize, testSeed);

    // Get the shared memory name
    std::string shmName = memory->GetMappedMemoryFilepath();
    const size_t expectedSize = (MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES) * PAGE_SIZE;

    // Open the shared memory externally (read-only)
    void* externalData = nullptr;
    void* handle = nullptr;
    bool opened = OpenSharedMemoryExternal(shmName, expectedSize, &externalData, &handle);
    ASSERT_TRUE(opened) << "Failed to open shared memory externally";
    ASSERT_NE(externalData, nullptr);

    // Verify that the external view sees the same data (read-only verification)
    const uint8_t* externalBytes = static_cast<const uint8_t*>(externalData);
    bool dataMatches = VerifyTestPattern(externalBytes, testSize, testSeed);

    EXPECT_TRUE(dataMatches) << "External process should see the same data written by emulator";

    // Clean up
    CloseSharedMemoryExternal(externalData, expectedSize, handle);
}

TEST_F(SharedMemory_Test, ExternalProcessSeesLiveUpdates)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Enable shared memory
    fm->setFeature(Features::kSharedMemory, true);
    memory->UpdateFeatureCache();
    ASSERT_TRUE(IsSharedMemoryActive());

    // Get the shared memory name and open it externally (read-only)
    std::string shmName = memory->GetMappedMemoryFilepath();
    const size_t expectedSize = (MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES) * PAGE_SIZE;

    void* externalData = nullptr;
    void* handle = nullptr;
    bool opened = OpenSharedMemoryExternal(shmName, expectedSize, &externalData, &handle);
    ASSERT_TRUE(opened) << "Failed to open shared memory externally";
    ASSERT_NE(externalData, nullptr);

    const uint8_t* externalBytes = static_cast<const uint8_t*>(externalData);

    // Write pattern 1 and verify external sees it
    const size_t testSize = 128;
    const uint8_t seed1 = 0x11;
    WriteTestPattern(memory->RAMBase(), testSize, seed1);
    EXPECT_TRUE(VerifyTestPattern(externalBytes, testSize, seed1)) << "External should see pattern 1";

    // Write pattern 2 (different) and verify external sees the update
    const uint8_t seed2 = 0x22;
    WriteTestPattern(memory->RAMBase(), testSize, seed2);
    EXPECT_TRUE(VerifyTestPattern(externalBytes, testSize, seed2)) << "External should see pattern 2 (live update)";

    // Write pattern 3 and verify
    const uint8_t seed3 = 0x33;
    WriteTestPattern(memory->RAMBase(), testSize, seed3);
    EXPECT_TRUE(VerifyTestPattern(externalBytes, testSize, seed3)) << "External should see pattern 3 (live update)";

    // Clean up
    CloseSharedMemoryExternal(externalData, expectedSize, handle);
}

TEST_F(SharedMemory_Test, ExternalCannotOpenWhenFeatureDisabled)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // Enable shared memory first to get the name
    fm->setFeature(Features::kSharedMemory, true);
    memory->UpdateFeatureCache();
    ASSERT_TRUE(IsSharedMemoryActive());

    std::string shmName = memory->GetMappedMemoryFilepath();
    const size_t expectedSize = (MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES + MAX_ROM_PAGES) * PAGE_SIZE;

    // Disable shared memory
    fm->setFeature(Features::kSharedMemory, false);
    memory->UpdateFeatureCache();
    ASSERT_FALSE(IsSharedMemoryActive());

    // Small delay to ensure cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Try to open the shared memory externally - should fail
    void* externalData = nullptr;
    bool opened = OpenSharedMemoryExternal(shmName, expectedSize, &externalData);

    EXPECT_FALSE(opened) << "External process should NOT be able to open shared memory after feature is disabled";

    // Clean up just in case
    if (externalData != nullptr)
    {
        CloseSharedMemoryExternal(externalData, expectedSize);
    }
}

TEST_F(SharedMemory_Test, ExternalProcessRigorousValidation)
{
    FeatureManager* fm = GetFeatureManager();
    ASSERT_NE(fm, nullptr);

    Memory* memory = GetMemory();
    ASSERT_NE(memory, nullptr);

    // 1. Initial State: Ensure emulator is running and then pause it
    _emulator->StartAsync();

    // Wait for the emulator thread to actually reach the Run state
    // (Otherwise Pause() will be immediately overriden by Start())
    for (int i = 0; i < 50 && _emulator->GetState() != StateRun; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(_emulator->GetState(), StateRun) << "Emulator failed to reach StateRun";

    _emulator->Pause();
    // Allow state to transition to Paused
    for (int i = 0; i < 20 && _emulator->GetState() != StatePaused; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(_emulator->GetState(), StatePaused) << "Emulator should be paused for rigorous memory testing";

    // 2. Enable Shared Memory feature
    fm->setFeature(Features::kSharedMemory, true);
    memory->UpdateFeatureCache();
    ASSERT_TRUE(IsSharedMemoryActive()) << "Shared memory must be active";

    // 3. Get shared memory details
    std::string shmName = memory->GetMappedMemoryFilepath();
    const size_t totalSize = MAX_PAGES * PAGE_SIZE;

    // 4. Open from "outside" (read-only)
    void* externalData = nullptr;
    void* handle = nullptr;
    bool opened = OpenSharedMemoryExternal(shmName, totalSize, &externalData, &handle);
    ASSERT_TRUE(opened) << "External process failed to open shared memory: " << shmName;
    ASSERT_NE(externalData, nullptr);
    const uint8_t* externalBytes = static_cast<const uint8_t*>(externalData);

    // 5. Rigorous Pattern Cycles over the ENTIRE mapped region
    // We use a variety of patterns to catch stuck bits, synchronization glitches, or offset issues.
    const uint8_t patterns[] = {0x00, 0xFF, 0x55, 0xAA, 0x33, 0xCC};

    for (uint8_t pattern : patterns)
    {
        // Fill from inside the emulator
        memset(memory->RAMBase(), pattern, totalSize);

        // Verify from outside
        // We scan every single byte to ensure no "holes" or size mismatches in mapping
        for (size_t i = 0; i < totalSize; i++)
        {
            if (externalBytes[i] != pattern)
            {
                // Cleanup before failing
                CloseSharedMemoryExternal(externalData, totalSize, handle);
                _emulator->Resume();

                FAIL() << "Rigorous validation failed at offset " << i << ": expected 0x" << std::hex << (int)pattern
                       << ", got 0x" << (int)externalBytes[i] << " during full-memory scan";
            }
        }
    }

    // 6. Complex Seeded Pattern Cycles
    // This helps catch linear addressing errors (e.g. A[i] = i+seed)
    const uint8_t seeds[] = {0x13, 0x42, 0x89};
    for (uint8_t seed : seeds)
    {
        WriteTestPattern(memory->RAMBase(), totalSize, seed);

        if (!VerifyTestPattern(externalBytes, totalSize, seed))
        {
            // Scan for the first mismatch to provide better error message
            for (size_t i = 0; i < totalSize; i++)
            {
                uint8_t expected = static_cast<uint8_t>((seed + i) & 0xFF);
                if (externalBytes[i] != expected)
                {
                    CloseSharedMemoryExternal(externalData, totalSize, handle);
                    _emulator->Resume();

                    FAIL() << "Rigorous seeded validation failed at offset " << i << " with seed 0x" << std::hex
                           << (int)seed << ": expected 0x" << (int)expected << ", got 0x" << (int)externalBytes[i];
                }
            }
        }
    }

    // 7. Cleanup
    CloseSharedMemoryExternal(externalData, totalSize, handle);

    // 8. Resume emulator and verify it's no longer paused
    _emulator->Resume();
    EXPECT_FALSE(_emulator->IsPaused());
}

/// endregion </External Access Tests - Cross Platform>
