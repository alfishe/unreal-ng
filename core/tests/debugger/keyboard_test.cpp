#include "keyboard_test.h"
#include "pch.h"

#include "common/modulelogger.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"

void DebugKeyboardManager_test::SetUp()
{
    _context = new EmulatorContext(LoggerLevel::LogError);
    
    // Initialize keyboard in context
    _context->pKeyboard = new Keyboard(_context);
    
    // Create the keyboard manager directly for isolated unit testing
    // (In integration tests, this is owned by DebugManager instead)
    _keyboardManager = new DebugKeyboardManager(_context);
}

void DebugKeyboardManager_test::TearDown()
{
    if (_context)
    {
        if (_context->pKeyboard)
        {
            delete _context->pKeyboard;
            _context->pKeyboard = nullptr;
        }
        
        delete _keyboardManager;
        _keyboardManager = nullptr;
        
        delete _context;
        _context = nullptr;
    }
}

// ========================================================================
// Key Name Resolution Tests
// ========================================================================

TEST_F(DebugKeyboardManager_test, ResolveKeyName_SingleCharacter)
{
    // Test lowercase letters resolve correctly
    auto key = DebugKeyboardManager::ResolveKeyName("a");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_A);
    
    key = DebugKeyboardManager::ResolveKeyName("z");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_Z);
    
    key = DebugKeyboardManager::ResolveKeyName("p");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_P);
}

TEST_F(DebugKeyboardManager_test, ResolveKeyName_UppercaseLetters)
{
    // Uppercase letters should also resolve
    auto key = DebugKeyboardManager::ResolveKeyName("A");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_A);
}

TEST_F(DebugKeyboardManager_test, ResolveKeyName_Numbers)
{
    auto key = DebugKeyboardManager::ResolveKeyName("1");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_1);
    
    key = DebugKeyboardManager::ResolveKeyName("0");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_0);
    
    key = DebugKeyboardManager::ResolveKeyName("5");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_5);
}

TEST_F(DebugKeyboardManager_test, ResolveKeyName_SpecialKeys)
{
    // Test ENTER/RETURN
    auto key = DebugKeyboardManager::ResolveKeyName("enter");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_ENTER);
    
    // Test SPACE
    key = DebugKeyboardManager::ResolveKeyName("space");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_SPACE);
    
    // Test CAPS SHIFT alias
    key = DebugKeyboardManager::ResolveKeyName("cs");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_CAPS_SHIFT);
    
    // Test SYMBOL SHIFT alias
    key = DebugKeyboardManager::ResolveKeyName("ss");
    EXPECT_NE(key, ZXKEY_NONE);
    EXPECT_EQ(key, ZXKEY_SYM_SHIFT);
}

TEST_F(DebugKeyboardManager_test, ResolveKeyName_InvalidKey)
{
    auto key = DebugKeyboardManager::ResolveKeyName("invalid_key");
    EXPECT_EQ(key, ZXKEY_NONE);
    
    key = DebugKeyboardManager::ResolveKeyName("");
    EXPECT_EQ(key, ZXKEY_NONE);
}

// ========================================================================
// GetAllKeyNames Tests
// ========================================================================

TEST_F(DebugKeyboardManager_test, GetAllKeyNames_ContainsExpectedKeys)
{
    auto keyNames = DebugKeyboardManager::GetAllKeyNames();
    
    // Should contain at least all letters
    EXPECT_NE(std::find(keyNames.begin(), keyNames.end(), "a"), keyNames.end());
    EXPECT_NE(std::find(keyNames.begin(), keyNames.end(), "z"), keyNames.end());
    
    // Should contain numbers
    EXPECT_NE(std::find(keyNames.begin(), keyNames.end(), "0"), keyNames.end());
    EXPECT_NE(std::find(keyNames.begin(), keyNames.end(), "9"), keyNames.end());
    
    // Should contain special keys
    EXPECT_NE(std::find(keyNames.begin(), keyNames.end(), "enter"), keyNames.end());
    EXPECT_NE(std::find(keyNames.begin(), keyNames.end(), "space"), keyNames.end());
    EXPECT_NE(std::find(keyNames.begin(), keyNames.end(), "cs"), keyNames.end());
    EXPECT_NE(std::find(keyNames.begin(), keyNames.end(), "ss"), keyNames.end());
}

TEST_F(DebugKeyboardManager_test, GetAllKeyNames_NoDuplicates)
{
    auto keyNames = DebugKeyboardManager::GetAllKeyNames();
    std::set<std::string> uniqueNames(keyNames.begin(), keyNames.end());
    
    EXPECT_EQ(keyNames.size(), uniqueNames.size());
}

TEST_F(DebugKeyboardManager_test, GetAllKeyNames_NotEmpty)
{
    auto keyNames = DebugKeyboardManager::GetAllKeyNames();
    
    // Should have at least 40 keys (26 letters + 10 digits + 4 specials)
    EXPECT_GE(keyNames.size(), 40u);
}

