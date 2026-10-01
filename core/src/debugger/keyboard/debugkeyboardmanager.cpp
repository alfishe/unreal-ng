#include "debugkeyboardmanager.h"
#include "debugger/ttd/timetravelmanager.h"  // TimeTravelManager (input journal capture, Phase 2 Item 3)
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"

#include <algorithm>
#include <cctype>

/// region <Static Member Initialization>

std::map<std::string, KeyboardSequence> DebugKeyboardManager::_macroLibrary;
bool DebugKeyboardManager::_macrosInitialized = false;

std::map<std::string, ZXKeysEnum> DebugKeyboardManager::_keyNameMap;
bool DebugKeyboardManager::_keyNamesInitialized = false;

/// endregion </Static Member Initialization>


/// region <Constructors / Destructors>

DebugKeyboardManager::DebugKeyboardManager(EmulatorContext* context)
    : _context(context)
{
    if (_context)
    {
        _keyboard = _context->pKeyboard;
    }
    
    // Initialize static maps on first instance
    if (!_macrosInitialized)
    {
        InitializeMacroLibrary();
    }
    
    if (!_keyNamesInitialized)
    {
        InitializeKeyNameMap();
    }
}

DebugKeyboardManager::~DebugKeyboardManager()
{
    // Release any held keys on destruction
    ReleaseAllKeys();
}

/// endregion </Constructors / Destructors>


/// region <Journalled matrix mutation>

/// Live input is refused while the TTD journal owns input: a replay, or the machine
/// re-executing recorded history (TimeTravelManager::OwnsInput)
bool DebugKeyboardManager::IsInputOwnedByJournal() const
{
    // An RZX playback owns input as well (its recording answers every IN)
    if (_context && _context->rzxPlayer)
        return true;
    return _context && _context->pTimeTravelManager && _context->pTimeTravelManager->OwnsInput();
}

/// The one place every automation path changes the matrix. With TTD present the change
/// goes through TimeTravelManager::SubmitLiveInput: refused while the journal owns input,
/// applied on the machine's thread at an instruction boundary and journalled there while
/// recording. Timed operations (tap, combo, type, sequences) used to call Keyboard
/// directly and were missing from the journal.
bool DebugKeyboardManager::ApplyPcKey(PcKey key, bool pressed)
{
    if (key == PcKey::None || !_keyboard || !_keyboard->HasPs2Sink())
        return false;

    if (_context && _context->pTimeTravelManager)
    {
        ttd::TTDInputEvent ev;
        ev.kind = ttd::TTDInputKind::PcKey;
        ev.key = static_cast<uint8_t>(key);
        ev.pressed = pressed;
        return _context->pTimeTravelManager->SubmitLiveInput(ev);
    }

    _keyboard->ApplyPcKey(key, pressed);
    return true;
}

bool DebugKeyboardManager::ApplyKey(ZXKeysEnum key, bool pressed, bool derivePcKeys)
{
    if (key == ZXKEY_NONE || !_keyboard)
        return false;

    // The PS/2 controller hears the PC keys the ZX key stands for (the ZX key as
    // given, before it is decomposed into matrix keys: Up is E0 75, not Shift + 7)
    if (derivePcKeys && _keyboard->HasPs2Sink())
    {
        const std::vector<PcKey> pcKeys = pckey::FromZxKey(key);
        if (pressed)
        {
            for (PcKey pc : pcKeys)
                ApplyPcKey(pc, true);
        }
        else
        {
            for (auto it = pcKeys.rbegin(); it != pcKeys.rend(); ++it)
                ApplyPcKey(*it, false);
        }
    }

    if (!pressed)
    {
        const ZXKeysEnum matrixKey = _keyboard->GetMatrixKey(key);
        if (matrixKey != ZXKEY_NONE && matrixKey != ZXKEY_CAPS_SHIFT && matrixKey != ZXKEY_SYM_SHIFT)
            _lastReleaseFrame[matrixKey] = _frameIndex;
    }

    if (_context && _context->pTimeTravelManager)
    {
        ttd::TTDInputEvent ev;
        ev.kind = ttd::TTDInputKind::Key;
        ev.key = static_cast<uint8_t>(key);
        ev.pressed = pressed;
        return _context->pTimeTravelManager->SubmitLiveInput(ev);
    }

    if (pressed)
        _keyboard->PressKey(key);
    else
        _keyboard->ReleaseKey(key);
    return true;
}

/// endregion </Journalled matrix mutation>

/// region <Single Key Operations>

void DebugKeyboardManager::PressKey(ZXKeysEnum key)
{
    if (key == ZXKEY_NONE)
        return;

    // TTD input ownership (parent TDD §8.2 + Appendix C) and journal capture
    // (Phase 2 Item 3) both live in ApplyKey. Live input must not mutate the
    // matrix while the journal drives it.
    if (IsInputOwnedByJournal())
        return;

    // Track in our direct press set
    _directPressedKeys.insert(key);

    ApplyKey(key, /*pressed=*/true);
}

void DebugKeyboardManager::PressKey(const std::string& keyName)
{
    const ZXKeysEnum key = ResolveKeyName(keyName);
    const PcKey pcKey = key == ZXKEY_NONE ? pckey::FromName(keyName) : PcKey::None;
    if (pcKey != PcKey::None)
    {
        // A PC key with no ZX name (F1, Home, ...): only the PS/2 controller hears it
        if (IsInputOwnedByJournal())
            return;
        _directPressedPcKeys.insert(pcKey);
        ApplyPcKey(pcKey, /*pressed=*/true);
        return;
    }
    PressKey(key);
}

