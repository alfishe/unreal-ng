#include <3rdparty/message-center/eventqueue.h>
#include <3rdparty/message-center/messagecenter.h>
#include <emulator/config.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/platform.h>
#include <emulator/ports/portdecoder.h>
#include <emulator/zxpoly/zxpolygroup.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "pch.h"
#include "stdafx.h"

class EmulatorManager_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;

protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);

        // Clean up any existing emulators before each test
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }

    void TearDown() override
    {
        // Clean up after each test
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }
};

TEST_F(EmulatorManager_Test, CreateEmulator)
{
    // Test creating a basic emulator with default parameters
    auto emulator = _manager->CreateEmulator();
    ASSERT_NE(emulator, nullptr);

    // Verify the emulator has a valid ID
    UUID emulatorId = emulator->GetUUID();
    ASSERT_FALSE(emulatorId.isNil());

    // Verify the emulator can be retrieved
    auto retrieved = _manager->GetEmulator(emulatorId);
    ASSERT_NE(retrieved, nullptr);
    ASSERT_EQ(retrieved->GetUUID(), emulatorId);
}

TEST_F(EmulatorManager_Test, CreateEmulatorWithId)
{
    std::string symbolicId = "test-symbolic-id";

    // Test creating an emulator with a symbolic ID
    auto emulator = _manager->CreateEmulator(symbolicId);
    ASSERT_NE(emulator, nullptr);

    // Get the dynamically generated UUID
    UUID emulatorId = emulator->GetUUID();

    // The emulator should be retrievable using its generated ID
    auto retrieved = _manager->GetEmulator(emulatorId);
    ASSERT_NE(retrieved, nullptr);
    ASSERT_EQ(retrieved->GetUUID(), emulatorId);

    // Verify the symbolic ID was set correctly
    ASSERT_EQ(emulator->GetSymbolicId(), symbolicId);
}

