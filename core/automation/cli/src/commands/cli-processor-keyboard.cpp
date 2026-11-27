#include "cli-processor.h"
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <debugger/debugmanager.h>
#include <debugger/keyboard/debugkeyboardmanager.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

/// region <Keyboard Injection Commands>

/// The keys of a `key` word: one name, or names held together joined with '+' ("w+o"), into `chord`; `display` gets
/// their display names joined the same way. Answers the refusal and returns false for an unknown name
static bool KeyChordOf(const ClientSession& session, const std::string& text, std::vector<std::string>& chord,
                       std::string& display)
{
    std::string error;
    if (!DebugKeyboardManager::ParseKeyChord(text, chord, error))
    {
        session.SendResponse("Error: " + error + ". Use 'key list' to see available keys." + CLIProcessor::NEWLINE);
        return false;
    }
    for (const std::string& name : chord)
    {
        const ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(name);
        // A PC key name (f1, home, pc.up) reaches a PS/2 machine only; its display name is the name itself
        display += (display.empty() ? "" : "+") + (key != ZXKEY_NONE ? DebugKeyboardManager::GetKeyDisplayName(key) : name);
    }
    return true;
}

void CLIProcessor::HandleKey(const ClientSession& session, const std::vector<std::string>& args)
{
    // Get the selected emulator
    auto emulator = GetSelectedEmulator(session);
    if (!emulator)
    {
        session.SendResponse(std::string("Error: No emulator selected.") + NEWLINE);
        return;
    }

    // Get emulator context
    EmulatorContext* context = emulator->GetContext();
    if (!context)
    {
        session.SendResponse(std::string("Error: Unable to access emulator context.") + NEWLINE);
        return;
    }

    // If no arguments, show usage
    if (args.empty())
    {
        ShowKeyHelp(session);
        return;
    }

    std::string subcommand = args[0];
    std::transform(subcommand.begin(), subcommand.end(), subcommand.begin(), ::tolower);

    if (subcommand == "press")
    {
        HandleKeyPress(session, context, args);
    }
    else if (subcommand == "release")
    {
        HandleKeyRelease(session, context, args);
    }
    else if (subcommand == "set")
    {
        HandleKeySet(session, context, args);
    }
    else if (subcommand == "tap")
    {
        HandleKeyTap(session, context, args);
    }
    else if (subcommand == "combo")
    {
        HandleKeyCombo(session, context, args);
    }
    else if (subcommand == "macro")
    {
        HandleKeyMacro(session, context, args);
    }
    else if (subcommand == "type")
    {
        HandleKeyType(session, context, args);
    }
    else if (subcommand == "list")
    {
        HandleKeyList(session);
    }
    else if (subcommand == "route")
    {
        // key route [auto|matrix|ps2|both]: where the host keyboard goes
        Keyboard* keyboard = context->pKeyboard;
        if (!keyboard)
        {
            session.SendResponse(std::string("Error: no keyboard.") + NEWLINE);
            return;
        }
        if (args.size() > 1)
        {
            std::string error;
            if (!keyboard->RequestHostRoute(args[1], error))
            {
                session.SendResponse("Error: " + error + NEWLINE);
                return;
            }
        }
        session.SendResponse(std::string("Keyboard route: ") + Keyboard::HostRouteName(keyboard->GetHostRoute()) +
                             " (in force: " + Keyboard::HostRouteName(keyboard->EffectiveHostRoute()) +
                             (keyboard->HasPs2Sink() ? ", PS/2 controller fitted" : ", no PS/2 controller") +
                             (keyboard->HasPs2Sink() && !keyboard->GetPs2Sink()->ControllerName().empty()
                                  ? ": " + keyboard->GetPs2Sink()->ControllerName()
                                  : std::string()) +
                             ")" + NEWLINE);
    }
    else if (subcommand == "clear" || subcommand == "reset")
    {
        HandleKeyClear(session, context);
    }
    else if (subcommand == "help")
    {
        ShowKeyHelp(session);
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown subcommand '") + subcommand + "'" + NEWLINE +
                            "Use 'key help' for available commands" + NEWLINE);
    }
}

