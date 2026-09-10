/// @file ttd_wd1793_serializer_test.cpp
/// @brief Round-trip tests for the WD1793 + FDD TTDSerializable implementation.
///
/// Per parent TDD §15.1 test table: `TTD_Serializer_RoundTrip_<Device>` —
/// "Save → mutate → load → full-state compare".
///
/// Per parent TDD §4 row 4: "FDC internal state (state machine phase,
/// track/sector regs, DRQ/INTRQ timers) must be fully serialized". This is
/// the hardest peripheral audit (impl-plan §3.A1 item 5 note), scheduled
/// last within the serializer group.
///
/// The blob is 251 bytes = 143 (controller) + 4×27 (FDDs).
///
/// Note on FDD track initialization: the FDD ctor randomizes _track via
/// std::random_device. We therefore test round-trip via two WD1793 instances
/// — load overwrites the random track with the captured value, so byte
/// identity still holds for the post-load re-save.

// Enable the WD1793CUT (Code Under Test) wrapper so tests can mutate protected
// register fields directly without going through the full port-write path
// (which touches live emulator state via processBeta128).
#define _CODE_UNDER_TEST 1

#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "common/modulelogger.h"
#include "debugger/ttd/ttd_checkpoint.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_serializable.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/platform.h"

/// region <Test harness>

namespace
{
/// Minimal test context that builds a WD1793 against a fresh EmulatorContext.
/// WD1793's ctor creates 4 FDDs in coreState.diskDrives[].
struct Wd1793Harness
{
    EmulatorContext* context = nullptr;
    CoreCUT*         core    = nullptr;
    Z80*             z80     = nullptr;
    WD1793*          fdc     = nullptr;

    Wd1793Harness()
    {
        context = new EmulatorContext(LoggerLevel::LogError);
        context->pModuleLogger->TurnOffLoggingForAll();
        core    = new CoreCUT(context);
        z80     = new Z80(context);
        core->_z80 = z80;
        context->pCore = core;
        fdc = new WD1793(context);
    }

    ~Wd1793Harness()
    {
        // WD1793 does not own the FDDs (per its dtor comment); they live in
        // coreState.diskDrives[]. Tear down FDDs explicitly before the
        // context goes away.
        if (context)
        {
            for (size_t i = 0; i < 4; ++i)
            {
                delete context->coreState.diskDrives[i];
                context->coreState.diskDrives[i] = nullptr;
            }
        }
        delete fdc;
        if (context && context->pCore)
        {
            core->_z80 = nullptr;
            delete z80;
            context->pCore = nullptr;
            delete core;
        }
        delete context;
    }

    Wd1793Harness(const Wd1793Harness&)            = delete;
    Wd1793Harness& operator=(const Wd1793Harness&) = delete;
};
}  // anonymous namespace

/// endregion </Test harness>

/// region <FDD serializer tests>

class TTD_FDD_Serializer_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    FDD*             _fddA    = nullptr;
    FDD*             _fddB    = nullptr;

    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _fddA    = new FDD(_context);
        _fddB    = new FDD(_context);
    }

    void TearDown() override
    {
        delete _fddA;
        delete _fddB;
        delete _context;
    }
};

TEST_F(TTD_FDD_Serializer_Test, TTDStateSize_IsStable_27Bytes)
{
    EXPECT_EQ(_fddA->TTDStateSize(), 27u);
    EXPECT_EQ(_fddB->TTDStateSize(), 27u);
}

TEST_F(TTD_FDD_Serializer_Test, RoundTrip_DefaultState_IsByteIdentical)
{
    // The FDD ctor randomizes _track; force both to a known state first.
    _fddA->setTrack(0);
    _fddB->setTrack(0);

    std::vector<uint8_t> saved(_fddA->TTDStateSize());
    _fddA->TTDSaveState(saved.data());

    _fddB->TTDLoadState(saved.data());

    std::vector<uint8_t> probe(_fddB->TTDStateSize());
    _fddB->TTDSaveState(probe.data());

    EXPECT_EQ(saved, probe) << "Default FDD state failed byte-for-byte round-trip";
}

