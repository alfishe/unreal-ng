#pragma once

#include <gtest/gtest.h>
#include <string>

class EmulatorManager;
class Emulator;

/// @brief Integration test fixture for keyboard injection across different ZX Spectrum modes
/// Tests boot into 48K, 128K, and TR-DOS modes, inject keyboard sequences, and verify via OCR.
/// Uses ROM breakpoints for deterministic timing + OCR for state verification.
class KeyboardInjection_Integration_test : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;

    /// Boot emulator and run frames until stable
    std::string BootEmulator(const std::string& symbolicId, int bootFrames = 1000);

    /// Run N frames on the emulator (waits for frame counter)
    void RunFrames(const std::string& emulatorId, int frameCount);

    /// Get screen text via OCR
    std::string GetScreenText(const std::string& emulatorId);

    /// Type text using keyboard injection and wait for it to appear
    void TypeAndWait(const std::string& emulatorId, const std::string& text, int framesPerChar = 3);

    /// Clean up emulator
    void CleanupEmulator(const std::string& emulatorId);

    /// Wait for specific text to appear on screen via OCR polling
    bool WaitForOCRText(const std::string& emulatorId, const std::string& searchText, int timeoutMs = 1000);

    /// Wait for ROM execution to reach a specific address (uses breakpoint)
    /// @return true if address hit within timeout
    bool WaitForROMAddress(const std::string& emulatorId, uint16_t address, int timeoutMs = 1000);

    /// Boot to 48K BASIC using ROM breakpoint at 0x12A2 (BASIC main loop)
    /// @return true if successfully entered 48K BASIC
    bool BootTo48KBASIC(const std::string& emulatorId);

    // Known ROM addresses for state detection
    static constexpr uint16_t BASIC_MAIN_EXEC = 0x12A2;   // 48K BASIC main loop entry (SOS ROM)
    static constexpr uint16_t KEY_SCAN = 0x028E;          // Keyboard scan routine
    static constexpr uint16_t MENU_48_BASIC = 0x1B47;     // 128K ROM: "48 BASIC" menu option handler
    static constexpr uint16_t MENU_KEY_WAIT = 0x367F;     // 128K ROM: Menu key wait routine
    static constexpr uint16_t MENU_KEY_LOOP = 0x3683;     // 128K ROM: Menu key scan loop

protected:
    EmulatorManager* _manager = nullptr;
};