void CLIProcessor::ShowKeyHelp(const ClientSession& session)
{
    std::stringstream ss;
    ss << "Usage: key <subcommand> [args]" << NEWLINE;
    ss << NEWLINE;
    ss << "Subcommands:" << NEWLINE;
    ss << "  press <key>           - Press and hold a key, or keys together (w+o)" << NEWLINE;
    ss << "  release <key>         - Release a held key or chord" << NEWLINE;
    ss << "  set <keys>|none       - Hold exactly these keys (w+o, or w o), release every other injected key; none = release all" << NEWLINE;
    ss << "  tap <key> [frames]    - Tap a key or a chord (press, hold, release)" << NEWLINE;
    ss << "  combo <key1> <key2>.. - Tap multiple keys simultaneously" << NEWLINE;
    ss << "  macro <name>          - Execute predefined macro sequence" << NEWLINE;
    ss << "  type <text>           - Type text with auto modifier handling" << NEWLINE;
    ss << "  list                  - List all recognized key names" << NEWLINE;
    ss << "  clear                 - Release all keys and reset keyboard state" << NEWLINE;
    ss << "  route [auto|matrix|ps2|both] - Where keys go: the ZX matrix, the PS/2 controller (ZX-Evo, ATM Turbo 2+), both" << NEWLINE;
    ss << NEWLINE;
    ss << "Key names: a-z, 0-9, enter, space, caps, symbol, up, down, left, right," << NEWLINE;
    ss << "           delete, break, edit, dot, comma, plus, minus, quote, etc." << NEWLINE;
    ss << NEWLINE;
    ss << "Macros: e_mode, g_mode, format, cat, erase, move, break" << NEWLINE;
    ss << NEWLINE;
    ss << "Examples:" << NEWLINE;
    ss << "  key tap a              - Tap the 'A' key" << NEWLINE;
    ss << "  key tap enter 5        - Tap ENTER, hold for 5 frames" << NEWLINE;
    ss << "  key tap down 2 4 10    - Tap DOWN 4 times, 10 frames apart after each release" << NEWLINE;
    ss << "  key tap caps+5         - Tap CAPS+5 (cursor left): keys joined with '+' go down together" << NEWLINE;
    ss << "  key combo caps 5       - The same, the keys as separate words" << NEWLINE;
    ss << "  key combo symbol p     - Tap SYMBOL+P (quote character)" << NEWLINE;
    ss << "  key set q+p            - Hold Q and P, release whatever else was held (key set none: release all)" << NEWLINE;
    ss << "  key macro e_mode       - Enter Extended mode (E cursor)" << NEWLINE;
    ss << "  key macro format       - Type FORMAT keyword (E-mode + 0)" << NEWLINE;
    ss << "  key type HELLO         - Type 'HELLO' with auto CAPS handling" << NEWLINE;
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleKeyPress(const ClientSession& session, EmulatorContext* context, 
                                  const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing key name. Usage: key press <key>") + NEWLINE);
        return;
    }

    std::vector<std::string> chord;
    std::string displayName;
    if (!KeyChordOf(session, args[1], chord, displayName))
        return;

    // Get or create keyboard manager
    DebugKeyboardManager* kbdMgr = context->pDebugManager->GetKeyboardManager();
    if (!kbdMgr)
    {
        session.SendResponse(std::string("Error: Keyboard manager not available.") + NEWLINE);
        return;
    }

    if (chord.size() == 1)
        kbdMgr->PressKey(chord.front());
    else
        kbdMgr->PressCombo(chord);  // "w+o": held together
    session.SendResponse(std::string("Pressed: ") + displayName + NEWLINE);
}

void CLIProcessor::HandleKeyRelease(const ClientSession& session, EmulatorContext* context, 
                                    const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing key name. Usage: key release <key>") + NEWLINE);
        return;
    }

    std::vector<std::string> chord;
    std::string displayName;
    if (!KeyChordOf(session, args[1], chord, displayName))
        return;

    DebugKeyboardManager* kbdMgr = context->pDebugManager->GetKeyboardManager();
    if (!kbdMgr)
    {
        session.SendResponse(std::string("Error: Keyboard manager not available.") + NEWLINE);
        return;
    }

    if (chord.size() == 1)
        kbdMgr->ReleaseKey(chord.front());
    else
        kbdMgr->ReleaseCombo(chord);
    session.SendResponse(std::string("Released: ") + displayName + NEWLINE);
}