void DebugKeyboardManager::ReleaseKey(ZXKeysEnum key)
{
    if (key == ZXKEY_NONE)
        return;

    // TTD input ownership + journal capture: see ApplyKey
    if (IsInputOwnedByJournal())
        return;

    // Remove from our direct press set
    _directPressedKeys.erase(key);

    ApplyKey(key, /*pressed=*/false);
}

void DebugKeyboardManager::ReleaseKey(const std::string& keyName)
{
    const ZXKeysEnum key = ResolveKeyName(keyName);
    const PcKey pcKey = key == ZXKEY_NONE ? pckey::FromName(keyName) : PcKey::None;
    if (pcKey != PcKey::None)
    {
        if (IsInputOwnedByJournal())
            return;
        _directPressedPcKeys.erase(pcKey);
        ApplyPcKey(pcKey, /*pressed=*/false);
        return;
    }
    ReleaseKey(key);
}

void DebugKeyboardManager::TapKey(ZXKeysEnum key, uint16_t holdFrames)
{
    if (key == ZXKEY_NONE)
        return;
    
    // Queue as a sequence with TAP action
    KeyboardSequence seq;
    seq.name = "tap_single";
    seq.events.push_back({KeyboardSequenceEvent::Action::TAP, std::vector<ZXKeysEnum>{key}, holdFrames});
    
    QueueSequence(seq);
}

void DebugKeyboardManager::TapKey(const std::string& keyName, uint16_t holdFrames)
{
    const ZXKeysEnum key = ResolveKeyName(keyName);
    const PcKey pcKey = key == ZXKEY_NONE ? pckey::FromName(keyName) : PcKey::None;
    if (pcKey != PcKey::None)
    {
        KeyboardSequence seq;
        seq.name = "tap_single";
        KeyboardSequenceEvent event(KeyboardSequenceEvent::Action::TAP, std::vector<ZXKeysEnum>{}, holdFrames);
        event.pcKeys = {pcKey};
        seq.events.push_back(event);
        QueueSequence(seq);
        return;
    }
    TapKey(key, holdFrames);
}

void DebugKeyboardManager::ReleaseAllKeys()
{
    std::lock_guard<std::recursive_mutex> lock(_sequenceMutex);

    // Clear direct pressed keys (the reset below releases them on the PS/2 side too)
    _directPressedKeys.clear();
    _directPressedPcKeys.clear();
    
    // Release tap-held keys
    for (ZXKeysEnum key : _tapHeldKeys)
    {
        ApplyKey(key, /*pressed=*/false, _tapDerivesPcKeys);
    }
    _tapHeldKeys.clear();
    for (auto it = _tapHeldPcKeys.rbegin(); it != _tapHeldPcKeys.rend(); ++it)
        ApplyPcKey(*it, /*pressed=*/false);
    _tapHeldPcKeys.clear();
    _inTapHoldPhase = false;
    
    // Whole-matrix reset (also clears host press counters), journalled as one event so a
    // replay reaches the same all-released matrix
    if (_keyboard)
    {
        if (_context && _context->pTimeTravelManager)
        {
            ttd::TTDInputEvent ev;
            ev.kind = ttd::TTDInputKind::KeyboardReset;
            _context->pTimeTravelManager->SubmitLiveInput(ev);
        }
        else
        {
            _keyboard->Reset();
            _keyboard->ReleaseAllPcKeys();
        }
    }
}

/// endregion </Single Key Operations>


/// region <Modifier + Key Combo Operations>

void DebugKeyboardManager::PressCombo(const std::vector<ZXKeysEnum>& keys)
{
    if (!_keyboard)
        return;
    
    for (ZXKeysEnum key : keys)
    {
        ApplyKey(key, /*pressed=*/true);
    }
}

void DebugKeyboardManager::PressCombo(const std::vector<std::string>& keyNames)
{
    // Split by name: a name that is not a ZX key (e.g. "f12", "rshift" - no ZX
    // Spectrum matrix equivalent at all) falls back to a pure PC key, exactly
    // like the single-key PressKey(string) overload already does. Without
    // this split, a PC-only name resolved to ZXKEY_NONE and ApplyKey silently
    // dropped it - a combo like {"rshift", "f12"} (TS-Conf's BIOS Setup entry)
    // pressed nothing at all.
    std::vector<ZXKeysEnum> keys;
    std::vector<PcKey> pcKeys;
    keys.reserve(keyNames.size());
    for (const auto& name : keyNames)
    {
        const ZXKeysEnum key = ResolveKeyName(name);
        if (key != ZXKEY_NONE)
        {
            keys.push_back(key);
            continue;
        }
        const PcKey pcKey = pckey::FromName(name);
        if (pcKey != PcKey::None)
            pcKeys.push_back(pcKey);
    }
    PressCombo(keys);
    for (PcKey pcKey : pcKeys)
        ApplyPcKey(pcKey, /*pressed=*/true);
}

void DebugKeyboardManager::ReleaseCombo(const std::vector<ZXKeysEnum>& keys)
{
    if (!_keyboard)
        return;
    
    // Release in reverse order (modifier last)
    for (auto it = keys.rbegin(); it != keys.rend(); ++it)
    {
        ApplyKey(*it, /*pressed=*/false);
    }
}