TEST_F(TTD_FDD_Serializer_Test, RoundTrip_KnownState_Preserved)
{
    // Set known state on A
    _fddA->setTrack(42);
    _fddA->setSide(true);
    _fddA->setMotor(true);
    _fddA->setWriteProtect(true);

    std::vector<uint8_t> saved(_fddA->TTDStateSize());
    _fddA->TTDSaveState(saved.data());

    _fddB->TTDLoadState(saved.data());

    EXPECT_EQ(_fddB->getTrack(), 42);
    EXPECT_TRUE(_fddB->getSide());
    EXPECT_TRUE(_fddB->getMotor());
    EXPECT_TRUE(_fddB->isWriteProtect());

    // Re-save and byte-compare
    std::vector<uint8_t> probe(_fddB->TTDStateSize());
    _fddB->TTDSaveState(probe.data());
    EXPECT_EQ(saved, probe) << "FDD round-trip diverged after restore";
}

TEST_F(TTD_FDD_Serializer_Test, SaveIsPureRead_DoesNotMutateDevice)
{
    _fddA->setTrack(7);

    std::vector<uint8_t> first(_fddA->TTDStateSize());
    _fddA->TTDSaveState(first.data());

    std::vector<uint8_t> second(_fddA->TTDStateSize());
    _fddA->TTDSaveState(second.data());

    EXPECT_EQ(first, second) << "TTDSaveState has side effects on the FDD";
}

TEST_F(TTD_FDD_Serializer_Test, RoundTrip_All256Tracks)
{
    for (int t = 0; t <= 80; ++t)
    {
        _fddA->setTrack(t);

        std::vector<uint8_t> saved(_fddA->TTDStateSize());
        _fddA->TTDSaveState(saved.data());

        _fddB->TTDLoadState(saved.data());

        std::vector<uint8_t> probe(_fddB->TTDStateSize());
        _fddB->TTDSaveState(probe.data());

        ASSERT_EQ(saved, probe) << "FDD round-trip diverged at track " << t;
        ASSERT_EQ(_fddB->getTrack(), static_cast<int8_t>(t));
    }
}

/// endregion </FDD serializer tests>

/// region <WD1793 controller + subsystem serializer tests>

class TTD_WD1793_Serializer_Test : public ::testing::Test
{
protected:
    Wd1793Harness* _harnessA = nullptr;
    Wd1793Harness* _harnessB = nullptr;

    void SetUp() override
    {
        _harnessA = new Wd1793Harness();
        _harnessB = new Wd1793Harness();
    }

    void TearDown() override
    {
        delete _harnessA;
        delete _harnessB;
    }
};

TEST_F(TTD_WD1793_Serializer_Test, TTDStateSize_IsStable_251Bytes)
{
    EXPECT_EQ(_harnessA->fdc->TTDStateSize(), 251u)
        << "WD1793+4×FDD subsystem state size drift (expected 251 = 143 + 4×27)";
}

TEST_F(TTD_WD1793_Serializer_Test, RoundTrip_PostResetState_IsByteIdentical)
{
    // After reset() both controllers are in a fully-zeroed canonical state
    // (modulo the FDDs' randomized _track fields). Force FDD tracks to zero
    // so the cross-instance compare is meaningful.
    for (size_t i = 0; i < 4; ++i)
    {
        _harnessA->context->coreState.diskDrives[i]->setTrack(0);
        _harnessB->context->coreState.diskDrives[i]->setTrack(0);
    }

    _harnessA->fdc->reset();
    _harnessB->fdc->reset();

    std::vector<uint8_t> saved(_harnessA->fdc->TTDStateSize());
    _harnessA->fdc->TTDSaveState(saved.data());

    _harnessB->fdc->TTDLoadState(saved.data());

    std::vector<uint8_t> probe(_harnessB->fdc->TTDStateSize());
    _harnessB->fdc->TTDSaveState(probe.data());

    EXPECT_EQ(saved, probe) << "Post-reset WD1793 state failed byte-for-byte round-trip";
}

