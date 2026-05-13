#include "cli-processor.h"
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <debugger/debugmanager.h>
#include <debugger/keyboard/debugkeyboardmanager.h>

#include <algorithm>
#include <iomanip>
#include <sstream>

/// region <Keyboard Injection Commands>

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
    ss << "  press <key>           - Press and hold a key" << NEWLINE;
    ss << "  release <key>         - Release a held key" << NEWLINE;
    ss << "  tap <key> [frames]    - Tap a key (press, hold, release)" << NEWLINE;
    ss << "  combo <key1> <key2>.. - Tap multiple keys simultaneously" << NEWLINE;
    ss << "  macro <name>          - Execute predefined macro sequence" << NEWLINE;
    ss << "  type <text>           - Type text with auto modifier handling" << NEWLINE;
    ss << "  list                  - List all recognized key names" << NEWLINE;
    ss << "  clear                 - Release all keys and reset keyboard state" << NEWLINE;
    ss << NEWLINE;
    ss << "Key names: a-z, 0-9, enter, space, caps, symbol, up, down, left, right," << NEWLINE;
    ss << "           delete, break, edit, dot, comma, plus, minus, quote, etc." << NEWLINE;
    ss << NEWLINE;
    ss << "Macros: e_mode, g_mode, format, cat, erase, move, break" << NEWLINE;
    ss << NEWLINE;
    ss << "Examples:" << NEWLINE;
    ss << "  key tap a              - Tap the 'A' key" << NEWLINE;
    ss << "  key tap enter 5        - Tap ENTER, hold for 5 frames" << NEWLINE;
    ss << "  key combo caps 5       - Tap CAPS+5 (cursor left)" << NEWLINE;
    ss << "  key combo symbol p     - Tap SYMBOL+P (quote character)" << NEWLINE;
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

    std::string keyName = args[1];
    ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(keyName);
    
    if (key == ZXKEY_NONE)
    {
        session.SendResponse(std::string("Error: Unknown key '") + keyName + "'. Use 'key list' to see available keys." + NEWLINE);
        return;
    }

    // Get or create keyboard manager
    DebugKeyboardManager* kbdMgr = context->pDebugManager->GetKeyboardManager();
    if (!kbdMgr)
    {
        session.SendResponse(std::string("Error: Keyboard manager not available.") + NEWLINE);
        return;
    }

    kbdMgr->PressKey(key);
    session.SendResponse(std::string("Pressed: ") + DebugKeyboardManager::GetKeyDisplayName(key) + NEWLINE);
}

void CLIProcessor::HandleKeyRelease(const ClientSession& session, EmulatorContext* context, 
                                    const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing key name. Usage: key release <key>") + NEWLINE);
        return;
    }

    std::string keyName = args[1];
    ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(keyName);
    
    if (key == ZXKEY_NONE)
    {
        session.SendResponse(std::string("Error: Unknown key '") + keyName + "'. Use 'key list' to see available keys." + NEWLINE);
        return;
    }

    DebugKeyboardManager* kbdMgr = context->pDebugManager->GetKeyboardManager();
    if (!kbdMgr)
    {
        session.SendResponse(std::string("Error: Keyboard manager not available.") + NEWLINE);
        return;
    }

    kbdMgr->ReleaseKey(key);
    session.SendResponse(std::string("Released: ") + DebugKeyboardManager::GetKeyDisplayName(key) + NEWLINE);
}

void CLIProcessor::HandleKeyTap(const ClientSession& session, EmulatorContext* context, 
                                const std::vector<std::string>& args)
{
    if (args.size() < 2)
    {
        session.SendResponse(std::string("Error: Missing key name. Usage: key tap <key> [frames]") + NEWLINE);
        return;
    }

    std::string keyName = args[1];
    ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(keyName);
    
    if (key == ZXKEY_NONE)
    {
        session.SendResponse(std::string("Error: Unknown key '") + keyName + "'. Use 'key list' to see available keys." + NEWLINE);
        return;
    }

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

    kbdMgr->TapKey(key, holdFrames);
    session.SendResponse(std::string("Tapping: ") + DebugKeyboardManager::GetKeyDisplayName(key) + 
                        " for " + std::to_string(holdFrames) + " frames" + NEWLINE);
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
            session.SendResponse(std::string("Error: Unknown key '") + args[i] + "'. Use 'key list' to see available keys." + NEWLINE);
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

    kbdMgr->TypeText(text);
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
