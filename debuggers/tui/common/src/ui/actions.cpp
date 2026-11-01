// actions.cpp - the monitor's commands, their UnrealSpeccy keys and the system menu.
#include "ui/actions.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "ui/keymap.h"

namespace dbg {

namespace {

// The key map of unreal.ini [SYSTEM.KEYS]. Scope: mon.* keys work in every window, cpu.* in the trace window, mem.*
// in the memory window (UnrealSpeccy's own rule)
enum class Scope { Monitor, Cpu, Mem, Menu };

struct Binding {
    Action action;
    Key key;
    char ch;
    unsigned mods;
    Scope scope;
};

constexpr Binding kBindings[] = {
    {Action::Step, Key::F7, 0, 0, Scope::Monitor},
    {Action::StepOver, Key::F8, 0, 0, Scope::Monitor},
    {Action::ExitSub, Key::F11, 0, 0, Scope::Monitor},
    {Action::Exit, Key::F4, 0, kModAlt, Scope::Monitor},
    {Action::NextWindow, Key::Tab, 0, 0, Scope::Monitor},
    {Action::PrevWindow, Key::Tab, 0, kModShift, Scope::Monitor},
    {Action::Menu, Key::F10, 0, 0, Scope::Menu},
    {Action::Here, Key::F4, 0, 0, Scope::Cpu},
    {Action::Bpx, Key::Space, 0, 0, Scope::Cpu},
    {Action::Goto, Key::Char, 'g', 0, Scope::Cpu},
    {Action::FindPc, Key::Home, 0, 0, Scope::Cpu},
    {Action::SetPc, Key::Char, 'z', 0, Scope::Cpu},
    {Action::MemSwitch, Key::Tab, 0, kModCtrl, Scope::Mem},
    {Action::MemPc, Key::Char, 'p', kModCtrl, Scope::Mem},
    {Action::MemSp, Key::Char, 's', kModCtrl, Scope::Mem},
};

// mon.emul: Esc in UnrealSpeccy's own ini, ` (TIL) in the TS-Conf build's
bool IsEmulKey(const KeyPress& key, bool isTsconf) {
    if (isTsconf) return key.key == Key::Char && key.ch == '`' && key.mods == 0;
    return key.key == Key::Esc && key.mods == 0;
}

// The menu, top to bottom; None is a separator line
constexpr Action kMenu[] = {
    Action::Emul,     Action::Step,      Action::StepOver,   Action::ExitSub,  Action::Here,  Action::None,
    Action::Bpx,      Action::Goto,      Action::FindPc,     Action::SetPc,    Action::None,  Action::MemSwitch,
    Action::MemPc,    Action::MemSp,     Action::None,       Action::NextWindow, Action::PrevWindow,
    Action::MarkTime, Action::None,      Action::Exit,
};
constexpr int kMenuCount = static_cast<int>(sizeof(kMenu) / sizeof(kMenu[0]));

const char* ActionLabel(Action action, bool running) {
    switch (action) {
        case Action::Emul: return running ? "pause" : "continue";
        case Action::Step: return "step";
        case Action::StepOver: return "step over";
        case Action::ExitSub: return "exit subroutine";
        case Action::Here: return "run to cursor";
        case Action::Bpx: return "breakpoint at cursor";
        case Action::Goto: return "go to address";
        case Action::FindPc: return "cursor to PC";
        case Action::SetPc: return "PC to cursor";
        case Action::MemSwitch: return "dump: hex / text";
        case Action::MemPc: return "dump at PC";
        case Action::MemSp: return "dump at SP";
        case Action::NextWindow: return "next window";
        case Action::PrevWindow: return "previous window";
        case Action::MarkTime: return "time delta from here";
        case Action::Exit: return "exit";
        default: return "";
    }
}

// Commands that change the machine wait while it runs: they need its registers
bool NeedsStop(Action action) {
    switch (action) {
        case Action::Step:
        case Action::StepOver:
        case Action::ExitSub:
        case Action::Here:
        case Action::SetPc:
        case Action::FindPc:
        case Action::MemPc:
        case Action::MemSp:
        case Action::MarkTime:
            return true;
        default:
            return false;
    }
}

bool IsSelectable(Action action, bool running) {
    return action != Action::None && !(running && NeedsStop(action));
}

struct MenuGeometry {
    int x, y, w, h;
    int maxlen;
};

MenuGeometry MeasureMenu(bool isTsconf) {
    const char* const title = "monitor";
    int maxlen = static_cast<int>(std::strlen(title));
    for (const Action a : kMenu) {
        if (a == Action::None) continue;
        // the longer of the two labels of Emul, a gap of two and the key
        const int label = a == Action::Emul ? 8 : static_cast<int>(std::strlen(ActionLabel(a, false)));
        const int len = label + 2 + static_cast<int>(std::strlen(ActionKeyText(a, isTsconf)));
        if (len > maxlen) maxlen = len;
    }
    MenuGeometry g{};
    g.maxlen = maxlen;
    g.w = maxlen + 2;
    g.h = kMenuCount + 3;
    g.x = ((isTsconf ? 157 : 80) - g.w) / 2;
    g.y = (30 - g.h) / 2;
    return g;
}

void MoveMenu(UiState& ui, bool running, int dir) {
    for (int n = 0; n < kMenuCount; ++n) {
        ui.menuCurs = (ui.menuCurs + dir + kMenuCount) % kMenuCount;
        if (IsSelectable(kMenu[ui.menuCurs], running)) return;
    }
}

void FocusWidget(UiState& ui, WidgetId widget) {
    ui.activeWidget = widget;
    ui.activeWindow = static_cast<int>(widget);
    ui.showBank = widget == WidgetId::Pages;
}

void AfterRun(ActionContext& ctx) {
    ctx.ui.traceCurs = ctx.be.GetRegs(0).pc;
    if (ctx.followCursor) ctx.followCursor();
}

bool HandleMenuKey(ActionContext& ctx, const KeyPress& key) {
    UiState& ui = ctx.ui;
    const bool running = ctx.be.IsRunning();
    switch (key.key) {
        case Key::Esc:
        case Key::F10:
            ui.menuOpen = false;
            return true;
        case Key::Up:
        case Key::Left:
            MoveMenu(ui, running, -1);
            return true;
        case Key::Down:
        case Key::Right:
            MoveMenu(ui, running, 1);
            return true;
        case Key::Home:
        case Key::PgUp:
            ui.menuCurs = kMenuCount - 1;
            MoveMenu(ui, running, 1);
            return true;
        case Key::End:
        case Key::PgDn:
            ui.menuCurs = 0;
            MoveMenu(ui, running, -1);
            return true;
        case Key::Enter:
        case Key::Space: {
            const Action action = kMenu[ui.menuCurs];
            if (!IsSelectable(action, running)) return true;
            ui.menuOpen = false;
            RunAction(ctx, action);
            return true;
        }
        default:
            break;
    }
    // The command's own key runs it from the menu as well
    ui.menuOpen = false;
    if (DispatchKey(ctx, key)) return true;
    ui.menuOpen = true;
    return true;
}

}  // namespace

const char* ActionKeyText(Action action, bool isTsconf) {
    switch (action) {
        case Action::Emul: return isTsconf ? "`" : "Esc";
        case Action::Step: return "F7";
        case Action::StepOver: return "F8";
        case Action::ExitSub: return "F11";
        case Action::Here: return "F4";
        case Action::Bpx: return "Space";
        case Action::Goto: return "G";
        case Action::FindPc: return "Home";
        case Action::SetPc: return "Z";
        case Action::MemSwitch: return "Ctrl+Tab";
        case Action::MemPc: return "Ctrl+P";
        case Action::MemSp: return "Ctrl+S";
        case Action::NextWindow: return "Tab";
        case Action::PrevWindow: return "Shift+Tab";
        case Action::Menu: return "F10";
        case Action::Exit: return "Alt+F4";
        default: return "";
    }
}

Action FindAction(const KeyPress& key, const UiState& ui, bool isTsconf) {
    if (IsEmulKey(key, isTsconf)) return Action::Emul;
    for (const Binding& b : kBindings) {
        if (b.key != key.key || b.mods != key.mods) continue;
        if (b.key == Key::Char && b.ch != key.ch) continue;
        if (b.scope == Scope::Cpu && ui.activeWidget != WidgetId::Trace) continue;
        if (b.scope == Scope::Mem && ui.activeWidget != WidgetId::Memory) continue;
        return b.action;
    }
    return Action::None;
}

bool RunAction(ActionContext& ctx, Action action) {
    IDebuggerBackend& be = ctx.be;
    UiState& ui = ctx.ui;
    if (be.IsRunning() && NeedsStop(action)) return false;
    switch (action) {
        case Action::Emul:
            if (be.IsRunning()) be.Pause();
            else be.Continue(0);
            AfterRun(ctx);  // a backend that ran it here has stopped by now
            return true;
        case Action::Step:
            be.Step(0);
            AfterRun(ctx);
            return true;
        case Action::StepOver:
            be.StepOver(0);
            AfterRun(ctx);
            return true;
        case Action::ExitSub:
            be.StepOut(0);
            AfterRun(ctx);
            return true;
        case Action::Here:
            be.RunTo(0, ui.traceCurs);
            AfterRun(ctx);
            return true;
        case Action::Bpx:
            be.SetBpBits(ui.traceCurs, static_cast<uint8_t>(be.BpBitsAt(ui.traceCurs) ^ 1));
            return true;
        case Action::Goto:
            FocusWidget(ui, WidgetId::Trace);
            ui.isEditing = true;
            ui.editBuf.clear();
            return true;
        case Action::FindPc:
            FocusWidget(ui, WidgetId::Trace);
            AfterRun(ctx);
            return true;
        case Action::SetPc:
            be.WriteReg(0, IDebuggerBackend::RegField::Pc, ui.traceCurs);
            return true;
        case Action::MemSwitch:
            ui.memAscii = !ui.memAscii;
            return true;
        case Action::MemPc:
        case Action::MemSp: {
            const Z80Regs r = be.GetRegs(0);
            const uint16_t addr = action == Action::MemPc ? r.pc : r.sp;
            FocusWidget(ui, WidgetId::Memory);
            ui.memTop = static_cast<uint16_t>(addr & 0xFFF8);
            ui.memCurs = addr;
            return true;
        }
        case Action::NextWindow:
            FocusNextWidget(ui, ctx.isTsconf);
            return true;
        case Action::PrevWindow:
            FocusPrevWidget(ui, ctx.isTsconf);
            return true;
        case Action::MarkTime:
            be.SetDebugMark(0, be.GetAbsoluteT(0));
            return true;
        case Action::Menu:
            ui.menuOpen = true;
            ui.menuCurs = kMenuCount - 1;
            MoveMenu(ui, be.IsRunning(), 1);
            return true;
        case Action::Exit:
            if (ctx.quit) ctx.quit();
            return true;
        default:
            return false;
    }
}

bool HandleRegisterKey(ActionContext& ctx, const KeyPress& key) {
    UiState& ui = ctx.ui;
    if (ui.activeWidget != WidgetId::Regs || key.key != Key::Char || ctx.be.IsRunning()) return false;
    // reg.a .. reg.r: the field, edited at once
    struct Field {
        char ch;
        unsigned mods;
        int index;  // kRegLayout
    };
    static constexpr Field kFields[] = {
        {'a', kModCtrl, 0}, {'f', kModCtrl, 1}, {'b', kModCtrl, 2}, {'d', kModCtrl, 3}, {'h', 0, 4},
        {'s', 0, 9},        {'p', 0, 10},       {'x', 0, 11},       {'y', 0, 12},       {'i', 0, 13},
        {'r', 0, 14},       {'m', 0, 15},
    };
    for (const Field& f : kFields) {
        if (f.ch != key.ch || f.mods != key.mods) continue;
        ui.regsCurs = f.index;
        ui.isEditing = f.index != 15;  // IM takes 0..2 or Space
        ui.editBuf.clear();
        return true;
    }
    // reg.iff1 / reg.iff2: Ctrl+1 / Ctrl+2 toggle
    if (key.mods == kModCtrl && (key.ch == '1' || key.ch == '2')) {
        ui.regsCurs = key.ch == '1' ? 16 : 17;
        return HandleSpaceKey(ctx.be, ui);
    }
    // reg.SF .. reg.CF: Alt+S Z 5 H 3 P N C toggle the flag
    if (key.mods == kModAlt) {
        static const char kFlags[] = "sz5h3pnc";
        const char* at = std::strchr(kFlags, key.ch);
        if (at != nullptr && key.ch != 0) {
            ui.regsCurs = 18 + static_cast<int>(at - kFlags);
            return HandleSpaceKey(ctx.be, ui);
        }
    }
    return false;
}

bool DispatchKey(ActionContext& ctx, const KeyPress& key) {
    UiState& ui = ctx.ui;
    if (ui.menuOpen) return HandleMenuKey(ctx, key);
    if (ui.isEditing) {
        // Esc ends an edit before it is mon.emul
        if (key.key == Key::Esc && key.mods == 0) return HandleEscapeKey(ui);
        if (key.key != Key::F10 && !(key.key == Key::F4 && key.mods == kModAlt)) return false;
    }
    const Action action = FindAction(key, ui, ctx.isTsconf);
    if (action != Action::None) {
        RunAction(ctx, action);
        return true;
    }
    return HandleRegisterKey(ctx, key);
}

bool HandleKey(ActionContext& ctx, const KeyPress& key) {
    // Waiting for an emulator: the dialog holds the keys, Alt+F4 still exits
    if (!ctx.be.Online()) {
        if (key.key == Key::F4 && key.mods == kModAlt && ctx.quit) ctx.quit();
        return true;
    }
    if (DispatchKey(ctx, key)) return true;
    IDebuggerBackend& be = ctx.be;
    UiState& ui = ctx.ui;
    if (key.mods & (kModCtrl | kModAlt)) return false;
    const auto arrow = [&](NavKey nav, bool follow) {
        HandleArrowKey(be, ui, nav, ctx.isTsconf);
        if (follow && ui.activeWidget == WidgetId::Trace && ctx.followCursor) ctx.followCursor();
        return true;
    };
    switch (key.key) {
        case Key::Up: return arrow(NavKey::Up, true);
        case Key::Down: return arrow(NavKey::Down, true);
        case Key::Left: return arrow(NavKey::Left, false);
        case Key::Right: return arrow(NavKey::Right, false);
        case Key::PgUp: return arrow(NavKey::PgUp, true);
        case Key::PgDn: return arrow(NavKey::PgDn, true);
        case Key::Enter:
            HandleEnterKey(be, ui);
            if (ui.activeWidget == WidgetId::Trace && ctx.followCursor) ctx.followCursor();
            return true;
        case Key::Backspace: return HandleBackspaceKey(ui);
        case Key::Space: return HandleSpaceKey(be, ui);
        case Key::Char: {
            char ch = key.ch;
            if ((key.mods & kModShift) != 0 && ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            return HandleCharInput(be, ui, ch);
        }
        default: return false;
    }
}

void PaintSystemMenu(TextScreen& screen, const UiState& ui, bool isTsconf, bool running) {
    if (!ui.menuOpen) return;
    const MenuGeometry g = MeasureMenu(isTsconf);
    screen.FilledFrame(g.x, g.y, g.w, g.h);
    screen.FillRect(g.x, g.y, g.w, g.h, kMenuInside);
    const std::string title = "monitor";
    const int titleX = g.x + 1 + (g.maxlen - static_cast<int>(title.size())) / 2;
    screen.FillRect(g.x, g.y, g.w, 1, kMenuHeader);
    screen.Tprint(titleX, g.y, title, kMenuHeader);
    for (int i = 0; i < kMenuCount; ++i) {
        const int row = g.y + 2 + i;
        const Action a = kMenu[i];
        if (a == Action::None) {
            screen.Tprint(g.x + 1, row, std::string(static_cast<size_t>(g.maxlen), '\xC4'), kMenuInside);
            continue;
        }
        const uint8_t attr = !IsSelectable(a, running) ? kMenuItemDis : i == ui.menuCurs ? kMenuCursor : kMenuInside;
        std::string line = ActionLabel(a, running);
        const std::string keyText = ActionKeyText(a, isTsconf);
        line.append(static_cast<size_t>(g.maxlen) - line.size() - keyText.size(), ' ');
        line += keyText;
        screen.FillRect(g.x, row, g.w, 1, attr);
        screen.Tprint(g.x + 1, row, line, attr);
    }
}

void PaintOffline(TextScreen& screen, const std::string& waitingFor) {
    // The last state stays readable, grayed out; only the dialog's frame is drawn (FilledFrame)
    for (int y = 0; y < screen.Height(); ++y)
        for (int x = 0; x < screen.Width(); ++x) screen.SetAttr(x, y, kOffline);
    const std::string title = "no emulator";
    const std::string lines[] = {"Waiting for " + waitingFor, "It reconnects by itself", "Alt+F4 exits"};
    int width = static_cast<int>(title.size());
    for (const std::string& line : lines) width = std::max(width, static_cast<int>(line.size()));
    width = std::min(width + 2, screen.Width() - 2);
    const int height = 2 + static_cast<int>(std::size(lines));
    const int x = (screen.Width() - width) / 2;
    const int y = (screen.Height() - height) / 2;
    screen.FilledFrame(x, y, width, height);
    screen.FillRect(x, y, width, height, kMenuInside);
    screen.FillRect(x, y, width, 1, kMenuHeader);
    screen.Tprint(x + (width - static_cast<int>(title.size())) / 2, y, title, kMenuHeader);
    for (size_t i = 0; i < std::size(lines); ++i) {
        const std::string line = lines[i].substr(0, static_cast<size_t>(width - 2));
        screen.Tprint(x + 1, y + 2 + static_cast<int>(i), line, kMenuInside);
    }
}

bool HandleMenuClick(ActionContext& ctx, int cellX, int cellY) {
    if (!ctx.be.Online()) return true;  // the waiting dialog holds the mouse too
    UiState& ui = ctx.ui;
    if (!ui.menuOpen) return false;
    const MenuGeometry g = MeasureMenu(ctx.isTsconf);
    const int item = cellY - g.y - 2;
    ui.menuOpen = false;
    if (cellX < g.x || cellX >= g.x + g.w || item < 0 || item >= kMenuCount) return true;
    if (IsSelectable(kMenu[item], ctx.be.IsRunning())) RunAction(ctx, kMenu[item]);
    else ui.menuOpen = true;
    return true;
}

}  // namespace dbg
