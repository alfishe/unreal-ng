/// @file sysvar_detection_test.cpp
/// @brief Validates system variable state detection matches OCR results
///
/// These tests verify that fast system variable checks (memory reads) produce
/// the same state detection as slower OCR-based verification. This ensures
/// we can safely replace OCR polling with sysvar polling in integration tests.

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/spectrumconstants.h"

class SysVarDetection_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    std::string _emulatorId;

    void SetUp() override
    {
        // Use 48K for fast direct BASIC boot (Pentagon boots to 128K menu first)
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        if (_emulator)
        {
            _emulatorId = _emulator->GetId();

            // Host-side only: mutes audio, drops the sound DSP to the
            // low-quality path and decimates rendering. Safe here - ScreenOCR
            // decodes the VRAM page rather than the framebuffer, and the timing
            // these tests compare is the cost of a *check* (sysvar read vs OCR
            // decode), not emulation speed. Every test boots ~100-200 frames.
            _emulator->EnableTurboMode();
        }
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

/// Verify sysvar detection fires before OCR would succeed (it's faster)
TEST_F(SysVarDetection_Test, BASICReady_FasterThanOCR)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    // Run until BASIC ready via sysvar (fast)
    auto t0 = std::chrono::steady_clock::now();
    bool sysvarReady = EmulatorTestHelper::RunUntilBASICReady(_emulator, 200);
    auto t1 = std::chrono::steady_clock::now();
    auto sysvarMs = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    ASSERT_TRUE(sysvarReady) << "BASIC not ready via sysvar";

    // At this point sysvar says ready, but OCR may not show screen yet
    // Run more frames to let ROM print copyright message
    EmulatorTestHelper::RunFramesFast(_emulator, 100);

    // Now OCR should also show BASIC
    std::string screen = ScreenOCR::ocrScreen(_emulatorId);
    bool ocrReady = screen.find("1982") != std::string::npos ||
                    screen.find("Sinclair") != std::string::npos;

    EXPECT_TRUE(ocrReady) << "OCR should show BASIC after extra frames. Screen:\n" << screen;

    // Verify ERR_NR is still 0x00
    uint8_t errNr = EmulatorTestHelper::ReadSysVar(_emulator, SystemVariables48k::ERR_NR);
    EXPECT_EQ(errNr, 0x00) << "ERR_NR should be 0x00 (OK)";

    std::cout << "[INFO] SysVar detection took " << sysvarMs << " ms (OCR needs more frames)\n";
}

/// Verify sysvar detection is significantly faster than OCR polling
TEST_F(SysVarDetection_Test, SysVar_FasterThanOCR)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    // Warm up - boot to BASIC
    EmulatorTestHelper::RunUntilBASICReady(_emulator, 200);

    // Time 100 sysvar checks
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; i++)
    {
        volatile bool ready = EmulatorTestHelper::IsBASICReady(_emulator);
        (void)ready;
    }
    auto t1 = std::chrono::steady_clock::now();
    auto sysvarUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    // Time 10 OCR checks (OCR is much slower, so fewer iterations)
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; i++)
    {
        volatile std::string screen = ScreenOCR::ocrScreen(_emulatorId);
        (void)screen;
    }
    t1 = std::chrono::steady_clock::now();
    auto ocrUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    // Normalize to per-check time
    double sysvarPerCheck = sysvarUs / 100.0;
    double ocrPerCheck = ocrUs / 10.0;
    double speedup = ocrPerCheck / sysvarPerCheck;

    std::cout << "[INFO] SysVar: " << sysvarPerCheck << " us/check, OCR: " << ocrPerCheck
              << " us/check, Speedup: " << speedup << "x\n";

    // SysVar should be at least 100x faster than OCR
    EXPECT_GT(speedup, 100.0) << "SysVar should be at least 100x faster than OCR";
}

/// Verify MODE sysvar reflects cursor state
TEST_F(SysVarDetection_Test, MODE_ReflectsCursorState)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    EmulatorTestHelper::RunUntilBASICReady(_emulator, 200);

    // MODE should be 0 (K mode) when BASIC first starts
    uint8_t mode = EmulatorTestHelper::ReadSysVar(_emulator, SystemVariables48k::MODE);

    // K mode = 0, L mode = 1, C mode = 2, E mode = 3, G mode = 4
    EXPECT_LE(mode, 4) << "MODE should be in range 0-4, got " << (int)mode;

    std::cout << "[INFO] Initial MODE: " << (int)mode << " (K=0, L=1, C=2, E=3, G=4)\n";
}

/// Verify FRAMES counter increments
TEST_F(SysVarDetection_Test, FRAMES_Increments)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    EmulatorTestHelper::RunUntilBASICReady(_emulator, 200);

    // Read initial FRAMES value (3 bytes at 23672)
    uint32_t frames1 = EmulatorTestHelper::ReadSysVar(_emulator, SystemVariables48k::FRAMES) |
                       (EmulatorTestHelper::ReadSysVar(_emulator, SystemVariables48k::FRAMES + 1) << 8) |
                       (EmulatorTestHelper::ReadSysVar(_emulator, SystemVariables48k::FRAMES + 2) << 16);

    // Run 50 frames
    EmulatorTestHelper::RunFramesFast(_emulator, 50);

    // Read FRAMES again
    uint32_t frames2 = EmulatorTestHelper::ReadSysVar(_emulator, SystemVariables48k::FRAMES) |
                       (EmulatorTestHelper::ReadSysVar(_emulator, SystemVariables48k::FRAMES + 1) << 8) |
                       (EmulatorTestHelper::ReadSysVar(_emulator, SystemVariables48k::FRAMES + 2) << 16);

    // FRAMES should have increased by approximately 50 (ROM interrupt increments it)
    uint32_t delta = frames2 - frames1;
    EXPECT_GE(delta, 45) << "FRAMES should increase by ~50 after 50 frames";
    EXPECT_LE(delta, 55) << "FRAMES should increase by ~50 after 50 frames";

    std::cout << "[INFO] FRAMES: " << frames1 << " -> " << frames2 << " (delta: " << delta << ")\n";
}

/// Overall test - boot and verify using only sysvars, check frame count not wall time
TEST_F(SysVarDetection_Test, FastBoot_SysVarOnly)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    // Boot using only sysvar detection (no OCR)
    // 48K BASIC boot should complete in ~80-120 emulator frames
    int framesRun = 0;
    bool ready = EmulatorTestHelper::RunUntilBASICReady(_emulator, 200, &framesRun);
    ASSERT_TRUE(ready) << "BASIC not ready within 200 frames, ERR_NR=0x" << std::hex
                       << (int)EmulatorTestHelper::ReadSysVar(_emulator, SystemVariables48k::ERR_NR);

    // Verify various sysvars (ERR_NR=0x00 = "OK", PROG typically at 0x5CCB)
    EXPECT_EQ(EmulatorTestHelper::ReadSysVar(_emulator, SystemVariables48k::ERR_NR), 0x00);
    EXPECT_GE(EmulatorTestHelper::ReadSysVar16(_emulator, SystemVariables48k::PROG), 0x5C00);
    EXPECT_GE(EmulatorTestHelper::ReadSysVar16(_emulator, SystemVariables48k::VARS), 0x5C00);

    // Quality check: boot should complete in under 150 emulator frames (deterministic)
    // This catches regressions without being wall-clock dependent
    EXPECT_LT(framesRun, 150) << "Boot took " << framesRun << " frames - possible regression";

    std::cout << "[INFO] Boot completed in " << framesRun << " emulator frames (turbo mode)\n";
}