/// @brief ZX-Evo (MM_ATM3) must boot the BaseConf ROM set, not TSConf.
///
/// Regression: the atm3 model config once carried a ROMSET section mapping
/// the slots to low pages of the image, which put TSConf's TS-BIOS (page 0)
/// into the sys slot - the machine booted TSConf firmware on BaseConf
/// hardware and hung in ZX screen mode with a red border. Correct behavior
/// (reference unrealspeccy config.cpp, non-ROMSET ATM branch): the whole
/// 512K image loads raw and the standard set comes from the LAST 4 pages.
/// The shipped image is the official BaseConf zxevo_fe.rom (pentevo
/// build_full.bat): sos=28 BASIC48, dos=29 NEO-DOS, 128=30, sys=31 EVO Reset
/// Service, with the FFF7-paged ERS service pages 22..26 behind them.
TEST_F(EmulatorManager_Test, CreateZXEvo_BootsBaseConfRomSet)
{
    // "ZX-Evo" is the FullName; the lookup key is the short name "ATM3"
    auto emulator = _manager->CreateEmulatorWithModelAndRAM("zxevo-rom-test", "ATM3", 4096, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(context->config.mem_model, MM_ATM3);
    EXPECT_FALSE(context->config.use_romset) << "atm3 config must not use ROMSET";
    EXPECT_EQ(context->config.atm.evo_legacy_fpga, 0) << "zxevo-fe.rom needs the current (trdemu) BaseConf";

    Memory* memory = context->pMemory;
    ASSERT_NE(memory, nullptr);

    // 512K image = 32 banks; standard set = last 4 banks (28..31)
    EXPECT_EQ(memory->base_sos_rom, memory->ROMPageHostAddress(28));
    EXPECT_EQ(memory->base_dos_rom, memory->ROMPageHostAddress(29));
    EXPECT_EQ(memory->base_128_rom, memory->ROMPageHostAddress(30));
    EXPECT_EQ(memory->base_sys_rom, memory->ROMPageHostAddress(31));

    auto pageContains = [memory](uint16_t page, const char* text) {
        const uint8_t* data = memory->ROMPageHostAddress(page);
        const size_t length = strlen(text);
        for (size_t i = 0; i + length <= PAGE_SIZE; ++i)
            if (memcmp(data + i, text, length) == 0)
                return true;
        return false;
    };

    // dos = NEO-DOS, the TR-DOS 5.03-family ROM the ERS virtual-drive code is built against
    EXPECT_TRUE(pageContains(29, "NEO-DOS")) << "dos slot must be NEO-DOS";

    // sys must NOT be the custom-ROM slot (page 0 of the same image)
    EXPECT_NE(memcmp(memory->base_sys_rom, memory->ROMPageHostAddress(0), PAGE_SIZE / 16), 0)
        << "sys slot must be the EVO Reset Service, not the custom-ROM slot";

    // Whole image loaded: the FFF7-paged ERS service pages must be present
    EXPECT_TRUE(pageContains(22, "EVO Magic Service")) << "ERS RST8 service pages must be loaded (ROMSET loads only 4 banks)";
    EXPECT_TRUE(pageContains(24, "MAGIC Service"));
}

/// @brief Tests the full lifecycle of an emulator instance: create, start, pause, resume, stop, remove.
///
/// DESIGN NOTES:
/// 1. Uses EXPECT_* instead of ASSERT_* for most checks to ensure test cleanup always runs.
///    ASSERT_* causes immediate test exit on failure, which can leave resources dangling.
///
/// 2. AVOIDS MessageCenter observers with local captures. The previous implementation used:
///    ```
///    std::mutex mtx;  // Local variable
///    auto callback = [&mtx](...) { ... };  // Captures local by reference
///    messageCenter.AddObserver(..., callback);
///    ```
///    This is DANGEROUS because:
///    - If ASSERT_* fails, the test exits but the observer remains registered
///    - The callback still holds references to destroyed local variables (mtx, cv)
///    - When the next test runs and emits state changes, the dangling callback
///      tries to lock the destroyed mutex, causing "mutex lock failed: Invalid argument"
///
/// 3. Uses simple sleep-based synchronization instead of condition variables.
///    While less precise, this approach is robust and doesn't require observer cleanup.
///
/// 4. Wraps conditional test sections in if-blocks rather than using ASSERT for early exit.
///    This ensures cleanup code (Stop, RemoveEmulator) always executes.
TEST_F(EmulatorManager_Test, EmulatorInstanceLifecycle)
{
    // Test creating an emulator with default parameters
    auto emulator = _manager->CreateEmulator("test-emulator");
    ASSERT_NE(emulator, nullptr);

    // Get the generated ID
    std::string emulatorId = emulator->GetUUID();
    ASSERT_FALSE(emulatorId.empty());

    // Verify the emulator is in the correct initial state
    EXPECT_EQ(emulator->GetState(), StateInitialized);

    // Start the emulator asynchronously using the built-in method
    emulator->StartAsync();

    // Wait for the emulator to transition to running state (1s budget: the
    // original 100ms cap could trip on a CPU-starved parallel shard runner)
    for (int i = 0; i < 500 && emulator->GetState() != StateRun; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Verify the emulator transitioned to a running state
    int currentState = emulator->GetState();
    std::cout << "Emulator state after StartAsync: " << currentState << std::endl;

    // The emulator should be in StateRun (2) after starting
    bool startedOk = (currentState == StateRun);
    EXPECT_TRUE(startedOk) << "Emulator did not enter RUN state. Current state: " << currentState;

    if (startedOk)
    {
        // Verify we can get the emulator context
        auto context = emulator->GetContext();
        EXPECT_NE(context, nullptr);

        // Pause the emulator
        emulator->Pause();

        // Wait for pause to take effect (1s budget - see StartAsync note)
        for (int i = 0; i < 500 && !emulator->IsPaused(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        EXPECT_TRUE(emulator->IsPaused()) << "Emulator should be paused";

        // Resume the emulator
        emulator->Resume();

        // Wait for resume to take effect (1s budget - see StartAsync note)
        for (int i = 0; i < 500 && emulator->IsPaused(); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        EXPECT_FALSE(emulator->IsPaused()) << "Emulator should not be paused after resume";
    }

    // Stop the emulator (synchronously joins the mainloop thread)
    emulator->Stop();

    // Verify the emulator is now stopped
    int stopState = emulator->GetState();
    std::cout << "Emulator state after stop: " << stopState << std::endl;
    EXPECT_NE(stopState, StateRun) << "Emulator did not stop";

    // Clean up by removing the emulator
    bool removed = _manager->RemoveEmulator(emulatorId);
    EXPECT_TRUE(removed);

    // Verify the emulator is no longer accessible
    EXPECT_FALSE(_manager->HasEmulator(emulatorId));
    EXPECT_EQ(_manager->GetEmulator(emulatorId), nullptr);
}

TEST_F(EmulatorManager_Test, RemoveEmulator)
{
    // Create a test emulator
    auto emulator = _manager->CreateEmulator();
    ASSERT_NE(emulator, nullptr);
    std::string emulatorId = emulator->GetUUID();

    // Verify it exists
    ASSERT_TRUE(_manager->HasEmulator(emulatorId));

    // Remove it
    bool removed = _manager->RemoveEmulator(emulatorId);
    ASSERT_TRUE(removed);

    // Verify it no longer exists
    ASSERT_FALSE(_manager->HasEmulator(emulatorId));
    ASSERT_EQ(_manager->GetEmulator(emulatorId), nullptr);
}

// A UI thread holding a context lease (Emulator::LeaseContext) keeps the
// context alive: a removal on another thread refuses new leases at once, but
// stops and frees the instance only after the lease ends (2026-10-02 crash:
// the Qt Machine menu read pKeyboard from a context a WebAPI DELETE had freed)
TEST_F(EmulatorManager_Test, RemoveEmulatorWaitsForContextLease)
{
    auto emulator = _manager->CreateEmulator();
    ASSERT_NE(emulator, nullptr);
    const std::string emulatorId = emulator->GetId();

    std::optional<Emulator::ContextLease> lease(emulator->LeaseContext());
    ASSERT_TRUE(*lease);
    EmulatorContext* context = lease->get();
    ASSERT_EQ(context, emulator->GetContext());

    std::atomic<bool> removed{false};
    std::thread remover([&] {
        _manager->RemoveEmulator(emulatorId);
        removed = true;
    });

    ASSERT_TRUE(TestWait::For([&] { return emulator->IsRetiring(); }));
    EXPECT_FALSE(emulator->LeaseContext()) << "a lease taken after the removal began must be refused";

    // The lease is still held: the removal cannot have freed anything
    EXPECT_FALSE(removed.load());
    EXPECT_FALSE(emulator->IsReleased());
    EXPECT_EQ(emulator->GetContext(), context);
    EXPECT_NE(context->pKeyboard, nullptr);

    lease.reset();
    remover.join();
    EXPECT_TRUE(removed.load());
    EXPECT_TRUE(emulator->IsReleased());
    EXPECT_EQ(emulator->GetContext(), nullptr);
    EXPECT_FALSE(emulator->LeaseContext());
    EXPECT_FALSE(_manager->HasEmulator(emulatorId));
}

TEST_F(EmulatorManager_Test, ContextLeaseRefusedOnceReleased)
{
    auto emulator = _manager->CreateEmulator();
    ASSERT_NE(emulator, nullptr);
    {
        const Emulator::ContextLease lease = emulator->LeaseContext();
        ASSERT_TRUE(lease);
        EXPECT_EQ(lease.get(), emulator->GetContext());
        // Leases are shared: a second reader does not wait
        EXPECT_TRUE(emulator->LeaseContext());
    }
    ASSERT_TRUE(_manager->RemoveEmulator(emulator->GetId()));
    EXPECT_TRUE(emulator->IsRetiring());
    EXPECT_FALSE(emulator->LeaseContext());
}

TEST_F(EmulatorManager_Test, GetEmulatorIds)
{
    // Create multiple emulators and collect their IDs
    std::vector<std::string> createdIds;
    std::vector<std::string> symbolicIds = {"test1", "test2", "test3"};

    for (const auto& symbolicId : symbolicIds)
    {
        auto emulator = _manager->CreateEmulator(symbolicId);
        ASSERT_NE(emulator, nullptr);
        createdIds.push_back(emulator->GetUUID());
    }

    // Get all emulator IDs
    auto emulatorIds = _manager->GetEmulatorIds();

    // Verify we got the correct number of IDs
    ASSERT_EQ(emulatorIds.size(), createdIds.size());

    // Verify all created IDs are present in the returned list
    for (const auto& id : createdIds)
    {
        auto it = std::find(emulatorIds.begin(), emulatorIds.end(), id);
        ASSERT_NE(it, emulatorIds.end()) << "Emulator ID " << id << " not found in emulator IDs";
    }
}

TEST_F(EmulatorManager_Test, GetAllEmulatorStatuses)
{
    // Create test emulators
    auto emulator1 = _manager->CreateEmulator("test1");
    auto emulator2 = _manager->CreateEmulator("test2");

    // Get the generated IDs
    std::string id1 = emulator1->GetUUID();
    std::string id2 = emulator2->GetUUID();

    // Get all statuses
    auto statuses = _manager->GetAllEmulatorStatuses();

    // Verify we have status for both emulators
    ASSERT_EQ(statuses.size(), 2);

    // Verify both emulators are in the status map
    ASSERT_NE(statuses.find(id1), statuses.end());
    ASSERT_NE(statuses.find(id2), statuses.end());

    // Verify default state is correct (should be StateInitialized after creation)
    ASSERT_EQ(statuses[id1], StateInitialized);
    ASSERT_EQ(statuses[id2], StateInitialized);
}

TEST_F(EmulatorManager_Test, FindEmulatorsBySymbolicId)
{
    // Create emulators with different symbolic IDs
    auto emulator1 = _manager->CreateEmulator("test1");
    auto emulator2 = _manager->CreateEmulator("test2");
    auto emulator3 = _manager->CreateEmulator("test3");

    // Get the generated IDs
    UUID id1 = emulator1->GetUUID();
    UUID id2 = emulator2->GetUUID();
    UUID id3 = emulator3->GetUUID();

    // Find emulators by symbolic ID - returns vector of emulator pointers
    auto test1Emulators = _manager->FindEmulatorsBySymbolicId("test1");
    auto test2Emulators = _manager->FindEmulatorsBySymbolicId("test2");
    auto test3Emulators = _manager->FindEmulatorsBySymbolicId("test3");
    auto nonexistentEmulators = _manager->FindEmulatorsBySymbolicId("nonexistent");

    // Verify results - each emulator has a unique symbolic ID, so we expect 1 for each
    ASSERT_EQ(test1Emulators.size(), 1);
    ASSERT_EQ(test2Emulators.size(), 1);
    ASSERT_EQ(test3Emulators.size(), 1);
    ASSERT_TRUE(nonexistentEmulators.empty());

    // Verify the correct emulators are returned for each symbolic ID
    ASSERT_EQ(test1Emulators[0]->GetUUID(), id1);
    ASSERT_EQ(test2Emulators[0]->GetUUID(), id2);
    ASSERT_EQ(test3Emulators[0]->GetUUID(), id3);
}

TEST_F(EmulatorManager_Test, GetEmulatorNonExistent)
{
    // Try to get a non-existent emulator
    auto emulator = _manager->GetEmulator("non-existent-id");
    ASSERT_EQ(emulator, nullptr);
}

TEST_F(EmulatorManager_Test, RemoveNonExistentEmulator)
{
    // Try to remove a non-existent emulator
    bool removed = _manager->RemoveEmulator("non-existent-id");
    ASSERT_FALSE(removed);
}

TEST_F(EmulatorManager_Test, CreateEmulatorWithDuplicateId)
{
    // Create first emulator
    auto emulator1 = _manager->CreateEmulator("test-emulator");
    ASSERT_NE(emulator1, nullptr);
    UUID emulator1Id = emulator1->GetUUID();

    // Try to create another emulator with the same symbolic ID
    auto emulator2 = _manager->CreateEmulator("test-emulator");

    // The second creation should succeed since we're not using CreateEmulatorWithId
    ASSERT_NE(emulator2, nullptr);

    // The first emulator should still be accessible using its ID
    auto retrieved1 = _manager->GetEmulator(emulator1Id);
    ASSERT_NE(retrieved1, nullptr);
    ASSERT_EQ(retrieved1->GetUUID(), emulator1Id);

    // The second emulator should have a different ID
    UUID emulator2Id = emulator2->GetUUID();
    ASSERT_NE(emulator1Id, emulator2Id);

    // The second emulator should be accessible using its ID
    auto retrieved2 = _manager->GetEmulator(emulator2Id);
    ASSERT_NE(retrieved2, nullptr);
    ASSERT_EQ(retrieved2->GetUUID(), emulator2Id);
}

// P0-2 (docs/inprogress/2026-09-14-automation-triage-gaps): every failure
// path of the model create APIs must hand the caller a usable reason -
// WebAPI returns it as the 400 message, CLI prints it as "Reason:".
TEST_F(EmulatorManager_Test, CreateEmulatorWithModelUnknownModelReportsReason)
{
    std::string error;
    auto emulator = _manager->CreateEmulatorWithModel("", "NO_SUCH_MACHINE", LoggerLevel::LogWarning, &error);

    EXPECT_EQ(emulator, nullptr);
    EXPECT_NE(error.find("NO_SUCH_MACHINE"), std::string::npos) << "reason was: " << error;
    EXPECT_NE(error.find("unknown model"), std::string::npos) << "reason was: " << error;
}

TEST_F(EmulatorManager_Test, CreateEmulatorWithUnsupportedRamReportsReason)
{
    // PENTAGON supports 128/512/1024 - request something else
    std::string error;
    auto emulator = _manager->CreateEmulatorWithModelAndRAM("", "PENTAGON", 256, LoggerLevel::LogWarning, &error);

    EXPECT_EQ(emulator, nullptr);
    EXPECT_NE(error.find("256"), std::string::npos) << "reason was: " << error;
    EXPECT_NE(error.find("PENTAGON"), std::string::npos) << "reason was: " << error;
    // The supported list must be part of the reason so callers can self-correct
    EXPECT_NE(error.find("128"), std::string::npos) << "reason was: " << error;
}

TEST_F(EmulatorManager_Test, CreateEmulatorWithNonCreatableModelReportsReason)
{
    // A non-creatable model (e.g. NEXT): whichever guard fires first,
    // the caller must learn WHICH model failed and why
    std::string error;
    auto emulator = _manager->CreateEmulatorWithModel("", "NEXT", LoggerLevel::LogWarning, &error);

    EXPECT_EQ(emulator, nullptr);
    EXPECT_NE(error.find("NEXT"), std::string::npos) << "reason was: " << error;
    EXPECT_FALSE(error.empty());
}

TEST_F(EmulatorManager_Test, CreateEmulatorWithModelLeavesNoOrphanOnError)
{
    // A failed create must not leave a half-built instance in the manager
    size_t countBefore = _manager->GetEmulatorIds().size();

    std::string error;
    auto emulator = _manager->CreateEmulatorWithModel("", "NO_SUCH_MACHINE", LoggerLevel::LogWarning, &error);

    EXPECT_EQ(emulator, nullptr);
    EXPECT_EQ(_manager->GetEmulatorIds().size(), countBefore);
}

TEST_F(EmulatorManager_Test, CreateEmulatorWithModelResolvesRequestedMachine)
{
    // Success path: the created instance must actually BE the requested
    // machine (guards the historical silent-48K-fallback class of bugs)
    auto emulator = _manager->CreateEmulatorWithModel("", "48K");
    ASSERT_NE(emulator, nullptr);

    EmulatorContext* context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    EXPECT_EQ(context->config.mem_model, MM_SPECTRUM48);
    EXPECT_EQ(context->config.ramsize, 48u);
}

// Parity rule: MachineIdentity is the SINGLE source the WebAPI identity
// fields, the MCP passthrough and the CLI echo are all built from. It must
// report the same resolved facts the WebAPI responses carry.
TEST_F(EmulatorManager_Test, GetMachineIdentityReportsResolvedMachine)
{
    auto emulator = _manager->CreateEmulatorWithModel("", "48K");
    ASSERT_NE(emulator, nullptr);

    MachineIdentity identity = EmulatorManager::GetMachineIdentity(*emulator);

    EXPECT_TRUE(identity.Valid);
    EXPECT_EQ(identity.Model, "48K");
    EXPECT_EQ(identity.ModelFullName, "ZX-Spectrum 48k");
    EXPECT_EQ(identity.RamKb, 48u);
    EXPECT_EQ(identity.ConfigFolder, "spectrum48");
    EXPECT_GE(identity.SpeedMultiplier, 1.0);

    // Init wires the screen subsystem, so the live video mode is reportable
    EXPECT_TRUE(identity.HasVideoMode);
    EXPECT_FALSE(identity.VideoMode.empty());
}

namespace
{
/// Index of the first non-zero byte in the machine's configured RAM, or -1
long FirstNonZeroRamByte(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    const uint8_t* ram = context->pMemory->RAMPageAddress(0);
    const size_t bytes = static_cast<size_t>(context->config.ramsize) * 1024;
    for (size_t i = 0; i < bytes; i++)
    {
        if (ram[i] != 0)
            return static_cast<long>(i);
    }
    return -1;
}
}  // namespace

// RAMPowerOn=ZERO through every create path: every RAM page of the
// configuration reads 0 (the default fills the screen pages with noise)
TEST_F(EmulatorManager_Test, ZeroRamPowerOnClearsEveryRamPageOnEveryCreatePath)
{
    const auto zero = Config::RamPowerOnOverride(RamPowerOn::Zero);
    std::vector<std::shared_ptr<Emulator>> created = {
        _manager->CreateEmulator("", LoggerLevel::LogError, zero),
        _manager->CreateEmulatorWithModel("", "PENTAGON", LoggerLevel::LogError, nullptr, zero),
        _manager->CreateEmulatorWithModelAndRAM("", "PENTAGON", 512, LoggerLevel::LogError, nullptr, zero),
    };
    for (const auto& emulator : created)
    {
        ASSERT_NE(emulator, nullptr);
        EXPECT_EQ(emulator->GetContext()->config.ramPowerOn, RamPowerOn::Zero);
        EXPECT_EQ(FirstNonZeroRamByte(*emulator), -1) << emulator->GetContext()->config.ramsize << "KB machine";
        EXPECT_EQ(EmulatorManager::GetMachineIdentity(*emulator).RamPowerOn, "zero");
    }
}

TEST_F(EmulatorManager_Test, RandomRamPowerOnFillsTheScreenPages)
{
    auto emulator = _manager->CreateEmulatorWithModel("", "PENTAGON", LoggerLevel::LogError, nullptr,
                                                      Config::RamPowerOnOverride(RamPowerOn::Random));
    ASSERT_NE(emulator, nullptr);
    const uint8_t* page5 = emulator->GetContext()->pMemory->RAMPageAddress(5);
    size_t nonZero = 0;
    for (size_t i = 0; i < PAGE_SIZE; i++)
        nonZero += page5[i] != 0;
    EXPECT_GT(nonZero, PAGE_SIZE / 2) << "the power-on noise is gone";
    EXPECT_EQ(EmulatorManager::GetMachineIdentity(*emulator).RamPowerOn, "random");
}

// A ZX-Poly configuration name goes through CreateEmulatorWithModel: the
// override reaches all four modules
TEST_F(EmulatorManager_Test, ZeroRamPowerOnReachesEveryZXPolyModule)
{
    auto master = _manager->CreateEmulatorWithModel("", "ZXPOLY-48K", LoggerLevel::LogError, nullptr,
                                                    Config::RamPowerOnOverride(RamPowerOn::Zero));
    ASSERT_NE(master, nullptr);
    ZXPolyGroup* group = _manager->GetZXPolyGroup(master->GetId());
    ASSERT_NE(group, nullptr);
    for (const std::string& id : group->GetStatus().memberIds)
    {
        auto member = _manager->GetEmulator(id);
        ASSERT_NE(member, nullptr) << id;
        EXPECT_EQ(member->GetContext()->config.ramPowerOn, RamPowerOn::Zero) << id;
        EXPECT_EQ(FirstNonZeroRamByte(*member), -1) << id;
    }
}

// P0-4: creatability helpers back the /emulator/status models_creatable list
// and the per-model "creatable" flags. The two sources of truth must agree:
// whatever IsModelCreatable reports must match what a create attempt does.
TEST_F(EmulatorManager_Test, PortDecoderIsModelSupportedMatchesCreatableExpectations)
{
    // Models with decoders (GetPortDecoderForModel returns a factory instance)
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_SPECTRUM48));
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_PENTAGON));
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_SPECTRUM128));
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_PLUS3));
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_PROFI));
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_SCORP));
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_PROFSCORP));
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_ATM710));
    EXPECT_TRUE(PortDecoder::IsModelSupported(MM_ATM3));

    // ATM710/ATM3 decoders exist and their config folders ship with the
    // build (bin/configs/atm710, bin/configs/atm3) - so both must report
    // creatable: the factory succeeds and IsModelCreatable must agree
    {
        const TMemModel* modelAtm710 = Config::FindModelByShortName("ATM710");
        ASSERT_NE(modelAtm710, nullptr);
        EXPECT_TRUE(Config::IsModelCreatable(*modelAtm710));

        const TMemModel* modelAtm3 = Config::FindModelByShortName("ATM3");
        ASSERT_NE(modelAtm3, nullptr);
        EXPECT_TRUE(Config::IsModelCreatable(*modelAtm3));
    }

    // TS-Conf (TSConf phase 1): decoder + bin/configs/ts-conf
    {
        EXPECT_TRUE(PortDecoder::IsModelSupported(MM_TSL));
        const TMemModel* model = Config::FindModelByShortName("TSL");
        ASSERT_NE(model, nullptr);
        EXPECT_TRUE(Config::IsModelCreatable(*model));
    }

    // ATM Turbo 2 v4.50 (docs/inprogress/2026-10-01-atm450)
    {
        EXPECT_TRUE(PortDecoder::IsModelSupported(MM_ATM450));
        const TMemModel* model = Config::FindModelByShortName("ATM450");
        ASSERT_NE(model, nullptr);
        EXPECT_TRUE(Config::IsModelCreatable(*model));
    }

    // Profi v3 (docs/inprogress/2026-10-01-profi-v3-v5): the v5 decoder with the v3 board profile, configs/profi3;
    // PROFI5 names the v5 board
    {
        EXPECT_TRUE(PortDecoder::IsModelSupported(MM_PROFI3));
        const TMemModel* model = Config::FindModelByShortName("PROFI3");
        ASSERT_NE(model, nullptr);
        EXPECT_EQ(model->Model, MM_PROFI3);
        EXPECT_TRUE(Config::IsModelCreatable(*model));
        EXPECT_EQ(Config::GetConfigFolderForModel(MM_PROFI3), "profi3");
        const TMemModel* v5 = Config::FindModelByShortName("PROFI5");
        ASSERT_NE(v5, nullptr);
        EXPECT_EQ(v5->Model, MM_PROFI);
        const TMemModel* profi = Config::FindModelByShortName("PROFI");
        ASSERT_NE(profi, nullptr);
        EXPECT_EQ(profi->Model, MM_PROFI) << "PROFI3 must not shadow PROFI (exact-name match)";
    }

    // Models without decoders (no factory case yet)
    for (MEM_MODEL model : {MM_GMX, MM_KAY, MM_QUORUM, MM_LSY256, MM_PHOENIX})
        EXPECT_FALSE(PortDecoder::IsModelSupported(model)) << int(model);
}

