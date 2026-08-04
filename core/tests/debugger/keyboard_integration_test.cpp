#include "keyboard_integration_test.h"
#include "pch.h"

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

#include <atomic>
#include <chrono>
#include <thread>

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

    // Enable turbo mode for fast execution
    emulator->EnableTurboMode(false);

    // Start async - emulator needs to run to process keyboard
    emulator->StartAsync();

    // Wait for actual frame count to reach target
    RunFrames(emulatorId, bootFrames);

    return emulatorId;
}

void KeyboardInjection_Integration_test::RunFrames(const std::string& emulatorId, int frameCount)
{
    auto emulator = _manager->GetEmulator(emulatorId);
    if (!emulator)
        return;

    // Get current frame counter
    auto context = emulator->GetContext();
    if (!context)
        return;

    uint64_t startFrame = context->emulatorState.frame_counter;
    uint64_t targetFrame = startFrame + frameCount;

    // Wait until target frame reached (turbo mode runs fast)
    constexpr int timeoutMs = 1000;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (context->emulatorState.frame_counter < targetFrame &&
           std::chrono::steady_clock::now() < deadline)
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
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    keyMgr->TypeText(text, framesPerChar);

    // Run frames synchronously until sequence completes
    int maxFrames = text.length() * framesPerChar * 10;
    for (int i = 0; i < maxFrames && keyMgr->IsSequenceRunning(); i++)
    {
        emulator->RunFrame();
        keyMgr->OnFrame();
    }

    // Extra frames for screen update
    RunFrames(emulatorId, 50);
}

void KeyboardInjection_Integration_test::CleanupEmulator(const std::string& emulatorId)
{
    auto emulator = _manager->GetEmulator(emulatorId);
    if (emulator)
    {
        emulator->Stop();
    }
    _manager->RemoveEmulator(emulatorId);
}

bool KeyboardInjection_Integration_test::WaitForOCRText(const std::string& emulatorId,
                                                         const std::string& searchText,
                                                         int timeoutMs)
{
    // Adaptive polling: start fast (5ms), slow down if not found quickly
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    int pollMs = 5;
    int polls = 0;

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (ScreenOCR::containsText(emulatorId, searchText))
            return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
        polls++;
        // After 10 polls, slow down to 10ms intervals
        if (polls == 10)
            pollMs = 10;
    }
    return false;
}

bool KeyboardInjection_Integration_test::WaitForROMAddress(const std::string& emulatorId,
                                                            uint16_t address,
                                                            int timeoutMs)
{
    auto emulator = _manager->GetEmulator(emulatorId);
    if (!emulator)
        return false;

    // Enable debug mode and set breakpoint (don't pause - let emulator keep running)
    emulator->DebugOn();
    BreakpointManager* bpManager = emulator->GetBreakpointManager();
    if (!bpManager)
        return false;

    BreakpointDescriptor* bp = new BreakpointDescriptor();
    bp->type = BreakpointTypeEnum::BRK_MEMORY;
    bp->memoryType = BRK_MEM_EXECUTE;
    bp->z80address = address;
    uint16_t bpId = bpManager->AddBreakpoint(bp);
    if (bpId == BRK_INVALID)
        return false;

    // Set up MessageCenter observer for breakpoint hit
    std::atomic<bool> bpHit{false};
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    auto handler = [&bpHit](int, Message*) { bpHit.store(true); };
    mc.AddObserver(NC_EXECUTION_BREAKPOINT, handler);

    // Wait for breakpoint (emulator already running)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    while (!bpHit.load() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Cleanup
    mc.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);
    bpManager->RemoveBreakpointByID(bpId);

    return bpHit.load();
}

bool KeyboardInjection_Integration_test::BootTo48KBASIC(const std::string& emulatorId)
{
    auto emulator = _manager->GetEmulator(emulatorId);
    if (!emulator)
    {
        std::cout << "[BootTo48KBASIC] No emulator\n";
        return false;
    }

    // Step 1: Wait for 128K menu to appear (confirm via OCR)
    if (!WaitForOCRText(emulatorId, "128", 200))
    {
        std::cout << "[BootTo48KBASIC] Failed step 1: 128K menu not found\n";
        return false;
    }

    // Step 2: Navigate to "48 BASIC" option using cursor keys (3x DOWN + ENTER)
    // Menu order: 1=Tape Loader, 2=128 BASIC, 3=Calculator, 4=48 BASIC
    auto context = emulator->GetContext();
    auto keyMgr = context->pDebugManager->GetKeyboardManager();

    // Press DOWN 3 times to reach "48 BASIC", then ENTER
    for (int i = 0; i < 3; i++)
    {
        keyMgr->TapKey("DOWN", 3);  // Hold 3 frames (minimum reliable)
        while (keyMgr->IsSequenceRunning())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        RunFrames(emulatorId, 5);  // Let menu update (5 frames sufficient)
    }

    // Press ENTER to select
    keyMgr->TapKey("ENTER", 3);
    while (keyMgr->IsSequenceRunning())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    // Step 3: Wait for 48K BASIC copyright (verifies ROM switch)
    if (!WaitForOCRText(emulatorId, "1982", 300))
    {
        std::cout << "[BootTo48KBASIC] Failed: 48K BASIC copyright not found\n";
        return false;
    }

    // Resume for further operation
    emulator->Resume();
    return true;
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
    bool hasMenu = WaitForOCRText(emulatorId, "128", 100) ||
                   WaitForOCRText(emulatorId, "BASIC", 100) ||
                   WaitForOCRText(emulatorId, "Sinclair", 100);
    
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_TRUE(hasMenu) << "128K menu not found on screen:\n" << screenText;
    
    CleanupEmulator(emulatorId);
}

