#pragma once

#include "stdafx.h"
#include "emulator/io/keyboard/keyboard.h"

#include <vector>
#include <queue>
#include <string>
#include <map>
#include <set>
#include <optional>

class EmulatorContext;
class Keyboard;

/// region <Types>

/// Single keyboard event in a sequence
struct KeyboardSequenceEvent
{
    /// Action types for keyboard events
    enum class Action : uint8_t
    {
        PRESS,          ///< Press key(s), keep held. frames = wait after pressing
        RELEASE,        ///< Release key(s). frames = wait after releasing
        TAP,            ///< Press + hold + auto-release. frames = hold duration
        COMBO_PRESS,    ///< Press multiple keys simultaneously. frames = wait after pressing
        COMBO_RELEASE,  ///< Release multiple keys. frames = wait after releasing
        COMBO_TAP,      ///< Press multiple + hold + release. frames = hold duration
        WAIT,           ///< Pause for N frames (no key action)
        RELEASE_ALL     ///< Release all currently pressed keys
    };

    Action action = Action::TAP;
    std::vector<ZXKeysEnum> keys;   ///< One or more keys
    uint16_t frames = 2;            ///< Duration/wait meaning depends on action
    
    // Convenience constructors
    KeyboardSequenceEvent() = default;
    KeyboardSequenceEvent(Action a, std::vector<ZXKeysEnum> k, uint16_t f = 2)
        : action(a), keys(std::move(k)), frames(f) {}
    KeyboardSequenceEvent(Action a, ZXKeysEnum k, uint16_t f = 2)
        : action(a), keys({k}), frames(f) {}
};

/// A sequence of keyboard events with timing
struct KeyboardSequence
{
    std::string name;                               ///< Sequence identifier
    std::vector<KeyboardSequenceEvent> events;      ///< Events to execute
    uint16_t defaultGapFrames = 2;                  ///< Default gap between events (if not specified)
    
    KeyboardSequence() = default;
    KeyboardSequence(const std::string& n, std::vector<KeyboardSequenceEvent> e, uint16_t gap = 2)
        : name(n), events(std::move(e)), defaultGapFrames(gap) {}
};

/// endregion </Types>


/// DebugKeyboardManager: High-level orchestrator for keyboard injection
/// 
/// Provides three levels of abstraction:
/// 1. Single key operations: PressKey, ReleaseKey, TapKey
/// 2. Modifier+key combos: PressCombo, TapCombo (e.g., CAPS+5 for LEFT)
/// 3. Event sequences: Complex multi-event sequences with timing
/// 
/// Integrates with emulator frame loop via OnFrame() for timing-based release.
/// 
/// @see http://slady.net/Sinclair-ZX-Spectrum-keyboard/ for keyboard reference
class DebugKeyboardManager
{
    /// region <Constants>
public:
    /// Default hold duration for tapped keys (frames)
    static constexpr uint16_t DEFAULT_HOLD_FRAMES = 2;
    
    /// Default wait between sequence events (frames)
    static constexpr uint16_t DEFAULT_WAIT_FRAMES = 2;
    
    /// endregion </Constants>
    
    /// region <Fields>
private:
    EmulatorContext* _context = nullptr;
    Keyboard* _keyboard = nullptr;
    
    /// Pending events queue for sequence execution
    std::queue<KeyboardSequenceEvent> _eventQueue;
    
    /// Frame countdown for current event timing
    uint16_t _frameCountdown = 0;
    
    /// Keys currently held by pending tap operations (sequence based)
    std::vector<ZXKeysEnum> _tapHeldKeys;
    
    /// Keys directly pressed via PressKey (not via sequences)
    std::set<ZXKeysEnum> _directPressedKeys;
    
    /// Whether we're in the "hold" phase of a tap (waiting to release)
    bool _inTapHoldPhase = false;
    
    /// Sequence currently being executed
    std::optional<std::string> _currentSequenceName;
    
