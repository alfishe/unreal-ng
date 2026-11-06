// actions.h - the monitor's commands, their UnrealSpeccy keys and the system menu.
//
// One table for both front ends: the terminal (FTXUI) and the SDL window translate their key events into a KeyPress,
// DispatchKey finds the command by the key map of UnrealSpeccy's unreal.ini ([SYSTEM.KEYS]: mon.*, cpu.*, mem.*,
// reg.*) and the window in focus, and RunAction carries it out. The system menu (F10) lists the same commands with
// their keys, so everything is reachable without knowing a hotkey.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "backend/debugger-backend.h"
#include "panels/paint.h"
#include "screen/textscreen.h"

namespace dbg {

enum class Key {
    None, Char, Esc, Tab, Enter, Backspace, Space, Home, End, Up, Down, Left, Right, PgUp, PgDn,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
};

constexpr unsigned kModShift = 1;
constexpr unsigned kModCtrl = 2;
constexpr unsigned kModAlt = 4;

// A key with its modifiers; ch is the character of Key::Char (lower case for letters, shift in mods)
struct KeyPress {
    Key key = Key::None;
    char ch = 0;
    unsigned mods = 0;
};

enum class Action {
    None,
    Step,         // mon.step F7
    StepOver,     // mon.stepover F8
    ExitSub,      // mon.exitsub F11
    Here,         // cpu.here F4
    Emul,         // mon.emul: Continue, or Pause while the machine runs
    Bpx,          // cpu.bpx Space
    Goto,         // cpu.goto G
    FindPc,       // cpu.findpc Home
    SetPc,        // cpu.setpc Z
    MemSwitch,    // mem.switch Ctrl+Tab
    MemPc,        // mem.pc Ctrl+P
    MemSp,        // mem.sp Ctrl+S
    NextWindow,   // mon.next Tab
    PrevWindow,   // mon.prev Shift+Tab
    MarkTime,     // the time delta counts from now (no UnrealSpeccy key)
    Menu,         // the system menu F10
    Exit,         // mon.exit Alt+F4
};

// What a command needs from its front end
struct ActionContext {
    IDebuggerBackend& be;
    UiState& ui;
    bool isTsconf;
    std::function<void()> followCursor;
    std::function<void()> quit;
};

// The key text of a command as UnrealSpeccy's ini names it ("F7", "Shift+Tab"); "" when it has none
const char* ActionKeyText(Action action, bool isTsconf);

// The command a key runs in the window in focus; None for keys the editors and navigation take
Action FindAction(const KeyPress& key, const UiState& ui, bool isTsconf);

// Carries out a command; true when the screen is to be painted again
bool RunAction(ActionContext& ctx, Action action);

// UnrealSpeccy's reg.* keys inside the registers window (H edits HL, Alt+Z toggles ZF ...); true when handled
bool HandleRegisterKey(ActionContext& ctx, const KeyPress& key);

// The whole key path of both front ends: the open menu, the commands, the register keys; true when handled.
// Keys it leaves (arrows, Enter, typing) go to the editors of keymap.h
bool DispatchKey(ActionContext& ctx, const KeyPress& key);

// DispatchKey, then the editors and navigation of keymap.h (arrows, Enter, Backspace, Space, typing): the key path
// of every front end; true when the screen is to be painted again
bool HandleKey(ActionContext& ctx, const KeyPress& key);

// The system menu over the frame when it is open (ui.menuOpen)
void PaintSystemMenu(TextScreen& screen, const UiState& ui, bool isTsconf, bool running);
// No emulator (IDebuggerBackend::Online false): the screen grayed out under the waiting dialog
void PaintOffline(TextScreen& screen, const std::string& waitingFor);

// A click on the open menu runs the item under it or closes the menu; true when the menu was open
bool HandleMenuClick(ActionContext& ctx, int cellX, int cellY);

}  // namespace dbg