void DebugKeyboardManager::ReleaseCombo(const std::vector<std::string>& keyNames)
{
    // Same ZX/PC split as PressCombo(vector<string>); PC keys release first,
    // in reverse name order, mirroring "modifier last" for the ZX half below
    std::vector<ZXKeysEnum> keys;
    std::vector<PcKey> pcKeys;
    keys.reserve(keyNames.size());
    for (const auto& name : keyNames)
    {
        const ZXKeysEnum key = ResolveKeyName(name);
        if (key != ZXKEY_NONE)
        {
            keys.push_back(key);
            continue;
        }
        const PcKey pcKey = pckey::FromName(name);
        if (pcKey != PcKey::None)
            pcKeys.push_back(pcKey);
    }
    for (auto it = pcKeys.rbegin(); it != pcKeys.rend(); ++it)
        ApplyPcKey(*it, /*pressed=*/false);
    ReleaseCombo(keys);
}

void DebugKeyboardManager::TapCombo(const std::vector<ZXKeysEnum>& keys, uint16_t holdFrames)
{
    if (keys.empty())
        return;
    
    KeyboardSequence seq;
    seq.name = "tap_combo";
    seq.events.push_back({KeyboardSequenceEvent::Action::COMBO_TAP, keys, holdFrames});
    
    QueueSequence(seq);
}

void DebugKeyboardManager::TapCombo(const std::vector<std::string>& keyNames, uint16_t holdFrames)
{
    // Same ZX/PC split as PressCombo(vector<string>). A combo made entirely of
    // PC-only names (e.g. {"rshift", "f12"} - TS-Conf's BIOS Setup entry) still
    // queues a single COMBO_TAP event with an empty ZX key list and a
    // populated pcKeys list; ExecuteEvent/OnFrame already presses, holds and
    // releases pcKeys exactly like the ZX half (see pressPcKeys() and the
    // reverse-order release in OnFrame)
    std::vector<ZXKeysEnum> keys;
    std::vector<PcKey> pcKeys;
    keys.reserve(keyNames.size());
    for (const auto& name : keyNames)
    {
        const ZXKeysEnum key = ResolveKeyName(name);
        if (key != ZXKEY_NONE)
        {
            keys.push_back(key);
            continue;
        }
        const PcKey pcKey = pckey::FromName(name);
        if (pcKey != PcKey::None)
            pcKeys.push_back(pcKey);
    }

    if (keys.empty() && pcKeys.empty())
        return;

    KeyboardSequence seq;
    seq.name = "tap_combo";
    KeyboardSequenceEvent event(KeyboardSequenceEvent::Action::COMBO_TAP, keys, holdFrames);
    event.pcKeys = std::move(pcKeys);
    seq.events.push_back(std::move(event));

    QueueSequence(seq);
}

/// endregion </Modifier + Key Combo Operations>


/// region <Sequence Operations>

void DebugKeyboardManager::ExecuteSequence(const KeyboardSequence& sequence)
{
    // Appends like QueueSequence: dropping pending input here made a `type`
    // silently cancel a `tap` sent just before it
    QueueSequence(sequence);
}

bool DebugKeyboardManager::ExecuteNamedSequence(const std::string& name)
{
    auto it = _macroLibrary.find(name);
    if (it != _macroLibrary.end())
    {
        ExecuteSequence(it->second);
        return true;
    }
    return false;
}

void DebugKeyboardManager::QueueSequence(const KeyboardSequence& sequence)
{
    // Automation thread queues, the emulator thread consumes in OnFrame
    std::lock_guard<std::recursive_mutex> lock(_sequenceMutex);

    // Add events to existing queue
    if (!_currentSequenceName.has_value())
    {
        _currentSequenceName = sequence.name;
    }
    
    for (const auto& event : sequence.events)
    {
        _eventQueue.push(event);
    }
    
    // Start processing if not already running
    if (_frameCountdown == 0 && !_inTapHoldPhase && !_eventQueue.empty())
    {
        ProcessNextEvent();
    }
}

bool DebugKeyboardManager::IsSequenceRunning() const
{
    std::lock_guard<std::recursive_mutex> lock(_sequenceMutex);
    return !_eventQueue.empty() || _inTapHoldPhase || _frameCountdown > 0;
}

void DebugKeyboardManager::AbortSequence()
{
    std::lock_guard<std::recursive_mutex> lock(_sequenceMutex);

    // Clear queue
    while (!_eventQueue.empty())
    {
        _eventQueue.pop();
    }
    
    // Release any held keys
    for (ZXKeysEnum key : _tapHeldKeys)
    {
        ApplyKey(key, /*pressed=*/false, _tapDerivesPcKeys);
    }
    _tapHeldKeys.clear();
    for (auto it = _tapHeldPcKeys.rbegin(); it != _tapHeldPcKeys.rend(); ++it)
        ApplyPcKey(*it, /*pressed=*/false);
    _tapHeldPcKeys.clear();
    
    _frameCountdown = 0;
    _inTapHoldPhase = false;
    _currentSequenceName.reset();
}

std::string DebugKeyboardManager::GetCurrentSequenceName() const
{
    std::lock_guard<std::recursive_mutex> lock(_sequenceMutex);
    return _currentSequenceName.value_or("");
}

/// endregion </Sequence Operations>


/// region <High-Level Helpers>

void DebugKeyboardManager::EnterExtendedMode()
{
    ExecuteNamedSequence("e_mode");
}

void DebugKeyboardManager::EnterGraphicsMode()
{
    ExecuteNamedSequence("g_mode");
}

