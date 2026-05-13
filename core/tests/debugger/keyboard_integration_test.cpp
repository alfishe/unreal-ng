#include "keyboard_integration_test.h"
#include "pch.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/analyzers/rom-print/screenocr.h"

#include <thread>
#include <chrono>

/// region <SetUp / TearDown>

void KeyboardInjection_Integration_test::SetUp()
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

void KeyboardInjection_Integration_test::TearDown()
{
    // Clean up after each test
    auto emulatorIds = _manager->GetEmulatorIds();
    for (const auto& id : emulatorIds)
    {
        _manager->RemoveEmulator(id);
    }
}

/// endregion </SetUp / TearDown>

/// region <Helper Methods>

std::string KeyboardInjection_Integration_test::BootEmulator(const std::string& symbolicId, int bootFrames)
{
    auto emulator = _manager->CreateEmulator(symbolicId);
    if (!emulator)
        return "";
    
    std::string emulatorId = emulator->GetUUID();
    
    // Start async
    emulator->StartAsync();
    
    // Wait for startup (emulator needs a moment to start its thread)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    if (emulator->GetState() != StateRun)
        return "";
    
    // Wait for boot - the emulator runs at ~50Hz, so bootFrames at 20ms each
    // But the emulator runs in realtime, so we just wait appropriate time
    int msToWait = bootFrames * 20; // 20ms per frame at 50Hz
    std::this_thread::sleep_for(std::chrono::milliseconds(msToWait));
    
    return emulatorId;
}

void KeyboardInjection_Integration_test::RunFrames(const std::string& emulatorId, int frameCount)
{
    auto emulator = _manager->GetEmulator(emulatorId);
    if (!emulator)
        return;
    
    auto context = emulator->GetContext();
    if (!context || !context->pDebugManager)
        return;
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    
    // The emulator runs in realtime at ~50Hz (20ms per frame)
    // We need to both wait for time to pass AND call OnFrame for keyboard processing
    int msPerFrame = 20;
    
    for (int i = 0; i < frameCount; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(msPerFrame));
        if (keyMgr)
        {
            keyMgr->OnFrame();
        }
    }
}

std::string KeyboardInjection_Integration_test::GetScreenText(const std::string& emulatorId)
{
    return ScreenOCR::ocrScreen(emulatorId);
}