    /// Predefined macro library
    static std::map<std::string, KeyboardSequence> _macroLibrary;
    static bool _macrosInitialized;
    
    /// String-to-key mapping for string-based API
    static std::map<std::string, ZXKeysEnum> _keyNameMap;
    static bool _keyNamesInitialized;
    
    /// endregion </Fields>
    
    /// region <Constructors / Destructors>
public:
    DebugKeyboardManager() = delete;
    explicit DebugKeyboardManager(EmulatorContext* context);
    ~DebugKeyboardManager();
    
    /// endregion </Constructors / Destructors>
    
    /// region <Single Key Operations>
public:
    /// Press a single key (stays pressed until released)
    /// @param key Key to press
    void PressKey(ZXKeysEnum key);
    void PressKey(const std::string& keyName);
    
    /// Release a specific key
    /// @param key Key to release
    void ReleaseKey(ZXKeysEnum key);
    void ReleaseKey(const std::string& keyName);
    
    /// Tap a key (press, hold for N frames, then auto-release)
    /// @param key Key to tap
    /// @param holdFrames How many frames to hold (default: 2)
    void TapKey(ZXKeysEnum key, uint16_t holdFrames = DEFAULT_HOLD_FRAMES);
    void TapKey(const std::string& keyName, uint16_t holdFrames = DEFAULT_HOLD_FRAMES);
    
    /// Release all currently pressed keys
    void ReleaseAllKeys();
    
    /// endregion </Single Key Operations>
    
    /// region <Modifier + Key Combo Operations>
public:
    /// Press multiple keys simultaneously (e.g., CAPS+5 for LEFT arrow)
    /// @param keys Keys to press
    void PressCombo(const std::vector<ZXKeysEnum>& keys);
    void PressCombo(const std::vector<std::string>& keyNames);
    
    /// Release multiple keys simultaneously
    /// @param keys Keys to release
    void ReleaseCombo(const std::vector<ZXKeysEnum>& keys);
    void ReleaseCombo(const std::vector<std::string>& keyNames);
    
    /// Tap a combo (press all, hold for N frames, then release all)
    /// @param keys Keys to tap simultaneously
    /// @param holdFrames How many frames to hold (default: 2)
    void TapCombo(const std::vector<ZXKeysEnum>& keys, uint16_t holdFrames = DEFAULT_HOLD_FRAMES);
    void TapCombo(const std::vector<std::string>& keyNames, uint16_t holdFrames = DEFAULT_HOLD_FRAMES);
    
    /// endregion </Modifier + Key Combo Operations>
    
    /// region <Sequence Operations>
public:
    /// Execute a sequence of events with timing
    /// @param sequence Sequence to execute
    void ExecuteSequence(const KeyboardSequence& sequence);
    
    /// Execute a named predefined sequence from macro library
    /// @param name Macro name (e.g., "e_mode", "format", "cat", "break")
    /// @return true if macro exists and was queued
    bool ExecuteNamedSequence(const std::string& name);
    
    /// Queue a sequence for asynchronous execution
    /// @param sequence Sequence to queue
    void QueueSequence(const KeyboardSequence& sequence);
    
    /// Check if any sequence is currently executing
    /// @return true if sequence in progress
    bool IsSequenceRunning() const;
    
    /// Abort current sequence execution
    void AbortSequence();
    
    /// Get name of currently running sequence (if any)
    /// @return Sequence name or empty string
    std::string GetCurrentSequenceName() const;
    
    /// endregion </Sequence Operations>
    
    /// region <High-Level Helpers>
public:
    /// Enter Extended mode (CAPS + SYMBOL SHIFT)
    /// Produces E cursor in ZX Spectrum
    void EnterExtendedMode();
    
    /// Enter Graphics mode (CAPS + 9)
    void EnterGraphicsMode();
    