bool DebugKeyboardManager::TypeTRDOSKeyword(const std::string& keyword)
{
    // Convert to uppercase
    std::string upper = keyword;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
    
    ZXKeysEnum keywordKey = GetTRDOSKeywordKey(upper);
    if (keywordKey == ZXKEY_NONE)
    {
        return false;
    }
    
    // Build sequence: E-mode + keyword key
    KeyboardSequence seq;
    seq.name = "trdos_" + keyword;
    seq.events = {
        // Enter E-mode: CAPS+SYMBOL
        {KeyboardSequenceEvent::Action::COMBO_TAP, {ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT}, DEFAULT_HOLD_FRAMES},
        {KeyboardSequenceEvent::Action::WAIT, {}, DEFAULT_WAIT_FRAMES},
        // Press keyword key
        {KeyboardSequenceEvent::Action::TAP, std::vector<ZXKeysEnum>{keywordKey}, DEFAULT_HOLD_FRAMES}
    };
    
    ExecuteSequence(seq);
    return true;
}

void DebugKeyboardManager::TypeTRDOSCommand(const std::string& keyword, const std::string& argument)
{
    KeyboardSequence seq;
    seq.name = "trdos_command";
    
    // 1. Enter E-mode and type keyword
    ZXKeysEnum keywordKey = GetTRDOSKeywordKey(keyword);
    if (keywordKey != ZXKEY_NONE)
    {
        // E-mode entry
        seq.events.push_back({KeyboardSequenceEvent::Action::COMBO_TAP, 
                              {ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT}, DEFAULT_HOLD_FRAMES});
        seq.events.push_back({KeyboardSequenceEvent::Action::WAIT, {}, DEFAULT_WAIT_FRAMES});
        
        // Keyword key
        seq.events.push_back({KeyboardSequenceEvent::Action::TAP, std::vector<ZXKeysEnum>{keywordKey}, DEFAULT_HOLD_FRAMES});
        seq.events.push_back({KeyboardSequenceEvent::Action::WAIT, {}, DEFAULT_WAIT_FRAMES});
    }
    
    // 2. Opening quote: SYMBOL+P
    seq.events.push_back({KeyboardSequenceEvent::Action::COMBO_TAP, 
                          {ZXKEY_SYM_SHIFT, ZXKEY_P}, DEFAULT_HOLD_FRAMES});
    seq.events.push_back({KeyboardSequenceEvent::Action::WAIT, {}, DEFAULT_WAIT_FRAMES});
    
    // 3. Type argument characters
    for (char c : argument)
    {
        std::vector<ZXKeysEnum> keys = CharToKeys(c);
        if (!keys.empty())
        {
            if (keys.size() == 1)
            {
                seq.events.push_back({KeyboardSequenceEvent::Action::TAP, keys, DEFAULT_HOLD_FRAMES});
            }
            else
            {
                seq.events.push_back({KeyboardSequenceEvent::Action::COMBO_TAP, keys, DEFAULT_HOLD_FRAMES});
            }
        }
    }
    seq.events.push_back({KeyboardSequenceEvent::Action::WAIT, {}, DEFAULT_WAIT_FRAMES});
    
    // 4. Closing quote: SYMBOL+P
    seq.events.push_back({KeyboardSequenceEvent::Action::COMBO_TAP, 
                          {ZXKEY_SYM_SHIFT, ZXKEY_P}, DEFAULT_HOLD_FRAMES});
    
    ExecuteSequence(seq);
}

void DebugKeyboardManager::TypeText(const std::string& text, uint16_t charDelayFrames)
{
    KeyboardSequence seq;
    seq.name = "type_text";

    // A PS/2 keyboard (ZX-Evo) gets the PC keys that type each character, not
    // the ones behind its ZX key combination ('&' is Symbol Shift + 6 on the ZX,
    // Shift + 7 on a PC)
    const bool ps2 = _keyboard && _keyboard->HasPs2Sink();
    
    for (char c : text)
    {
        std::vector<ZXKeysEnum> keys = CharToKeys(c);
        std::vector<PcKey> pcKeys = ps2 ? pckey::FromCharacter(c) : std::vector<PcKey>{};
        if (!keys.empty() || !pcKeys.empty())
        {
            const auto action = keys.size() > 1 || pcKeys.size() > 1 ? KeyboardSequenceEvent::Action::COMBO_TAP
                                                                      : KeyboardSequenceEvent::Action::TAP;
            KeyboardSequenceEvent event(action, keys, DEFAULT_HOLD_FRAMES);
            event.pcKeys = std::move(pcKeys);
            seq.events.push_back(event);
            
            if (charDelayFrames > 0)
            {
                seq.events.push_back({KeyboardSequenceEvent::Action::WAIT, {}, charDelayFrames});
            }
        }
    }
    
    ExecuteSequence(seq);
}

/// endregion </High-Level Helpers>


/// region <State Queries>

bool DebugKeyboardManager::IsKeyPressed(ZXKeysEnum key) const
{
    // Check in directly pressed keys
    if (_directPressedKeys.count(key) > 0)
        return true;
    
    // Check in tap held keys (sequence-based)
    auto it = std::find(_tapHeldKeys.begin(), _tapHeldKeys.end(), key);
    return it != _tapHeldKeys.end();
}

std::vector<ZXKeysEnum> DebugKeyboardManager::GetPressedKeys() const
{
    // Combine direct pressed keys and tap held keys
    std::vector<ZXKeysEnum> result;
    result.reserve(_directPressedKeys.size() + _tapHeldKeys.size());
    
    // Add direct pressed keys
    for (ZXKeysEnum key : _directPressedKeys)
    {
        result.push_back(key);
    }
    
    // Add tap held keys (avoid duplicates)
    for (ZXKeysEnum key : _tapHeldKeys)
    {
        if (_directPressedKeys.count(key) == 0)
        {
            result.push_back(key);
        }
    }
    
    return result;
}