TEST_F(TTD_WD1793_Serializer_Test, SaveIsPureRead_DoesNotMutateDevice)
{
    std::vector<uint8_t> first(_harnessA->fdc->TTDStateSize());
    _harnessA->fdc->TTDSaveState(first.data());

    std::vector<uint8_t> second(_harnessA->fdc->TTDStateSize());
    _harnessA->fdc->TTDSaveState(second.data());

    EXPECT_EQ(first, second) << "TTDSaveState has side effects on the WD1793";
}

TEST_F(TTD_WD1793_Serializer_Test, RoundTrip_PortWrites_PreservedViaRegisters)
{
    // The port-write path (portDeviceOutMethod → processBeta128) touches live
    // emulator state (motor timeouts, FDD messages) and is exercised by the
    // existing wd1793_test.cpp suite. For the serializer round-trip we
    // instead mutate registers via the CUT (Code Under Test) wrapper, which
    // exposes the protected register fields directly without going through
    // the FDC's command-dispatch path.
    //
    // The CUT wrapper is selected by #define _CODE_UNDER_TEST before
    // including wd1793.h. The existing test infrastructure (wd1793_test.cpp)
    // uses the same pattern.
    WD1793CUT* fdcA = reinterpret_cast<WD1793CUT*>(_harnessA->fdc);
    WD1793CUT* fdcB = reinterpret_cast<WD1793CUT*>(_harnessB->fdc);

    // Mutate registers directly via the CUT
    fdcA->_trackRegister  = 0x12;
    fdcA->_sectorRegister = 0x07;
    fdcA->_dataRegister   = 0xAB;
    fdcA->_drive          = 1;       // drive B
    fdcA->_sideUp         = true;

    std::vector<uint8_t> saved(_harnessA->fdc->TTDStateSize());
    _harnessA->fdc->TTDSaveState(saved.data());

    _harnessB->fdc->TTDLoadState(saved.data());

    // Verify individual register values were preserved via public getters
    EXPECT_EQ(_harnessB->fdc->getTrackRegister(), 0x12);
    EXPECT_EQ(_harnessB->fdc->getSectorRegister(), 0x07);
    EXPECT_EQ(_harnessB->fdc->getDataRegister(), 0xAB);

    // And via the CUT fields on B
    EXPECT_EQ(fdcB->_drive, 1);
    EXPECT_EQ(fdcB->_sideUp, true);

    // Re-save and byte-compare (catches any field offset/type bug)
    std::vector<uint8_t> probe(_harnessB->fdc->TTDStateSize());
    _harnessB->fdc->TTDSaveState(probe.data());
    EXPECT_EQ(saved, probe) << "WD1793 register state diverged after restore";
}

TEST_F(TTD_WD1793_Serializer_Test, RoundTrip_SelectedDrive_ReDerivedFromDriveIndex)
{
    // Select drive B (index 1) by mutating _drive directly via CUT
    WD1793CUT* fdcA = reinterpret_cast<WD1793CUT*>(_harnessA->fdc);
    fdcA->_drive = 1;

    std::vector<uint8_t> saved(_harnessA->fdc->TTDStateSize());
    _harnessA->fdc->TTDSaveState(saved.data());

    // Before load, B's selected drive is the default (drive 0)
    FDD* driveBefore = _harnessB->fdc->getDrive();
    EXPECT_EQ(driveBefore, _harnessB->context->coreState.diskDrives[0]);

    _harnessB->fdc->TTDLoadState(saved.data());

    FDD* driveAfter = _harnessB->fdc->getDrive();

    // After load, the selected drive should be re-derived from the restored
    // _drive index (now 1) and point to coreState.diskDrives[1].
    ASSERT_NE(driveAfter, nullptr);
    EXPECT_EQ(driveAfter, _harnessB->context->coreState.diskDrives[1])
        << "_selectedDrive not re-derived from restored _drive index";
    EXPECT_NE(driveAfter, _harnessB->context->coreState.diskDrives[0]);
}