    /// Type a TR-DOS keyword (handles E-mode entry automatically)
    /// @param keyword Keyword name (e.g., "FORMAT", "CAT", "ERASE")
    /// @return true if keyword recognized and queued
    bool TypeTRDOSKeyword(const std::string& keyword);
    
    /// Type a complete TR-DOS command with argument
    /// e.g., TypeTRDOSCommand("FORMAT", "test") produces FORMAT"test"
    /// @param keyword TR-DOS keyword
    /// @param argument Command argument (disk name, file name, etc.)
    void TypeTRDOSCommand(const std::string& keyword, const std::string& argument);
    
    /// Type text string with automatic modifier handling
    /// Handles uppercase (CAPS+letter) and symbols (SYM+key)
    /// @param text ASCII text to type
    /// @param charDelayFrames Frames between each character
    void TypeText(const std::string& text, uint16_t charDelayFrames = DEFAULT_WAIT_FRAMES);
    
    /// Type a complete 48K BASIC command with automatic keyword + literal handling
    /// Format: "PRINT \"hello\"" - first token is keyword, rest is literal
    /// The first character triggers K-mode keyword, quotes trigger L-mode for literals
    /// @param command Complete BASIC command (e.g., "PRINT \"hello\"", "10 LET a=5")
    /// @param charDelayFrames Frames between each character
    void TypeBasicCommand(const std::string& command, uint16_t charDelayFrames = DEFAULT_WAIT_FRAMES);
    
    /// endregion </High-Level Helpers>
    
    /// region <State Queries>
public:
    /// Check if specific key is currently pressed
    /// @param key Key to check
    /// @return true if pressed
    bool IsKeyPressed(ZXKeysEnum key) const;
    
    /// Get list of all currently pressed keys
    /// @return Vector of pressed keys
    std::vector<ZXKeysEnum> GetPressedKeys() const;
    
    /// Get raw matrix state (for debugging)
    /// @return Array of 8 bytes representing keyboard matrix
    std::array<uint8_t, 8> GetMatrixState() const;
    
    /// endregion </State Queries>
    
    /// region <Key Name Resolution>
public:
    /// Convert string key name to enum
    /// Supports: "a"-"z", "0"-"9", "caps", "symbol", "enter", "space",
    ///           "left", "right", "up", "down", etc.
    /// @param name Key name (case-insensitive)
    /// @return ZXKeysEnum value, or ZXKEY_NONE if not found
    static ZXKeysEnum ResolveKeyName(const std::string& name);
    
    /// Convert enum to display name
    /// @param key Key enum
    /// @return Human-readable key name
    static std::string GetKeyDisplayName(ZXKeysEnum key);
    
    /// Get list of all recognized key names
    /// @return Vector of key name strings
    static std::vector<std::string> GetAllKeyNames();
    
    /// endregion </Key Name Resolution>
    
    /// region <Frame Processing>
public:
    /// Called every frame by emulator to process pending events
    /// Must be called from EmulatorContext frame loop
    void OnFrame();
    
    /// endregion </Frame Processing>
    
    /// region <Private Methods>
private:
    /// Initialize predefined macro library
    static void InitializeMacroLibrary();
    
    /// Initialize key name mapping
    static void InitializeKeyNameMap();
    
    /// Process the next event from queue
    void ProcessNextEvent();
    
    /// Execute a single event immediately
    void ExecuteEvent(const KeyboardSequenceEvent& event);
    
    /// Get TR-DOS E-mode key for a keyword
    /// @param keyword Keyword name (uppercase)
    /// @return Key to press in E-mode, or ZXKEY_NONE if not recognized
    static ZXKeysEnum GetTRDOSKeywordKey(const std::string& keyword);
    
    /// Convert ASCII character to key sequence
    /// @param c ASCII character
    /// @return Keys needed to produce character (may be empty, single, or modifier+key)
    static std::vector<ZXKeysEnum> CharToKeys(char c);
    
    /// endregion </Private Methods>
};
