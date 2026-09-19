#include <gtest/gtest.h>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskfastload.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/memory/memory.h"
#include "emulator/spectrumconstants.h"

class DiskFastLoad_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    DiskFastLoad* _fastLoad = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _z80 = _context->pCore->GetZ80();
        _fastLoad = _context->pDiskFastLoad;
        ASSERT_NE(_fastLoad, nullptr);

        InjectTRDOSROMSignature();
    }

    void TearDown() override
    {
        if (_emulator != nullptr)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
        _context = nullptr;
        _z80 = nullptr;
        _fastLoad = nullptr;
    }

    void InjectTRDOSROMSignature()
    {
        if (_context && _context->pMemory && _context->pMemory->base_dos_rom)
        {
            uint8_t* rom = _context->pMemory->base_dos_rom;
            // $3FEC opcode INI: ED A2
            rom[0x3FEC] = 0xED;
            rom[0x3FED] = 0xA2;
        }
    }

    void EnableTRDOSMode()
    {
        _context->emulatorState.flags |= CF_TRDOS;
        _context->pMemory->UpdateZ80Banks();
    }

    void DisableTRDOSMode()
    {
        _context->emulatorState.flags &= ~CF_TRDOS;
        _context->pMemory->UpdateZ80Banks();
    }
};

TEST_F(DiskFastLoad_Test, IsArmed_WhenConditionsMet)
{
    EnableTRDOSMode();
    EXPECT_TRUE(_fastLoad->IsArmed());
}

TEST_F(DiskFastLoad_Test, IsArmed_ReturnsFalseWhenNotTRDOS)
{
    DisableTRDOSMode();
    EXPECT_FALSE(_fastLoad->IsArmed());
}

TEST_F(DiskFastLoad_Test, IsArmed_ReturnsFalseWhenFeatureDisabled)
{
    EnableTRDOSMode();
    ASSERT_TRUE(_fastLoad->IsArmed());

    FeatureManager* fm = _context->pFeatureManager;
    ASSERT_NE(fm, nullptr);
    ASSERT_TRUE(fm->setFeature(Features::kFastDisk, false));
    EXPECT_FALSE(_fastLoad->IsArmed());

    // Re-enable
    ASSERT_TRUE(fm->setFeature(Features::kFastDisk, true));
    EXPECT_TRUE(_fastLoad->IsArmed());
}

TEST_F(DiskFastLoad_Test, IsArmed_DisabledDuringTTDRecordingAndRestoredOnStop)
{
    EnableTRDOSMode();
    FeatureManager* fm = _context->pFeatureManager;
    ttd::TimeTravelManager* ttd = _context->pTimeTravelManager;
    ASSERT_NE(fm, nullptr);
    ASSERT_NE(ttd, nullptr);

    // Ensure shortcuts are enabled initially
    fm->setFeature(Features::kFastDisk, true);
    fm->setFeature(Features::kFastTape, true);
    fm->setFeature(Features::kTurboTape, true);

    EXPECT_TRUE(fm->isEnabled(Features::kFastDisk));
    EXPECT_TRUE(fm->isEnabled(Features::kFastTape));
    EXPECT_TRUE(fm->isEnabled(Features::kTurboTape));
    EXPECT_TRUE(_fastLoad->IsArmed());

    // Start TTD recording
    ASSERT_TRUE(ttd->StartRecording());

    // Shortcuts must be reported disabled
    EXPECT_FALSE(fm->isEnabled(Features::kFastDisk));
    EXPECT_FALSE(fm->isEnabled(Features::kFastTape));
    EXPECT_FALSE(fm->isEnabled(Features::kTurboTape));
    EXPECT_FALSE(_fastLoad->IsArmed());

    // API changes to shortcuts during recording must be rejected
    EXPECT_FALSE(fm->setFeature(Features::kFastDisk, true));
    EXPECT_FALSE(fm->setFeature(Features::kFastTape, true));
    EXPECT_FALSE(fm->setFeature(Features::kTurboTape, true));

    // Stop TTD recording
    ttd->StopRecording();

    // Original states restored
    EXPECT_TRUE(fm->isEnabled(Features::kFastDisk));
    EXPECT_TRUE(fm->isEnabled(Features::kFastTape));
    EXPECT_TRUE(fm->isEnabled(Features::kTurboTape));
    EXPECT_TRUE(_fastLoad->IsArmed());
}