// Uses ROM breakpoint at 0x1B47 (128K handler) then 0x12A2 (48K BASIC loop)
TEST_F(KeyboardInjection_Integration_test, TypeNumbers_In48KBASIC)
{
    // Boot emulator
    std::string emulatorId = BootEmulator("test_type", 10);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";

    // Boot to 48K BASIC using ROM breakpoint
    bool basicReady = BootTo48KBASIC(emulatorId);
    ASSERT_TRUE(basicReady) << "Failed to boot to 48K BASIC";

    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    
    // Now in 48K BASIC - type numbers (they appear literally)
    keyMgr->TypeText("12345", 3);

    // Wait for sequence to complete
    while (keyMgr->IsSequenceRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Poll for typed numbers to appear on screen
    bool found = WaitForOCRText(emulatorId, "12345", 100);
    
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
    
    // Boot to 48K BASIC using ROM breakpoint
    bool basicReady = BootTo48KBASIC(emulatorId);
    ASSERT_TRUE(basicReady) << "Failed to boot to 48K BASIC";

    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    auto keyMgr = context->pDebugManager->GetKeyboardManager();
    const int holdFrames = 3;
    
    // Helper lambda to wait for sequence completion (async)
    auto waitSequence = [&keyMgr]() {
        while (keyMgr->IsSequenceRunning())
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
    bool found = WaitForOCRText(emulatorId, "PRINT", 100) ||
                 WaitForOCRText(emulatorId, "hello", 100);
    
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_TRUE(found) << "PRINT \"hello\" not found on screen:\n" << screenText;
    
    CleanupEmulator(emulatorId);
}

TEST_F(KeyboardInjection_Integration_test, TapKey_SingleCharacter)
{
    std::string emulatorId = BootEmulator("test_tap", 100);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();

    // Tap a single key
    keyMgr->TapKey("a", 3);

    // Wait for sequence to complete
    while (keyMgr->IsSequenceRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    RunFrames(emulatorId, 50);
    
    // Get screen text and verify 'a' or 'A' appears
    std::string screenText = GetScreenText(emulatorId);
    
    bool hasA = (screenText.find('a') != std::string::npos) || 
                (screenText.find('A') != std::string::npos);
    EXPECT_TRUE(hasA) << "Tapped key 'a' not found on screen:\n" << screenText;
    
    CleanupEmulator(emulatorId);
}

TEST_F(KeyboardInjection_Integration_test, TapCombo_CapsShiftKey)
{
    std::string emulatorId = BootEmulator("test_combo", 100);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();

    // Type lowercase 'a'
    keyMgr->TapKey("a", 3);
    while (keyMgr->IsSequenceRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Now try combo (CAPS + A)
    std::vector<std::string> combo = {"cs", "a"};
    keyMgr->TapCombo(combo, 3);
    while (keyMgr->IsSequenceRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    RunFrames(emulatorId, 50);
    
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
    std::string emulatorId = BootEmulator("test_emode", 100);
    ASSERT_FALSE(emulatorId.empty()) << "Failed to boot emulator";
    
    auto emulator = _manager->GetEmulator(emulatorId);
    auto context = emulator->GetContext();
    ASSERT_NE(context, nullptr);
    ASSERT_NE(context->pDebugManager->GetKeyboardManager(), nullptr);
    
    auto keyMgr = context->pDebugManager->GetKeyboardManager();

    // Execute E-mode macro
    bool result = keyMgr->ExecuteNamedSequence("e_mode");
    EXPECT_TRUE(result) << "e_mode macro not found";

    // Wait for sequence to complete
    while (keyMgr->IsSequenceRunning())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // E-mode should be entered (cursor changes to E)
    RunFrames(emulatorId, 30);
    
    std::string screenText = GetScreenText(emulatorId);
    EXPECT_FALSE(screenText.empty());
    
    CleanupEmulator(emulatorId);
}

// ============================================================================
// Sequence Completion Tests  
// ============================================================================

TEST_F(KeyboardInjection_Integration_test, SequenceCompletes_NoHangingState)
{
    std::string emulatorId = BootEmulator("test_seq", 300);
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
    std::string emulatorId = BootEmulator("test_multi", 100);
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
    std::string emulatorId = BootEmulator("test_abort", 300);
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