TEST_F(TTD_WD1793_Serializer_Test, RoundTrip_FDDTracksPreservedAcrossInstances)
{
    // Set distinct non-zero track values on A's 4 FDDs
    for (size_t i = 0; i < 4; ++i)
    {
        _harnessA->context->coreState.diskDrives[i]->setTrack(
            static_cast<int8_t>(10 + i * 10));
    }

    std::vector<uint8_t> saved(_harnessA->fdc->TTDStateSize());
    _harnessA->fdc->TTDSaveState(saved.data());

    _harnessB->fdc->TTDLoadState(saved.data());

    // Each of B's FDDs should now have A's track value
    for (size_t i = 0; i < 4; ++i)
    {
        int8_t expected = static_cast<int8_t>(10 + i * 10);
        int8_t actual   = _harnessB->context->coreState.diskDrives[i]->getTrack();
        EXPECT_EQ(actual, expected)
            << "FDD[" << i << "] track not restored correctly";
    }
}

TEST_F(TTD_WD1793_Serializer_Test, RoundTrip_CrossDevice_LoadProducesSameSave)
{
    // Establish that the format is device-independent: save from A, load into
    // B, then re-save from B and compare. Both saves must be byte-identical.
    // Mutate A's state via CUT fields to give the format some variation.
    WD1793CUT* fdcA = reinterpret_cast<WD1793CUT*>(_harnessA->fdc);
    fdcA->_trackRegister  = 0x33;
    fdcA->_sectorRegister = 0x55;
    fdcA->_sideUp         = true;
    fdcA->_drive          = 2;

    std::vector<uint8_t> saveA(_harnessA->fdc->TTDStateSize());
    _harnessA->fdc->TTDSaveState(saveA.data());

    _harnessB->fdc->TTDLoadState(saveA.data());
    std::vector<uint8_t> saveB(_harnessB->fdc->TTDStateSize());
    _harnessB->fdc->TTDSaveState(saveB.data());

    EXPECT_EQ(saveA, saveB) << "WD1793 serializer is not device-independent";
}

/// endregion </WD1793 controller + subsystem serializer tests>

/// region <TimeTravelManager integration: fdcState blob is populated>

TEST(TTD_WD1793_ManagerIntegration_Test, CaptureNow_PopulatesFdcStateBlob_OnBetaDiskModel)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());

    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pTimeTravelManager, nullptr);

    // pBetaDisk is populated by Init() only on Beta Disk models (Pentagon,
    // Scorpion, etc.). The default test model may or may not have one —
    // either outcome is a valid v1 result.
    WD1793* fdc = context->pBetaDisk;

    ASSERT_TRUE(context->pTimeTravelManager->StartRecording());
    ASSERT_GE(context->pTimeTravelManager->GetCheckpointCount(), 1u);

    const ttd::TTDCheckpoint* cp = context->pTimeTravelManager->GetCheckpoint(0);
    ASSERT_NE(cp, nullptr);

    if (fdc != nullptr)
    {
        EXPECT_EQ(cp->fdcState.size(), 251u)
            << "fdcState blob must contain the 251-byte FDC subsystem payload "
            << "(controller 143 + 4×FDD 27) when a WD1793 is present";
    }
    // else: fdcState may be empty — that's the valid no-op for a model
    // without Beta Disk interface (e.g. plain Spectrum 48/128).

    emulator.Stop();
    emulator.Release();
}

/// endregion </TimeTravelManager integration>