std::array<uint8_t, 8> DebugKeyboardManager::GetMatrixState() const
{
    std::array<uint8_t, 8> state = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    // TODO: Access keyboard matrix state through _keyboard
    // For now return default (all keys released)
    
    return state;
}

/// endregion </State Queries>


/// region <Key Name Resolution>

bool DebugKeyboardManager::IsKnownKeyName(const std::string& name)
{
    return ResolveKeyName(name) != ZXKEY_NONE || pckey::FromName(name) != PcKey::None;
}

ZXKeysEnum DebugKeyboardManager::ResolveKeyName(const std::string& name)
{
    if (!_keyNamesInitialized)
    {
        InitializeKeyNameMap();
    }
    
    // Convert to lowercase for lookup
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    auto it = _keyNameMap.find(lower);
    if (it != _keyNameMap.end())
    {
        return it->second;
    }
    
    return ZXKEY_NONE;
}

std::string DebugKeyboardManager::GetKeyDisplayName(ZXKeysEnum key)
{
    switch (key)
    {
        case ZXKEY_NONE: return "NONE";
        case ZXKEY_CAPS_SHIFT: return "CAPS";
        case ZXKEY_SYM_SHIFT: return "SYMBOL";
        case ZXKEY_ENTER: return "ENTER";
        case ZXKEY_SPACE: return "SPACE";
        case ZXKEY_0: return "0";
        case ZXKEY_1: return "1";
        case ZXKEY_2: return "2";
        case ZXKEY_3: return "3";
        case ZXKEY_4: return "4";
        case ZXKEY_5: return "5";
        case ZXKEY_6: return "6";
        case ZXKEY_7: return "7";
        case ZXKEY_8: return "8";
        case ZXKEY_9: return "9";
        case ZXKEY_A: return "A";
        case ZXKEY_B: return "B";
        case ZXKEY_C: return "C";
        case ZXKEY_D: return "D";
        case ZXKEY_E: return "E";
        case ZXKEY_F: return "F";
        case ZXKEY_G: return "G";
        case ZXKEY_H: return "H";
        case ZXKEY_I: return "I";
        case ZXKEY_J: return "J";
        case ZXKEY_K: return "K";
        case ZXKEY_L: return "L";
        case ZXKEY_M: return "M";
        case ZXKEY_N: return "N";
        case ZXKEY_O: return "O";
        case ZXKEY_P: return "P";
        case ZXKEY_Q: return "Q";
        case ZXKEY_R: return "R";
        case ZXKEY_S: return "S";
        case ZXKEY_T: return "T";
        case ZXKEY_U: return "U";
        case ZXKEY_V: return "V";
        case ZXKEY_W: return "W";
        case ZXKEY_X: return "X";
        case ZXKEY_Y: return "Y";
        case ZXKEY_Z: return "Z";
        case ZXKEY_EXT_UP: return "UP";
        case ZXKEY_EXT_DOWN: return "DOWN";
        case ZXKEY_EXT_LEFT: return "LEFT";
        case ZXKEY_EXT_RIGHT: return "RIGHT";
        case ZXKEY_EXT_DELETE: return "DELETE";
        case ZXKEY_EXT_BREAK: return "BREAK";
        default: return "UNKNOWN";
    }
}

std::vector<std::string> DebugKeyboardManager::GetAllKeyNames()
{
    if (!_keyNamesInitialized)
    {
        InitializeKeyNameMap();
    }
    
    std::vector<std::string> names;
    names.reserve(_keyNameMap.size());
    for (const auto& pair : _keyNameMap)
    {
        names.push_back(pair.first);
    }

    // PC-only names (no ZX matrix equivalent at all, e.g. "f12", "rshift",
    // "home") are already fully accepted by PressKey/TapKey/PressCombo/etc.
    // (pckey::FromName fallback) but were missing from this discovery list.
    // A name that collides with a ZX name keeps ZX priority in ResolveKeyName,
    // so it is not duplicated here - only genuinely PC-only names are added
    for (const std::string& pcName : pckey::AllNames())
    {
        if (_keyNameMap.find(pcName) == _keyNameMap.end())
            names.push_back(pcName);
    }

    std::sort(names.begin(), names.end());
    return names;
}

/// endregion </Key Name Resolution>


/// region <Frame Processing>

void DebugKeyboardManager::OnFrame()
{
    std::lock_guard<std::recursive_mutex> lock(_sequenceMutex);

    _frameIndex++;

    // Decrement countdown if active
    if (_frameCountdown > 0)
    {
        _frameCountdown--;
        
        // Countdown finished?
        if (_frameCountdown == 0)
        {
            // If we were in tap hold phase, release the keys
            if (_inTapHoldPhase && (!_tapHeldKeys.empty() || !_tapHeldPcKeys.empty()))
            {
                // Release in REVERSE order to avoid ghost keypresses
                // For combo SS+P: release P first, then SS
                // This prevents the lone P from being seen without modifier
                for (auto it = _tapHeldKeys.rbegin(); it != _tapHeldKeys.rend(); ++it)
                {
                    ApplyKey(*it, /*pressed=*/false, _tapDerivesPcKeys);
                }
                _tapHeldKeys.clear();
                for (auto it = _tapHeldPcKeys.rbegin(); it != _tapHeldPcKeys.rend(); ++it)
                    ApplyPcKey(*it, /*pressed=*/false);
                _tapHeldPcKeys.clear();
                _inTapHoldPhase = false;
                
                // Add 1-frame debounce delay after release before next event
                // This ensures the keyboard matrix is scanned with all keys released
                _frameCountdown = 1;
                return;  // Don't process next event this frame
            }
            
            // Process next event in queue
            if (!_eventQueue.empty())
            {
                ProcessNextEvent();
            }
            else
            {
                _currentSequenceName.reset();
            }
        }
    }
}