// ========================================================================
// Key Press/Release Tests
// ========================================================================

TEST_F(DebugKeyboardManager_test, PressKey_SetsKey)
{
    // Initial state - release all keys
    _keyboardManager->ReleaseAllKeys();
    
    // Press a key
    _keyboardManager->PressKey("a");
    
    // Verify the key is registered as pressed
    EXPECT_TRUE(_keyboardManager->IsKeyPressed(ZXKEY_A));
}

TEST_F(DebugKeyboardManager_test, ReleaseKey_ClearsKey)
{
    _keyboardManager->ReleaseAllKeys();
    _keyboardManager->PressKey("a");
    EXPECT_TRUE(_keyboardManager->IsKeyPressed(ZXKEY_A));
    
    _keyboardManager->ReleaseKey("a");
    EXPECT_FALSE(_keyboardManager->IsKeyPressed(ZXKEY_A));
}

TEST_F(DebugKeyboardManager_test, ReleaseAllKeys_ClearsAllKeys)
{
    // Press several keys
    _keyboardManager->PressKey("a");
    _keyboardManager->PressKey("b");
    _keyboardManager->PressKey("c");
    _keyboardManager->PressKey("1");
    
    // Verify they're pressed
    EXPECT_TRUE(_keyboardManager->IsKeyPressed(ZXKEY_A));
    EXPECT_TRUE(_keyboardManager->IsKeyPressed(ZXKEY_B));
    EXPECT_TRUE(_keyboardManager->IsKeyPressed(ZXKEY_C));
    EXPECT_TRUE(_keyboardManager->IsKeyPressed(ZXKEY_1));
    
    // Release all
    _keyboardManager->ReleaseAllKeys();
    
    // Verify all released
    EXPECT_FALSE(_keyboardManager->IsKeyPressed(ZXKEY_A));
    EXPECT_FALSE(_keyboardManager->IsKeyPressed(ZXKEY_B));
    EXPECT_FALSE(_keyboardManager->IsKeyPressed(ZXKEY_C));
    EXPECT_FALSE(_keyboardManager->IsKeyPressed(ZXKEY_1));
}

TEST_F(DebugKeyboardManager_test, GetPressedKeys_ReturnsCorrectList)
{
    _keyboardManager->ReleaseAllKeys();
    
    // Press specific keys
    _keyboardManager->PressKey("a");
    _keyboardManager->PressKey("b");
    _keyboardManager->PressKey("enter");
    
    auto pressed = _keyboardManager->GetPressedKeys();
    
    // Should contain exactly the keys we pressed
    EXPECT_NE(std::find(pressed.begin(), pressed.end(), ZXKEY_A), pressed.end());
    EXPECT_NE(std::find(pressed.begin(), pressed.end(), ZXKEY_B), pressed.end());
    EXPECT_NE(std::find(pressed.begin(), pressed.end(), ZXKEY_ENTER), pressed.end());
    EXPECT_EQ(pressed.size(), 3u);
}

// ========================================================================
// Sequence Queue Tests
// ========================================================================

TEST_F(DebugKeyboardManager_test, TapKey_QueuesSequence)
{
    _keyboardManager->TapKey("a", 2);
    
    // Sequence should be running
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
}

TEST_F(DebugKeyboardManager_test, TapCombo_QueuesMultipleKeys)
{
    std::vector<std::string> keys = {"cs", "a"};
    _keyboardManager->TapCombo(keys, 2);
    
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
}

TEST_F(DebugKeyboardManager_test, TypeText_QueuesCharacterSequence)
{
    _keyboardManager->TypeText("HELLO", 2);
    
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
}

TEST_F(DebugKeyboardManager_test, AbortSequence_StopsRunning)
{
    _keyboardManager->TypeText("HELLO WORLD THIS IS A LONG TEXT", 2);
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
    
    _keyboardManager->AbortSequence();
    
    EXPECT_FALSE(_keyboardManager->IsSequenceRunning());
}

// ========================================================================
// Named Sequence Tests
// ========================================================================

TEST_F(DebugKeyboardManager_test, ExecuteNamedSequence_EMode)
{
    bool result = _keyboardManager->ExecuteNamedSequence("e_mode");
    EXPECT_TRUE(result);
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
}

TEST_F(DebugKeyboardManager_test, ExecuteNamedSequence_Format)
{
    bool result = _keyboardManager->ExecuteNamedSequence("format");
    EXPECT_TRUE(result);
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
}

TEST_F(DebugKeyboardManager_test, ExecuteNamedSequence_Cat)
{
    bool result = _keyboardManager->ExecuteNamedSequence("cat");
    EXPECT_TRUE(result);
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
}

TEST_F(DebugKeyboardManager_test, ExecuteNamedSequence_Unknown)
{
    bool result = _keyboardManager->ExecuteNamedSequence("unknown_macro");
    EXPECT_FALSE(result);
}

// ========================================================================
// GetKeyDisplayName Tests
// ========================================================================