/// key set <k1+k2 | k1 k2 ...> | none: the complete held-key state in one command (DebugKeyboardManager::SetHeldKeys)
void CLIProcessor::HandleKeySet(const ClientSession& session, EmulatorContext* context,
                                const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing keys. Usage: key set <key1+key2 | key1 key2 ...> | none "
                                         "(none releases every injected key)") + NEWLINE);
        return;
    }

    DebugKeyboardManager* kbdMgr = context->pDebugManager->GetKeyboardManager();
    if (!kbdMgr)
    {
        session.SendResponse(std::string("Error: Keyboard manager not available.") + NEWLINE);
        return;
    }

    // "none" alone is the empty state; otherwise every word is a key or a chord
    std::vector<std::string> names;
    std::string only = args[1];
    std::transform(only.begin(), only.end(), only.begin(), ::tolower);
    if (!(args.size() == 2 && only == "none"))
        names.assign(args.begin() + 1, args.end());

    std::string error;
    std::vector<std::string> held;
    const DebugKeyboardManager::SetHeldKeysStatus status = kbdMgr->SetHeldKeys(names, error, &held);
    if (status == DebugKeyboardManager::SetHeldKeysStatus::UnknownKey)
    {
        session.SendResponse("Error: " + error + ". Nothing changed. Use 'key list' to see available keys." + NEWLINE);
        return;
    }
    if (status != DebugKeyboardManager::SetHeldKeysStatus::Ok)
    {
        session.SendResponse("Error: " + error + NEWLINE);
        return;
    }

    std::string list;
    for (const std::string& name : held)
        list += (list.empty() ? "" : "+") + name;
    session.SendResponse((held.empty() ? std::string("Held: none (every injected key released)") : "Held: " + list) + NEWLINE);
}

void CLIProcessor::HandleKeyTap(const ClientSession& session, EmulatorContext* context, 
                                const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing key name. Usage: key tap <key> [frames] [count] [gap_frames]") + NEWLINE);
        return;
    }

    std::vector<std::string> chord;
    std::string displayName;
    if (!KeyChordOf(session, args[1], chord, displayName))
        return;

    uint16_t holdFrames = 2; // Default
    if (args.size() >= 3)
    {
        try
        {
            holdFrames = static_cast<uint16_t>(std::stoi(args[2]));
        }
        catch (const std::exception&)
        {
            session.SendResponse(std::string("Error: Invalid frame count '") + args[2] + "'" + NEWLINE);
            return;
        }
    }

    DebugKeyboardManager* kbdMgr = context->pDebugManager->GetKeyboardManager();
    if (!kbdMgr)
    {
        session.SendResponse(std::string("Error: Keyboard manager not available.") + NEWLINE);
        return;
    }

    // [count] taps in a row, [gap_frames] after each release (repeats of one key always wait 5 released frames)
    uint16_t count = 1, gapFrames = 0;
    for (size_t i = 3; i < args.size() && i < 5; i++)
    {
        int value = -1;
        try
        {
            value = std::stoi(args[i]);
        }
        catch (const std::exception&)
        {
        }
        const int limit = i == 3 ? DebugKeyboardManager::MAX_TAP_COUNT : 500;
        if (value < (i == 3 ? 1 : 0) || value > limit)
        {
            session.SendResponse(std::string("Error: ") + (i == 3 ? "count" : "gap_frames") + " '" + args[i] + "' must be " +
                                 (i == 3 ? "1.." : "0..") + std::to_string(limit) + NEWLINE);
            return;
        }
        (i == 3 ? count : gapFrames) = static_cast<uint16_t>(value);
    }

    if (chord.size() == 1)
        kbdMgr->TapKeyRepeated(chord.front(), holdFrames, count, gapFrames);
    else
        kbdMgr->TapComboRepeated(chord, holdFrames, count, gapFrames);  // down and up together, like `key combo`
    session.SendResponse(std::string("Tapping: ") + displayName + " for " + std::to_string(holdFrames) + " frames" +
                         (count > 1 ? ", " + std::to_string(count) + " times" : std::string()) +
                         (gapFrames ? ", " + std::to_string(gapFrames) + " frames apart" : std::string()) + NEWLINE);
}