/// endregion </Frame Processing>


/// region <Private Methods>

void DebugKeyboardManager::InitializeMacroLibrary()
{
    using Action = KeyboardSequenceEvent::Action;
    
    // E-mode: CAPS + SYMBOL SHIFT
    _macroLibrary["e_mode"] = {
        "e_mode",
        {
            {Action::COMBO_TAP, {ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT}, 2},
            {Action::WAIT, {}, 2}
        }
    };
    
    // G-mode (Graphics): CAPS + 9
    _macroLibrary["g_mode"] = {
        "g_mode",
        {
            {Action::COMBO_TAP, {ZXKEY_CAPS_SHIFT, ZXKEY_9}, 2},
            {Action::WAIT, {}, 2}
        }
    };
    
    // FORMAT: E-mode + 0
    _macroLibrary["format"] = {
        "format",
        {
            {Action::COMBO_TAP, {ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT}, 2},
            {Action::WAIT, {}, 2},
            {Action::TAP, std::vector<ZXKeysEnum>{ZXKEY_0}, 2}
        }
    };
    
    // CAT: E-mode + 9
    _macroLibrary["cat"] = {
        "cat",
        {
            {Action::COMBO_TAP, {ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT}, 2},
            {Action::WAIT, {}, 2},
            {Action::TAP, std::vector<ZXKeysEnum>{ZXKEY_9}, 2}
        }
    };
    
    // ERASE: E-mode + 7
    _macroLibrary["erase"] = {
        "erase",
        {
            {Action::COMBO_TAP, {ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT}, 2},
            {Action::WAIT, {}, 2},
            {Action::TAP, std::vector<ZXKeysEnum>{ZXKEY_7}, 2}
        }
    };
    
    // MOVE: E-mode + 6
    _macroLibrary["move"] = {
        "move",
        {
            {Action::COMBO_TAP, {ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT}, 2},
            {Action::WAIT, {}, 2},
            {Action::TAP, std::vector<ZXKeysEnum>{ZXKEY_6}, 2}
        }
    };
    
    // BREAK: CAPS + SPACE
    _macroLibrary["break"] = {
        "break",
        {
            {Action::COMBO_TAP, {ZXKEY_CAPS_SHIFT, ZXKEY_SPACE}, 3}
        }
    };
    
    _macrosInitialized = true;
}

void DebugKeyboardManager::InitializeKeyNameMap()
{
    // Letters
    _keyNameMap["a"] = ZXKEY_A;
    _keyNameMap["b"] = ZXKEY_B;
    _keyNameMap["c"] = ZXKEY_C;
    _keyNameMap["d"] = ZXKEY_D;
    _keyNameMap["e"] = ZXKEY_E;
    _keyNameMap["f"] = ZXKEY_F;
    _keyNameMap["g"] = ZXKEY_G;
    _keyNameMap["h"] = ZXKEY_H;
    _keyNameMap["i"] = ZXKEY_I;
    _keyNameMap["j"] = ZXKEY_J;
    _keyNameMap["k"] = ZXKEY_K;
    _keyNameMap["l"] = ZXKEY_L;
    _keyNameMap["m"] = ZXKEY_M;
    _keyNameMap["n"] = ZXKEY_N;
    _keyNameMap["o"] = ZXKEY_O;
    _keyNameMap["p"] = ZXKEY_P;
    _keyNameMap["q"] = ZXKEY_Q;
    _keyNameMap["r"] = ZXKEY_R;
    _keyNameMap["s"] = ZXKEY_S;
    _keyNameMap["t"] = ZXKEY_T;
    _keyNameMap["u"] = ZXKEY_U;
    _keyNameMap["v"] = ZXKEY_V;
    _keyNameMap["w"] = ZXKEY_W;
    _keyNameMap["x"] = ZXKEY_X;
    _keyNameMap["y"] = ZXKEY_Y;
    _keyNameMap["z"] = ZXKEY_Z;
    
    // Numbers
    _keyNameMap["0"] = ZXKEY_0;
    _keyNameMap["1"] = ZXKEY_1;
    _keyNameMap["2"] = ZXKEY_2;
    _keyNameMap["3"] = ZXKEY_3;
    _keyNameMap["4"] = ZXKEY_4;
    _keyNameMap["5"] = ZXKEY_5;
    _keyNameMap["6"] = ZXKEY_6;
    _keyNameMap["7"] = ZXKEY_7;
    _keyNameMap["8"] = ZXKEY_8;
    _keyNameMap["9"] = ZXKEY_9;
    
    // Modifiers
    _keyNameMap["caps"] = ZXKEY_CAPS_SHIFT;
    _keyNameMap["shift"] = ZXKEY_CAPS_SHIFT;
    _keyNameMap["capsshift"] = ZXKEY_CAPS_SHIFT;
    _keyNameMap["caps_shift"] = ZXKEY_CAPS_SHIFT;
    _keyNameMap["cs"] = ZXKEY_CAPS_SHIFT;  // Shorthand alias
    _keyNameMap["symbol"] = ZXKEY_SYM_SHIFT;
    _keyNameMap["sym"] = ZXKEY_SYM_SHIFT;
    _keyNameMap["symshift"] = ZXKEY_SYM_SHIFT;
    _keyNameMap["sym_shift"] = ZXKEY_SYM_SHIFT;
    _keyNameMap["ss"] = ZXKEY_SYM_SHIFT;   // Shorthand alias
    
    // Special keys
    _keyNameMap["enter"] = ZXKEY_ENTER;
    _keyNameMap["return"] = ZXKEY_ENTER;
    _keyNameMap["space"] = ZXKEY_SPACE;
    _keyNameMap[" "] = ZXKEY_SPACE;
    
    // Extended keys (cursor)
    _keyNameMap["up"] = ZXKEY_EXT_UP;
    _keyNameMap["down"] = ZXKEY_EXT_DOWN;
    _keyNameMap["left"] = ZXKEY_EXT_LEFT;
    _keyNameMap["right"] = ZXKEY_EXT_RIGHT;
    
    // Extended keys (editing)
    _keyNameMap["delete"] = ZXKEY_EXT_DELETE;
    _keyNameMap["backspace"] = ZXKEY_EXT_DELETE;
    _keyNameMap["del"] = ZXKEY_EXT_DELETE;
    _keyNameMap["break"] = ZXKEY_EXT_BREAK;
    _keyNameMap["edit"] = ZXKEY_EXT_EDIT;
    
    // Extended keys (symbols)
    _keyNameMap["."] = ZXKEY_EXT_DOT;
    _keyNameMap["dot"] = ZXKEY_EXT_DOT;
    _keyNameMap[","] = ZXKEY_EXT_COMMA;
    _keyNameMap["comma"] = ZXKEY_EXT_COMMA;
    _keyNameMap["+"] = ZXKEY_EXT_PLUS;
    _keyNameMap["plus"] = ZXKEY_EXT_PLUS;
    _keyNameMap["-"] = ZXKEY_EXT_MINUS;
    _keyNameMap["minus"] = ZXKEY_EXT_MINUS;
    _keyNameMap["*"] = ZXKEY_EXT_MULTIPLY;
    _keyNameMap["multiply"] = ZXKEY_EXT_MULTIPLY;
    _keyNameMap["/"] = ZXKEY_EXT_DIVIDE;
    _keyNameMap["divide"] = ZXKEY_EXT_DIVIDE;
    _keyNameMap["="] = ZXKEY_EXT_EQUAL;
    _keyNameMap["equal"] = ZXKEY_EXT_EQUAL;
    _keyNameMap["equals"] = ZXKEY_EXT_EQUAL;
    _keyNameMap["\""] = ZXKEY_EXT_DBLQUOTE;
    _keyNameMap["quote"] = ZXKEY_EXT_DBLQUOTE;
    _keyNameMap["dblquote"] = ZXKEY_EXT_DBLQUOTE;
    
    _keyNamesInitialized = true;
}

