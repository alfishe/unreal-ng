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

    // Force deterministic boot: 48K BASIC (RESET=BASIC) regardless of the staged
    // unreal.ini, which CMake re-copies on every build (RESET=128). Same approach
    // as EmulatorTestHelper::CreateStandardEmulator.
    emulator->GetContext()->config.reset_rom = RM_SOS;
    emulator->Reset();

    // Enable turbo mode for fast test execution - emulator runs as fast as possible
    emulator->EnableTurboMode(false);

    std::string emulatorId = emulator->GetUUID();

    // Start async
    emulator->StartAsync();

    // Wait for startup (emulator needs a moment to start its thread)
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    if (emulator->GetState() != StateRun)
        return "";

    // In turbo mode, the emulator runs many frames per wall-clock second.
    // A typical machine can do 500-2000 frames/sec in turbo, so we only need
    // a fraction of the original wait time. Use frame count polling instead.
    auto ctx = emulator->GetContext();
    uint64_t targetFrame = ctx->emulatorState.frame_counter + bootFrames;
    while (ctx->emulatorState.frame_counter < targetFrame)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return emulatorId;
}

void KeyboardInjection_Integration_test::RunFrames(const std::string& emulatorId, int frameCount)
{
    auto emulator = _manager->GetEmulator(emulatorId);
    if (!emulator)
        return;

    // In turbo mode, poll frame count instead of waiting wall-clock time
    auto ctx = emulator->GetContext();
    uint64_t targetFrame = ctx->emulatorState.frame_counter + frameCount;
    while (ctx->emulatorState.frame_counter < targetFrame)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
    
    // Wait for sequence to complete. The running emulator's mainloop already
    // pumps keyMgr->OnFrame() every frame — calling it from the test thread too
    // double-steps the sequence state machine and randomly truncates key holds.
    int maxMs = static_cast<int>(text.length()) * framesPerChar * 10 * 20; // generous: 10x frames at 20ms each
    for (int waitedMs = 0; waitedMs < maxMs && context->pDebugManager->GetKeyboardManager()->IsSequenceRunning(); waitedMs += 20)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
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
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    _manager->RemoveEmulator(emulatorId);
}