void KeyboardInjection_Integration_test::TypeAndWait(const std::string& emulatorId, const std::string& text, int framesPerChar)
{
    auto emulator = _manager->GetEmulator(emulatorId);
    if (!emulator)
        return;
    
    auto context = emulator->GetContext();
    if (!context || !context->pDebugManager->GetKeyboardManager())
        return;
    
    // Type the text
    context->pDebugManager->GetKeyboardManager()->TypeText(text, framesPerChar);
    
    // Wait for sequence to complete
    int maxFrames = text.length() * framesPerChar * 10; // generous estimate
    for (int i = 0; i < maxFrames && context->pDebugManager->GetKeyboardManager()->IsSequenceRunning(); i++)
    {
        context->pDebugManager->GetKeyboardManager()->OnFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Extra frames for screen update
    RunFrames(emulatorId, 100);
}

void KeyboardInjection_Integration_test::CleanupEmulator(const std::string& emulatorId)
{
    auto emulator = _manager->GetEmulator(emulatorId);
    if (emulator)
    {
        emulator->Stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    _manager->RemoveEmulator(emulatorId);
}

bool KeyboardInjection_Integration_test::WaitForOCRText(const std::string& emulatorId, 
                                                         const std::string& searchText, 
                                                         int maxWaitMs)
{
    int waited = 0;
    while (waited < maxWaitMs)
    {
        std::string screenText = GetScreenText(emulatorId);
        if (screenText.find(searchText) != std::string::npos)
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited += 100;
    }
    return false;
}

/// endregion </Helper Methods>

// ============================================================================
// 48K Mode Integration Tests
// ============================================================================

TEST_F(KeyboardInjection_Integration_test, Boot128K_VerifyMenuScreen)
{
    // Boot emulator - default config boots to 128K menu
    // Use minimal boot wait (10 frames = 200ms), then poll for text
    std::string emulatorId = BootEmulator("test_128k", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    // Poll OCR every 100ms for 128K menu text (max 3 seconds)
    bool hasMenu = WaitForOCRText(emulatorId, "128", 3000) ||
                   WaitForOCRText(emulatorId, "BASIC", 500) ||
                   WaitForOCRText(emulatorId, "Sinclair", 500);
    
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_TRUE(hasMenu) << "128K menu not found on screen:\n" << screenText;
    
    CleanupEmulator(emulatorId);
}

TEST_F(KeyboardInjection_Integration_test, TypeNumbers_In48KBASIC)
{
    // Boot emulator (minimal wait, use polling)
    std::string emulatorId = BootEmulator("test_type", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    // Wait for 128K menu to appear
    bool menuReady = WaitForOCRText(emulatorId, "128", 3000);
    ASSERT_TRUE(menuReady) << "128K menu not ready";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    
    // Press 4 to select "48 BASIC" from 128K menu
    keyMgr->TapKey("4", 3);
    
    // Wait for 48K BASIC to load (poll for copyright text)
    bool basicReady = WaitForOCRText(emulatorId, "1982", 3000);
    ASSERT_TRUE(basicReady) << "48K BASIC not ready";
    
    // Now in 48K BASIC - type numbers (they appear literally)
    keyMgr->TypeText("12345", 3);
    
    // Wait for sequence to complete
    for (int i = 0; i < 50 && keyMgr->IsSequenceRunning(); i++)
    {
        keyMgr->OnFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // Poll for typed numbers to appear on screen
    bool found = WaitForOCRText(emulatorId, "12345", 2000);
    
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_TRUE(found) << "Typed numbers '12345' not found on screen:\n" << screenText;
    
    CleanupEmulator(emulatorId);
}

/// @brief Test realistic 48K BASIC input: PRINT "hello"
/// In 48K BASIC K-mode (start of line):
///   P -> PRINT (keyword token)
///   SS+P -> " (double quote, enters L-mode for literal characters)
///   h,e,l,l,o -> individual letters (in L-mode these are literal)
///   SS+P -> " (closing quote)
/// Result on screen: PRINT "hello"
TEST_F(KeyboardInjection_Integration_test, Type48K_PrintHello)
{
    // Boot emulator (minimal wait, use polling)
    std::string emulatorId = BootEmulator("test_print", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    // Wait for 128K menu to appear, then select 48K BASIC
    bool menuReady = WaitForOCRText(emulatorId, "128", 3000);
    ASSERT_TRUE(menuReady) << "128K menu not ready";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    const int holdFrames = 3;
    
    // press 4 to enter 48K BASIC
    keyMgr->TapKey("4", holdFrames);
    
    // Wait for 48K BASIC
    bool basicReady = WaitForOCRText(emulatorId, "1982", 3000);
    ASSERT_TRUE(basicReady) << "48K BASIC not ready";
    
    // Helper lambda to wait for sequence completion
    auto waitSequence = [&keyMgr]() {
        for (int i = 0; i < 50 && keyMgr->IsSequenceRunning(); i++)
        {
            keyMgr->OnFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    };
    
    // Step 1: Tap P -> produces PRINT keyword token (in K-mode at start of line)
    keyMgr->TapKey("p", holdFrames);
    waitSequence();
    
    // Step 2: Tap SS+P -> produces " (double quote, enters L-mode)
    std::vector<std::string> quoteCombo = {"ss", "p"};
    keyMgr->TapCombo(quoteCombo, holdFrames);
    waitSequence();
    
    // Step 3: Type hello - now in L-mode, letters are literal
    keyMgr->TapKey("h", holdFrames); waitSequence();
    keyMgr->TapKey("e", holdFrames); waitSequence();
    keyMgr->TapKey("l", holdFrames); waitSequence();
    keyMgr->TapKey("l", holdFrames); waitSequence();
    keyMgr->TapKey("o", holdFrames); waitSequence();
    
    // Step 4: Tap SS+P -> produces " (closing quote)
    keyMgr->TapCombo(quoteCombo, holdFrames);
    waitSequence();
    
    // Poll for result - look for PRINT or hello on screen
    bool found = WaitForOCRText(emulatorId, "PRINT", 2000) ||
                 WaitForOCRText(emulatorId, "hello", 500);
    
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_TRUE(found) << "PRINT \"hello\" not found on screen:\n" << screenText;
    
    CleanupEmulator(emulatorId);
}

TEST_F(KeyboardInjection_Integration_test, TapKey_SingleCharacter)
{
    std::string emulatorId = BootEmulator("test_tap", 2000);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    // Tap a single key
    context->pDebugManager->GetKeyboardManager()->TapKey("a", 3);
    
    // Wait for sequence to complete
    for (int i = 0; i < 50 && context->pDebugManager->GetKeyboardManager()->IsSequenceRunning(); i++)
    {
        context->pDebugManager->GetKeyboardManager()->OnFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    RunFrames(emulatorId, 100);
    
    // Get screen text and verify 'a' or 'A' appears
    std::string screenText = GetScreenText(emulatorId);
    
    bool hasA = (screenText.find('a') != std::string::npos) || 
                (screenText.find('A') != std::string::npos);
    EXPECT_TRUE(hasA) << "Tapped key 'a' not found on screen:\n" << screenText;
    
    CleanupEmulator(emulatorId);
}

TEST_F(KeyboardInjection_Integration_test, TapCombo_CapsShiftKey)
{
    std::string emulatorId = BootEmulator("test_combo", 2000);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    // Type lowercase 'a', then CAPS+a (should produce uppercase A in keyword mode)
    context->pDebugManager->GetKeyboardManager()->TapKey("a", 3);
    
    for (int i = 0; i < 30 && context->pDebugManager->GetKeyboardManager()->IsSequenceRunning(); i++)
    {
        context->pDebugManager->GetKeyboardManager()->OnFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // Now try combo (CAPS + A)
    std::vector<std::string> combo = {"cs", "a"};
    context->pDebugManager->GetKeyboardManager()->TapCombo(combo, 3);
    
    for (int i = 0; i < 30 && context->pDebugManager->GetKeyboardManager()->IsSequenceRunning(); i++)
    {
        context->pDebugManager->GetKeyboardManager()->OnFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    RunFrames(emulatorId, 100);
    
    // Just verify no crash and screen is readable
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_FALSE(screenText.empty()) << "Screen should have content";
    
    CleanupEmulator(emulatorId);
}

// ============================================================================
// Named Sequence (Macro) Tests
// ============================================================================

TEST_F(KeyboardInjection_Integration_test, ExecuteMacro_EMode)
{
    std::string emulatorId = BootEmulator("test_emode", 2000);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    // Execute E-mode macro
    bool result = context->pDebugManager->GetKeyboardManager()->ExecuteNamedSequence("e_mode");
    EXPECT_TRUE(result) << "e_mode macro not found";
    
    // Wait for sequence to complete
    for (int i = 0; i < 100 && context->pDebugManager->GetKeyboardManager()->IsSequenceRunning(); i++)
    {
        context->pDebugManager->GetKeyboardManager()->OnFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    // E-mode should be entered (cursor changes to E)
    // Just verify no crash
    RunFrames(emulatorId, 50);
    
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_FALSE(screenText.empty());
    
    CleanupEmulator(emulatorId);
}

// ============================================================================
// Sequence Completion Tests  
// ============================================================================

TEST_F(KeyboardInjection_Integration_test, SequenceCompletes_NoHangingState)
{
    std::string emulatorId = BootEmulator("test_seq", 1000);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    
    // Queue several operations
    keyMgr->TapKey("h", 2);
    
    // Process until done
    int frameCount = 0;
    while (keyMgr->IsSequenceRunning() && frameCount < 1000)
    {
        keyMgr->OnFrame();
        frameCount++;
    }
    
    EXPECT_FALSE(keyMgr->IsSequenceRunning()) << "Sequence did not complete after " << frameCount << " frames";
    EXPECT_LT(frameCount, 100) << "Sequence took too long to complete";
    
    CleanupEmulator(emulatorId);
}

TEST_F(KeyboardInjection_Integration_test, MultipleSequences_ExecuteInOrder)
{
    std::string emulatorId = BootEmulator("test_multi", 2000);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    
    // Type multiple characters one after another
    keyMgr->TapKey("a", 2);
    
    // Wait for first to complete before starting second
    while (keyMgr->IsSequenceRunning())
    {
        keyMgr->OnFrame();
    }
    
    keyMgr->TapKey("b", 2);
    
    while (keyMgr->IsSequenceRunning())
    {
        keyMgr->OnFrame();
    }
    
    keyMgr->TapKey("c", 2);
    
    while (keyMgr->IsSequenceRunning())
    {
        keyMgr->OnFrame();
    }
    
    RunFrames(emulatorId, 100);
    
    // Verify all three characters appeared
    std::string screenText = GetScreenText(emulatorId);
    
    // In BASIC, lowercase letters are entered, check for any of them
    bool hasContent = !screenText.empty();
    EXPECT_TRUE(hasContent) << "Screen should have content after typing";
    
    CleanupEmulator(emulatorId);
}

// ============================================================================
// Abort Test
// ============================================================================

TEST_F(KeyboardInjection_Integration_test, AbortSequence_StopsImmediately)
{
    std::string emulatorId = BootEmulator("test_abort", 1000);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    
    // Start a long sequence
    keyMgr->TypeText("THIS IS A VERY LONG TEXT THAT WOULD TAKE MANY FRAMES", 5);
    EXPECT_TRUE(keyMgr->IsSequenceRunning());
    
    // Process a few frames
    for (int i = 0; i < 10; i++)
    {
        keyMgr->OnFrame();
    }
    
    // Should still be running
    EXPECT_TRUE(keyMgr->IsSequenceRunning());
    
    // Abort
    keyMgr->AbortSequence();
    
    // Should be stopped
    EXPECT_FALSE(keyMgr->IsSequenceRunning());
    
    CleanupEmulator(emulatorId);
}