void DebugKeyboardManager::ProcessNextEvent()
{
    if (_eventQueue.empty())
    {
        _currentSequenceName.reset();
        return;
    }
    
    // A key pressed again too soon after its release is not entered again by
    // the ROM: hold the event until the key has been released long enough
    const uint16_t wait = FramesUntilKeysReleasedLongEnough(_eventQueue.front());
    if (wait > 0)
    {
        _frameCountdown = wait;
        return;
    }

    KeyboardSequenceEvent event = _eventQueue.front();
    _eventQueue.pop();
    
    ExecuteEvent(event);
}

uint64_t DebugKeyboardManager::FramesSinceReleased(ZXKeysEnum key) const
{
    std::lock_guard<std::recursive_mutex> lock(_sequenceMutex);
    if (!_keyboard)
        return UINT64_MAX;
    const auto it = _lastReleaseFrame.find(_keyboard->GetMatrixKey(key));
    return it == _lastReleaseFrame.end() ? UINT64_MAX : _frameIndex - it->second;
}

uint16_t DebugKeyboardManager::FramesUntilKeysReleasedLongEnough(const KeyboardSequenceEvent& event) const
{
    using Action = KeyboardSequenceEvent::Action;
    if (!_keyboard || (event.action != Action::PRESS && event.action != Action::TAP &&
                       event.action != Action::COMBO_PRESS && event.action != Action::COMBO_TAP))
        return 0;

    uint64_t wait = 0;
    for (ZXKeysEnum key : event.keys)
    {
        const auto it = _lastReleaseFrame.find(_keyboard->GetMatrixKey(key));
        if (it == _lastReleaseFrame.end())
            continue;

        const uint64_t readyAt = it->second + REPRESS_RELEASED_FRAMES;
        if (readyAt > _frameIndex)
            wait = std::max(wait, readyAt - _frameIndex);
    }
    return static_cast<uint16_t>(wait);
}