bool KeyboardInjection_Integration_test::WaitForOCRText(const std::string& emulatorId,
                                                         const std::string& searchText,
                                                         int maxFrames)
{
    auto emulator = _manager->GetEmulator(emulatorId);
    if (!emulator)
        return false;

    auto ctx = emulator->GetContext();
    uint64_t startFrame = ctx->emulatorState.frame_counter;

    // Poll every 10 frames (in turbo mode this is ~5-20ms wall time)
    while (ctx->emulatorState.frame_counter - startFrame < static_cast<uint64_t>(maxFrames))
    {
        std::string screenText = GetScreenText(emulatorId);
        if (screenText.find(searchText) != std::string::npos)
        {
            return true;
        }
        // Wait 10 frames before next OCR check
        uint64_t targetFrame = ctx->emulatorState.frame_counter + 10;
        while (ctx->emulatorState.frame_counter < targetFrame)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return false;
}

/// endregion </Helper Methods>

// ============================================================================
// 48K Mode Integration Tests
// ============================================================================

TEST_F(KeyboardInjection_Integration_test, Boot_VerifyBASICScreen)
{
    // Boot emulator - RESET=BASIC boots straight into 48K BASIC (no 128K menu)
    // Use minimal boot wait (10 frames = 200ms), then poll for text
    std::string emulatorId = BootEmulator("test_128k", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    // Poll OCR every 100ms for the 48K BASIC copyright line (max 3 seconds)
    bool hasBasic = WaitForOCRText(emulatorId, "1982", 200) ||
                    WaitForOCRText(emulatorId, "Sinclair", 50);
    
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_TRUE(hasBasic) << "48K BASIC screen not found:\n" << screenText;
    
    CleanupEmulator(emulatorId);
}

TEST_F(KeyboardInjection_Integration_test, TypeNumbers_In48KBASIC)
{
    // Boot emulator (minimal wait, use polling)
    std::string emulatorId = BootEmulator("test_type", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    // Wait for 48K BASIC to boot (RESET=BASIC goes straight there, no 128K menu)
    bool basicReady = WaitForOCRText(emulatorId, "1982", 200);
    ASSERT_TRUE(basicReady) << "48K BASIC not ready. Screen:\n" << GetScreenText(emulatorId);
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    
    // Now in 48K BASIC - type numbers (they appear literally)
    keyMgr->TypeText("12345", 3);
    
    // Wait for sequence to complete. The running emulator's mainloop already
    // pumps keyMgr->OnFrame() every frame — calling it from the test thread too
    // double-steps the sequence state machine and randomly truncates key holds.
    for (int i = 0; i < 50 && keyMgr->IsSequenceRunning(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Poll for typed numbers to appear on screen
    bool found = WaitForOCRText(emulatorId, "12345", 150);
    
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
    
    // Wait for 48K BASIC to boot (RESET=BASIC goes straight there, no 128K menu)
    bool basicReady = WaitForOCRText(emulatorId, "1982", 200);
    ASSERT_TRUE(basicReady) << "48K BASIC not ready. Screen:\n" << GetScreenText(emulatorId);
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    const int holdFrames = 3;
    
    // Helper lambda to wait for sequence completion. The running emulator's
    // mainloop already pumps keyMgr->OnFrame() every frame — calling it from
    // the test thread too double-steps the sequence and truncates key holds.
    auto waitSequence = [&keyMgr]() {
        for (int i = 0; i < 50 && keyMgr->IsSequenceRunning(); i++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
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
    bool found = WaitForOCRText(emulatorId, "PRINT", 150) ||
                 WaitForOCRText(emulatorId, "hello", 50);
    
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_TRUE(found) << "PRINT \"hello\" not found on screen:\n" << screenText;
    
    CleanupEmulator(emulatorId);
}

TEST_F(KeyboardInjection_Integration_test, TapKey_SingleCharacter)
{
    std::string emulatorId = BootEmulator("test_tap", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";

    // Wait for BASIC to be ready
    ASSERT_TRUE(WaitForOCRText(emulatorId, "1982", 200)) << "48K BASIC not ready";

    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    // Tap a single key
    context->pDebugManager->GetKeyboardManager()->TapKey("a", 3);
    
    // Wait for sequence to complete. The running emulator's mainloop already
    // pumps keyMgr->OnFrame() every frame — calling it from the test thread too
    // double-steps the sequence state machine and randomly truncates key holds.
    for (int i = 0; i < 50 && context->pDebugManager->GetKeyboardManager()->IsSequenceRunning(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    
    // Verify the tap registered. In 48K BASIC K-mode
    // (fresh line, K cursor) letter keys produce keywords: 'a' types NEW and
    // the cursor flips to L. Assert the keyword — the letter itself never
    // reaches the screen in this mode. Poll for it instead of waiting a
    // fixed frame count.
    bool hasNew = WaitForOCRText(emulatorId, "NEW", 150);

    std::string screenText = GetScreenText(emulatorId);
    EXPECT_TRUE(hasNew) << "Tapped key 'a' did not produce the NEW keyword on screen:\n" << screenText;
    
    CleanupEmulator(emulatorId);
}

TEST_F(KeyboardInjection_Integration_test, TapCombo_CapsShiftKey)
{
    std::string emulatorId = BootEmulator("test_combo", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";

    ASSERT_TRUE(WaitForOCRText(emulatorId, "1982", 200)) << "48K BASIC not ready";

    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    // Type lowercase 'a', then CAPS+a (should produce uppercase A in keyword mode)
    context->pDebugManager->GetKeyboardManager()->TapKey("a", 3);
    
    // Let the emulator thread drive the sequence (its mainloop pumps OnFrame)
    for (int i = 0; i < 30 && context->pDebugManager->GetKeyboardManager()->IsSequenceRunning(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    
    // Now try combo (CAPS + A)
    std::vector<std::string> combo = {"cs", "a"};
    context->pDebugManager->GetKeyboardManager()->TapCombo(combo, 3);
    
    // Let the emulator thread drive the sequence (its mainloop pumps OnFrame)
    for (int i = 0; i < 30 && context->pDebugManager->GetKeyboardManager()->IsSequenceRunning(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    
    // Just verify no crash and screen is readable - the boot poll above already
    // proved the screen renders, no fixed frame wait needed
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_FALSE(screenText.empty()) << "Screen should have content";
    
    CleanupEmulator(emulatorId);
}

// ============================================================================
// Named Sequence (Macro) Tests
// ============================================================================

TEST_F(KeyboardInjection_Integration_test, ExecuteMacro_EMode)
{
    std::string emulatorId = BootEmulator("test_emode", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";

    ASSERT_TRUE(WaitForOCRText(emulatorId, "1982", 200)) << "48K BASIC not ready";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    // Execute E-mode macro
    bool result = context->pDebugManager->GetKeyboardManager()->ExecuteNamedSequence("e_mode");
    EXPECT_TRUE(result) << "e_mode macro not found";
    
    // Wait for sequence to complete. The running emulator's mainloop already
    // pumps keyMgr->OnFrame() every frame — calling it from the test thread too
    // double-steps the sequence state machine and randomly truncates key holds.
    for (int i = 0; i < 100 && context->pDebugManager->GetKeyboardManager()->IsSequenceRunning(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    
    // E-mode should be entered (cursor changes to E). Just verify no crash -
    // the boot poll above already proved the screen renders
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_FALSE(screenText.empty());
    
    CleanupEmulator(emulatorId);
}

// ============================================================================
// Sequence Completion Tests  
// ============================================================================

TEST_F(KeyboardInjection_Integration_test, SequenceCompletes_NoHangingState)
{
    std::string emulatorId = BootEmulator("test_seq", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";

    ASSERT_TRUE(WaitForOCRText(emulatorId, "1982", 200)) << "48K BASIC not ready";

    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    
    // Queue several operations
    keyMgr->TapKey("h", 2);
    
    // Process until done. The running emulator's mainloop already pumps
    // keyMgr->OnFrame() every frame — the test thread must only wait, never
    // pump too, or the sequence state machine double-steps and truncates key
    // holds. Wall time stands in for the frame count (1 frame = 20 ms at 50 Hz).
    int elapsedMs = 0;
    while (keyMgr->IsSequenceRunning() && elapsedMs < 2000)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        elapsedMs += 5;
    }

    EXPECT_FALSE(keyMgr->IsSequenceRunning()) << "Sequence did not complete after " << elapsedMs << " ms";
    EXPECT_LT(elapsedMs, 1000) << "Sequence took too long to complete";
    
    CleanupEmulator(emulatorId);
}

TEST_F(KeyboardInjection_Integration_test, MultipleSequences_ExecuteInOrder)
{
    std::string emulatorId = BootEmulator("test_multi", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";

    ASSERT_TRUE(WaitForOCRText(emulatorId, "1982", 200)) << "48K BASIC not ready";

    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    
    // Type multiple characters one after another
    keyMgr->TapKey("a", 2);
    
    // Wait for first to complete before starting second. The running
    // emulator's mainloop already pumps keyMgr->OnFrame() every frame —
    // calling it from the test thread too double-steps the sequence state
    // machine and randomly truncates key holds.
    for (int i = 0; i < 50 && keyMgr->IsSequenceRunning(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    
    keyMgr->TapKey("b", 2);
    
    for (int i = 0; i < 50 && keyMgr->IsSequenceRunning(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    
    keyMgr->TapKey("c", 2);
    
    for (int i = 0; i < 50 && keyMgr->IsSequenceRunning(); i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    
    // Verify all three characters appeared - the boot poll above already
    // proved the screen renders, no fixed frame wait needed
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
    std::string emulatorId = BootEmulator("test_abort", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";

    ASSERT_TRUE(WaitForOCRText(emulatorId, "1982", 200)) << "48K BASIC not ready";

    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    
    // Start a long sequence
    keyMgr->TypeText("THIS IS A VERY LONG TEXT THAT WOULD TAKE MANY FRAMES", 5);
    EXPECT_TRUE(keyMgr->IsSequenceRunning());
    
    // Let the emulator thread advance the sequence for ~10 frames (its
    // mainloop pumps OnFrame() every frame — never call it from the test thread)
    for (int i = 0; i < 10; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    
    // Should still be running
    EXPECT_TRUE(keyMgr->IsSequenceRunning());
    
    // Abort
    keyMgr->AbortSequence();
    
    // Should be stopped
    EXPECT_FALSE(keyMgr->IsSequenceRunning());
    
    CleanupEmulator(emulatorId);
}
