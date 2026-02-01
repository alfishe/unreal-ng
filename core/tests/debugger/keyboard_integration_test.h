#pragma once

#include <gtest/gtest.h>
#include <string>

class EmulatorManager;
class Emulator;

/// @brief Integration test fixture for keyboard injection across different ZX Spectrum modes
/// Tests boot into 48K, 128K, and TR-DOS modes, inject keyboard sequences, and verify via OCR
class KeyboardInjection_Integration_test : public ::testing::Test
{
protected:
    void SetUp() override;
    void TearDown() override;
    
    /// Boot emulator and run frames until stable
    /// @param symbolicId Symbolic ID for the emulator
    /// @param bootFrames Number of frames to run for boot
    /// @return Emulator ID if successful, empty string on failure
    std::string BootEmulator(const std::string& symbolicId, int bootFrames = 1000);
    
    /// Run N frames on the emulator
    /// @param emulatorId The emulator ID
    /// @param frameCount Number of frames to execute
    void RunFrames(const std::string& emulatorId, int frameCount);
    
    /// Get screen text via OCR
    /// @param emulatorId The emulator ID  
    /// @return Text on screen (24 lines)
    std::string GetScreenText(const std::string& emulatorId);
    
    /// Type text using keyboard injection and wait for it to appear
    /// @param emulatorId The emulator ID
    /// @param text Text to type
    /// @param framesPerChar Frames between characters
    void TypeAndWait(const std::string& emulatorId, const std::string& text, int framesPerChar = 3);
    
    /// Clean up emulator
    void CleanupEmulator(const std::string& emulatorId);
    
    /// Wait for specific text to appear on screen via OCR polling
    /// @param emulatorId The emulator ID
    /// @param searchText Text to search for
    /// @param maxWaitMs Maximum time to wait in milliseconds
    /// @return true if text found within timeout
    bool WaitForOCRText(const std::string& emulatorId, const std::string& searchText, int maxWaitMs = 3000);

protected:
    EmulatorManager* _manager = nullptr;
};