TEST_F(DiskFastLoad_Test, DrainTrap_DeclinesWhenNoReadInProgress)
{
    EnableTRDOSMode();
    ASSERT_TRUE(_fastLoad->IsArmed());
    EXPECT_FALSE(_fastLoad->HandleSectorDrainTrap(*_z80));
}

TEST_F(DiskFastLoad_Test, DrainTrap_DeclinesWhenIniOpcodeMissing)
{
    EnableTRDOSMode();
    _context->pMemory->base_dos_rom[0x3FEC] = 0x00;
    EXPECT_FALSE(_fastLoad->HandleSectorDrainTrap(*_z80));
}

/// Runs the real TR-DOS read loop ($3FE5..) for one Read Sector command and returns elapsed T-states.
/// Verifies the loaded bytes against the disk image.
class DiskFastLoad_ReadLoop_Test : public DiskFastLoad_Test
{
protected:
    uint64_t RunReadLoop(bool fast, uint8_t track, uint8_t sector, uint8_t* out)
    {
        _context->pFeatureManager->setFeature(Features::kFastDisk, fast);
        EnableTRDOSMode();

        WD1793* fdc = _context->pBetaDisk;
        fdc->portDeviceOutMethod(0xFF, 0x04);  // Reset off, drive A, side 0, MFM
        fdc->portDeviceOutMethod(0x1F, 0x08);  // Restore
        RunUntilNotBusy(fdc);
        fdc->portDeviceOutMethod(0x7F, track);
        fdc->portDeviceOutMethod(0x1F, 0x10);  // Seek to track in data register
        RunUntilNotBusy(fdc);
        fdc->portDeviceOutMethod(0x5F, sector);
        fdc->portDeviceOutMethod(0x1F, 0x80);  // Read sector

        _z80->pc = 0x3FE5;
        _z80->hl = 0x8000;
        _z80->b = 0;
        _z80->c = 0x7F;
        _z80->sp = 0xBFFE;
        _context->pMemory->MapZ80AddressToPhysicalAddress(0xBFFE)[0] = 0x00;
        _context->pMemory->MapZ80AddressToPhysicalAddress(0xBFFE)[1] = 0xC0;  // return to $C000

        const uint64_t start = _context->emulatorState.t_states + _z80->t;
        for (int i = 0; i < 2'000'000 && _z80->pc != 0xC000; i++)
        {
            _z80->Z80Step();
            _context->pBetaDisk->process();
            AdvanceFrameIfNeeded();
            if (fast && i < 30) printf("DBG i=%d pc=%04X hl=%04X b=%02X sp=%04X ff=%02X\n", i, _z80->pc, _z80->hl, _z80->b, _z80->sp, fdc->portDeviceInMethod(0xFF));
        }
        const uint64_t elapsed = _context->emulatorState.t_states + _z80->t - start;

        for (int i = 0; i < 256; i++)
            out[i] = _context->pMemory->MapZ80AddressToPhysicalAddress(0x8000 + i)[0];
        return elapsed;
    }

    void RunUntilNotBusy(WD1793* fdc)
    {
        // Advance emulated time without executing code
        for (int i = 0; i < 2'000'000 && (fdc->portDeviceInMethod(0x1F) & 0x01); i++)
        {
            _z80->t += 16;
            fdc->process();
            AdvanceFrameIfNeeded();
        }
    }

    void AdvanceFrameIfNeeded()
    {
        // Keep FDC time monotonic when the frame counter wraps
        if (_z80->t >= _context->config.frame)
        {
            _context->emulatorState.t_states += _z80->t;
            _z80->t = 0;
        }
    }
};

TEST_F(DiskFastLoad_ReadLoop_Test, TrapLoadsSameBytesMuchFaster)
{
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/EyeAche.trd")));

    uint8_t slowData[256] = {};
    uint8_t fastData[256] = {};
    const uint64_t slow = RunReadLoop(false, 1, 3, slowData);
    const uint64_t fast = RunReadLoop(true, 1, 3, fastData);

    EXPECT_EQ(0, memcmp(slowData, fastData, 256)) << "Trapped read must match authentic read";
    EXPECT_LT(fast, slow / 4) << "slow=" << slow << " fast=" << fast;
    EXPECT_EQ(_context->pBetaDisk->portDeviceInMethod(0x1F) & 0x9F, 0) << "FDC must finish with clean status";
}