TEST_F(EmulatorManager_Test, IsModelCreatableForSupportedMachines)
{
    // 48K/PENTAGON ship with config folders in every build's bin/configs
    const TMemModel* model48k = Config::FindModelByShortName("48K");
    ASSERT_NE(model48k, nullptr);
    EXPECT_TRUE(Config::IsModelCreatable(*model48k));

    const TMemModel* modelPentagon = Config::FindModelByShortName("PENTAGON");
    ASSERT_NE(modelPentagon, nullptr);
    EXPECT_TRUE(Config::IsModelCreatable(*modelPentagon));

    const TMemModel* modelPlus3 = Config::FindModelByShortName("PLUS3");
    ASSERT_NE(modelPlus3, nullptr);
    EXPECT_TRUE(Config::IsModelCreatable(*modelPlus3));

    for (const char* shortName : { "PLUS2", "PLUS2A" })
    {
        const TMemModel* model = Config::FindModelByShortName(shortName);
        ASSERT_NE(model, nullptr) << shortName;
        EXPECT_TRUE(Config::IsModelCreatable(*model)) << shortName;
    }
}

/// "PLUS2A" starts with "PLUS2": short names match whole, in any case
TEST_F(EmulatorManager_Test, ShortNameMatchesTheWholeName)
{
    const TMemModel* plus2 = Config::FindModelByShortName("plus2");
    ASSERT_NE(plus2, nullptr);
    EXPECT_EQ(plus2->Model, MM_PLUS2);

    const TMemModel* plus2a = Config::FindModelByShortName("PLUS2A");
    ASSERT_NE(plus2a, nullptr);
    EXPECT_EQ(plus2a->Model, MM_PLUS2A);

    EXPECT_EQ(Config::FindModelByShortName("PLUS2AB"), nullptr);

    const TMemModel* plus3 = Config::FindModelByShortName("PLUS3");
    ASSERT_NE(plus3, nullptr);
    EXPECT_EQ(plus3->Model, MM_PLUS3);
}

TEST_F(EmulatorManager_Test, IsModelCreatableAgreesWithCreateAttempt)
{
    // Consistency contract: IsModelCreatable says yes <=> create succeeds.
    // Checked for one creatable and one known-not-creatable machine.
    const TMemModel* model48k = Config::FindModelByShortName("48K");
    ASSERT_NE(model48k, nullptr);
    if (Config::IsModelCreatable(*model48k))
    {
        auto emulator = _manager->CreateEmulatorWithModel("", "48K");
        EXPECT_NE(emulator, nullptr);
    }

    const TMemModel* modelAtm = Config::FindModelByShortName("ATM450");
    ASSERT_NE(modelAtm, nullptr);
    if (!Config::IsModelCreatable(*modelAtm))
    {
        std::string error;
        auto emulator = _manager->CreateEmulatorWithModel("", "ATM450", LoggerLevel::LogWarning, &error);
        EXPECT_EQ(emulator, nullptr);
        EXPECT_FALSE(error.empty());
    }
}