void CLIProcessor::HandleKeyCombo(const ClientSession& session, EmulatorContext* context, 
                                  const std::vector<std::string>& args)
{
    if (args.size() < 3)
    {
        session.SendResponse(std::string("Error: Need at least 2 keys. Usage: key combo <key1> <key2> [key3...]") + NEWLINE);
        return;
    }

    std::vector<ZXKeysEnum> keys;
    std::stringstream keyNames;
    
    for (size_t i = 1; i < args.size(); ++i)
    {
        ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(args[i]);
        if (key == ZXKEY_NONE)
        {
            session.SendResponse(std::string("Error: Unknown key '") + args[i] + "'. Use 'key list' to see available keys." + CLIProcessor::NEWLINE);
            return;
        }
        keys.push_back(key);
        
        if (i > 1) keyNames << "+";
        keyNames << DebugKeyboardManager::GetKeyDisplayName(key);
    }

    DebugKeyboardManager* kbdMgr = context->pDebugManager->GetKeyboardManager();
    if (!kbdMgr)
    {
        session.SendResponse(std::string("Error: Keyboard manager not available.") + NEWLINE);
        return;
    }

    kbdMgr->TapCombo(keys);
    session.SendResponse(std::string("Tapping combo: ") + keyNames.str() + NEWLINE);
}

void CLIProcessor::HandleKeyMacro(const ClientSession& session, EmulatorContext* context, 
                                  const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing macro name. Usage: key macro <name>") + NEWLINE);
        session.SendResponse(std::string("Available macros: e_mode, g_mode, format, cat, erase, move, break") + NEWLINE);
        return;
    }

    std::string macroName = args[1];
    std::transform(macroName.begin(), macroName.end(), macroName.begin(), ::tolower);

    DebugKeyboardManager* kbdMgr = context->pDebugManager->GetKeyboardManager();
    if (!kbdMgr)
    {
        session.SendResponse(std::string("Error: Keyboard manager not available.") + NEWLINE);
        return;
    }

    if (kbdMgr->ExecuteNamedSequence(macroName))
    {
        session.SendResponse(std::string("Executing macro: ") + macroName + NEWLINE);
    }
    else
    {
        session.SendResponse(std::string("Error: Unknown macro '") + macroName + "'" + NEWLINE);
        session.SendResponse(std::string("Available macros: e_mode, g_mode, format, cat, erase, move, break") + NEWLINE);
    }
}

void CLIProcessor::HandleKeyType(const ClientSession& session, EmulatorContext* context, 
                                 const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing text. Usage: key type <text>") + NEWLINE);
        return;
    }

    // Join all remaining args with spaces
    std::stringstream textStream;
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (i > 1) textStream << " ";
        textStream << args[i];
    }
    std::string text = textStream.str();

    DebugKeyboardManager* kbdMgr = context->pDebugManager->GetKeyboardManager();
    if (!kbdMgr)
    {
        session.SendResponse(std::string("Error: Keyboard manager not available.") + NEWLINE);
        return;
    }

    if (!kbdMgr->TypeText(text).empty())
    {
        session.SendResponse(std::string("Error: no key types some characters of the text; nothing typed.") + NEWLINE);
        return;
    }
    session.SendResponse(std::string("Typing: \"") + text + "\"" + NEWLINE);
}

void CLIProcessor::HandleKeyList(const ClientSession& session)
{
    std::stringstream ss;
    ss << "Recognized Key Names:" << NEWLINE;
    ss << "=====================" << NEWLINE;
    ss << NEWLINE;
    
    ss << "Letters:    a b c d e f g h i j k l m n o p q r s t u v w x y z" << NEWLINE;
    ss << "Numbers:    0 1 2 3 4 5 6 7 8 9" << NEWLINE;
    ss << "Modifiers:  caps symbol (aliases: shift, sym)" << NEWLINE;
    ss << "Special:    enter space" << NEWLINE;
    ss << "Cursor:     up down left right" << NEWLINE;
    ss << "Editing:    delete backspace break edit" << NEWLINE;
    ss << "Symbols:    dot comma plus minus multiply divide equal quote" << NEWLINE;
    ss << NEWLINE;
    ss << "Note: Cursor keys (up/down/left/right) are extended keys that" << NEWLINE;
    ss << "      automatically decompose to CAPS+5/6/7/8 combinations." << NEWLINE;
    
    session.SendResponse(ss.str());
}

void CLIProcessor::HandleKeyClear(const ClientSession& session, EmulatorContext* context)
{
    DebugKeyboardManager* kbdMgr = context->pDebugManager->GetKeyboardManager();
    if (!kbdMgr)
    {
        session.SendResponse(std::string("Error: Keyboard manager not available.") + NEWLINE);
        return;
    }

    kbdMgr->ReleaseAllKeys();
    session.SendResponse(std::string("Keyboard state reset. All keys released.") + NEWLINE);
}

/// endregion </Keyboard Injection Commands>