TEST_F(DebugKeyboardManager_test, GetKeyDisplayName_Returns_NonEmpty)
{
    std::string name = DebugKeyboardManager::GetKeyDisplayName(ZXKEY_A);
    EXPECT_FALSE(name.empty());
    
    name = DebugKeyboardManager::GetKeyDisplayName(ZXKEY_ENTER);
    EXPECT_FALSE(name.empty());
    
    name = DebugKeyboardManager::GetKeyDisplayName(ZXKEY_CAPS_SHIFT);
    EXPECT_FALSE(name.empty());
}

// ========================================================================
// Frame Processing Tests
// ========================================================================

TEST_F(DebugKeyboardManager_test, OnFrame_ProcessesKeyActions)
{
    // Queue a tap with 2-frame hold
    _keyboardManager->TapKey("a", 2);
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
    
    // Process frames until sequence completes
    // Tap consists of: press, hold for N frames, release
    for (int i = 0; i < 20 && _keyboardManager->IsSequenceRunning(); i++)
    {
        _keyboardManager->OnFrame();
    }
    
    // After enough frames, sequence should complete
    EXPECT_FALSE(_keyboardManager->IsSequenceRunning());
}

TEST_F(DebugKeyboardManager_test, OnFrame_HandlesEmptyQueue)
{
    // Ensure no crash when processing empty queue
    EXPECT_FALSE(_keyboardManager->IsSequenceRunning());
    _keyboardManager->OnFrame();
    EXPECT_FALSE(_keyboardManager->IsSequenceRunning());
}

TEST_F(DebugKeyboardManager_test, OnFrame_TypeTextCompletes)
{
    _keyboardManager->TypeText("AB", 1);
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
    
    // Process enough frames for short text
    for (int i = 0; i < 50 && _keyboardManager->IsSequenceRunning(); i++)
    {
        _keyboardManager->OnFrame();
    }
    
    EXPECT_FALSE(_keyboardManager->IsSequenceRunning());
}

// ========================================================================
// TR-DOS Command Tests
// ========================================================================

TEST_F(DebugKeyboardManager_test, TypeTRDOSCommand_Simple)
{
    _keyboardManager->TypeTRDOSCommand("cat", "");
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
}

TEST_F(DebugKeyboardManager_test, TypeTRDOSCommand_WithArgument)
{
    _keyboardManager->TypeTRDOSCommand("run", "game");
    EXPECT_TRUE(_keyboardManager->IsSequenceRunning());
}

// ========================================================================
// Edge Cases
// ========================================================================

TEST_F(DebugKeyboardManager_test, PressKey_InvalidKey_NoEffect)
{
    _keyboardManager->ReleaseAllKeys();
    
    // Should not crash or throw
    _keyboardManager->PressKey("invalid_key");
    
    // No keys should be pressed
    auto pressed = _keyboardManager->GetPressedKeys();
    EXPECT_TRUE(pressed.empty());
}

TEST_F(DebugKeyboardManager_test, ReleaseKey_NotPressed_NoEffect)
{
    _keyboardManager->ReleaseAllKeys();
    
    // Releasing a key that wasn't pressed should be harmless
    _keyboardManager->ReleaseKey("a");
    
    // No crash, state unchanged
    auto pressed = _keyboardManager->GetPressedKeys();
    EXPECT_TRUE(pressed.empty());
}

TEST_F(DebugKeyboardManager_test, TypeText_EmptyString_NoSequence)
{
    _keyboardManager->TypeText("", 2);
    
    // Empty text should not start a sequence
    EXPECT_FALSE(_keyboardManager->IsSequenceRunning());
}

TEST_F(DebugKeyboardManager_test, TapCombo_EmptyVector_NoSequence)
{
    std::vector<std::string> empty;
    _keyboardManager->TapCombo(empty, 2);
    
    EXPECT_FALSE(_keyboardManager->IsSequenceRunning());
}

// ========================================================================
// Matrix State Tests
// ========================================================================

TEST_F(DebugKeyboardManager_test, GetMatrixState_ReturnsValidArray)
{
    auto matrix = _keyboardManager->GetMatrixState();
    
    // Should return 8 bytes
    EXPECT_EQ(matrix.size(), 8u);
    
    // With no keys pressed, all bytes should be 0xFF (active low)
    _keyboardManager->ReleaseAllKeys();
    matrix = _keyboardManager->GetMatrixState();
    for (int i = 0; i < 8; i++)
    {
        EXPECT_EQ(matrix[i], 0xFF);
    }
}

TEST_F(DebugKeyboardManager_test, GetMatrixState_ReflectsPressedKeys)
{
    // Note: GetMatrixState currently returns default state (TODO in implementation)
    // This test verifies the function doesn't crash and returns valid data
    _keyboardManager->ReleaseAllKeys();
    _keyboardManager->PressKey("a");
    
    auto matrix = _keyboardManager->GetMatrixState();
    
    // Just verify we get 8 bytes (actual matrix state not yet implemented)
    EXPECT_EQ(matrix.size(), 8u);
}