void DebugKeyboardManager::ExecuteEvent(const KeyboardSequenceEvent& event)
{
    if (!_keyboard)
        return;

    // Explicit PC keys replace the ones derived from the ZX keys
    const bool derive = event.pcKeys.empty();
    auto pressPcKeys = [this, &event]() {
        for (PcKey pc : event.pcKeys)
            ApplyPcKey(pc, /*pressed=*/true);
    };
    auto releasePcKeys = [this, &event]() {
        for (auto it = event.pcKeys.rbegin(); it != event.pcKeys.rend(); ++it)
            ApplyPcKey(*it, /*pressed=*/false);
    };
    
    switch (event.action)
    {
        case KeyboardSequenceEvent::Action::PRESS:
            for (ZXKeysEnum key : event.keys)
            {
                ApplyKey(key, /*pressed=*/true, derive);
            }
            pressPcKeys();
            _frameCountdown = event.frames;
            break;
            
        case KeyboardSequenceEvent::Action::RELEASE:
            releasePcKeys();
            for (ZXKeysEnum key : event.keys)
            {
                ApplyKey(key, /*pressed=*/false, derive);
            }
            _frameCountdown = event.frames;
            break;
            
        case KeyboardSequenceEvent::Action::TAP:
        case KeyboardSequenceEvent::Action::COMBO_TAP:
            // Press all keys
            _tapDerivesPcKeys = derive;
            for (ZXKeysEnum key : event.keys)
            {
                ApplyKey(key, /*pressed=*/true, derive);
                _tapHeldKeys.push_back(key);
            }
            pressPcKeys();
            _tapHeldPcKeys = event.pcKeys;
            _inTapHoldPhase = true;
            _frameCountdown = event.frames;
            break;
            
        case KeyboardSequenceEvent::Action::COMBO_PRESS:
            for (ZXKeysEnum key : event.keys)
            {
                ApplyKey(key, /*pressed=*/true, derive);
            }
            pressPcKeys();
            _frameCountdown = event.frames;
            break;
            
        case KeyboardSequenceEvent::Action::COMBO_RELEASE:
            // Release in reverse order
            releasePcKeys();
            for (auto it = event.keys.rbegin(); it != event.keys.rend(); ++it)
            {
                ApplyKey(*it, /*pressed=*/false, derive);
            }
            _frameCountdown = event.frames;
            break;
            
        case KeyboardSequenceEvent::Action::WAIT:
            _frameCountdown = event.frames;
            break;
            
        case KeyboardSequenceEvent::Action::RELEASE_ALL:
            ReleaseAllKeys();
            _frameCountdown = event.frames;
            break;
    }
}

ZXKeysEnum DebugKeyboardManager::GetTRDOSKeywordKey(const std::string& keyword)
{
    // TR-DOS E-mode keywords (key to press in E-mode)
    // Reference: http://slady.net/Sinclair-ZX-Spectrum-keyboard/
    static const std::map<std::string, ZXKeysEnum> trdosKeywords = {
        {"FORMAT", ZXKEY_0},
        {"CAT", ZXKEY_9},
        {"ERASE", ZXKEY_7},
        {"MOVE", ZXKEY_6},
        {"CLOSE #", ZXKEY_5},
        {"OPEN #", ZXKEY_4},
        {"LINE", ZXKEY_3},
        {"FN", ZXKEY_2},
        {"DEF FN", ZXKEY_1},
    };
    
    auto it = trdosKeywords.find(keyword);
    return (it != trdosKeywords.end()) ? it->second : ZXKEY_NONE;
}

std::vector<ZXKeysEnum> DebugKeyboardManager::CharToKeys(char c)
{
    // Uppercase letters: need CAPS_SHIFT + letter combo
    if (c >= 'A' && c <= 'Z')
    {
        std::string keyName(1, static_cast<char>(std::tolower(c)));
        ZXKeysEnum key = ResolveKeyName(keyName);
        if (key != ZXKEY_NONE)
            return {ZXKEY_CAPS_SHIFT, key};  // CAPS + letter for uppercase
    }
    
    // Lowercase letters: ZX Spectrum default (just the letter key)
    if (c >= 'a' && c <= 'z')
    {
        std::string keyName(1, c);
        ZXKeysEnum key = ResolveKeyName(keyName);
        if (key != ZXKEY_NONE)
            return {key};
    }
    
    // Numbers
    if (c >= '0' && c <= '9')
    {
        return {static_cast<ZXKeysEnum>(c)};
    }
    
    // Space
    if (c == ' ')
    {
        return {ZXKEY_SPACE};
    }
    
    // Common symbols (SYMBOL + key)
    switch (c)
    {
        case '"': return {ZXKEY_SYM_SHIFT, ZXKEY_P};
        case '.': return {ZXKEY_SYM_SHIFT, ZXKEY_M};
        case ',': return {ZXKEY_SYM_SHIFT, ZXKEY_N};
        case '+': return {ZXKEY_SYM_SHIFT, ZXKEY_K};
        case '-': return {ZXKEY_SYM_SHIFT, ZXKEY_J};
        case '*': return {ZXKEY_SYM_SHIFT, ZXKEY_B};
        case '/': return {ZXKEY_SYM_SHIFT, ZXKEY_V};
        case '=': return {ZXKEY_SYM_SHIFT, ZXKEY_L};
        case '!': return {ZXKEY_SYM_SHIFT, ZXKEY_1};
        case '@': return {ZXKEY_SYM_SHIFT, ZXKEY_2};
        case '#': return {ZXKEY_SYM_SHIFT, ZXKEY_3};
        case '$': return {ZXKEY_SYM_SHIFT, ZXKEY_4};
        case '%': return {ZXKEY_SYM_SHIFT, ZXKEY_5};
        case '&': return {ZXKEY_SYM_SHIFT, ZXKEY_6};
        case '\'': return {ZXKEY_SYM_SHIFT, ZXKEY_7};
        case '(': return {ZXKEY_SYM_SHIFT, ZXKEY_8};
        case ')': return {ZXKEY_SYM_SHIFT, ZXKEY_9};
        case '<': return {ZXKEY_SYM_SHIFT, ZXKEY_R};
        case '>': return {ZXKEY_SYM_SHIFT, ZXKEY_T};
        case ';': return {ZXKEY_SYM_SHIFT, ZXKEY_O};
        case ':': return {ZXKEY_SYM_SHIFT, ZXKEY_Z};
        case '?': return {ZXKEY_SYM_SHIFT, ZXKEY_C};
        case '_': return {ZXKEY_SYM_SHIFT, ZXKEY_0};
        case '^': return {ZXKEY_SYM_SHIFT, ZXKEY_H};
        default: return {}; // Unknown character
    }
}

/// endregion </Private Methods>
