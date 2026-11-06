// test-main.cpp - micro test harness for classic debugger interactivity and navigation.
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "backend/api-transport.h"
#include "backend/mock-backend.h"
#include "backend/webapi-backend.h"
#include "panels/paint.h"
#include "tui/key-input.h"
#include "ui/actions.h"
#include "ui/keymap.h"
#include "../../common/tests/offline-look.h"

namespace {

int failures = 0;

void Check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++failures;
    }
}

void TestWidgetTraversal() {
    dbg::UiState ui;
    ui.activeWidget = dbg::WidgetId::Regs;

    // Classic forward traversal order (4 focusable widgets)
    const dbg::WidgetId expectedForward[] = {
        dbg::WidgetId::Trace,
        dbg::WidgetId::Memory,
        dbg::WidgetId::Pages,
        dbg::WidgetId::Regs
    };

    for (const auto exp : expectedForward) {
        dbg::FocusNextWidget(ui, false);
        Check(ui.activeWidget == exp, "Forward cycle to widget " + std::to_string(static_cast<int>(exp)));
    }

    // Classic reverse traversal (4 focusable widgets)
    const dbg::WidgetId expectedReverse[] = {
        dbg::WidgetId::Pages,
        dbg::WidgetId::Memory,
        dbg::WidgetId::Trace,
        dbg::WidgetId::Regs
    };

    for (const auto exp : expectedReverse) {
        dbg::FocusPrevWidget(ui, false);
        Check(ui.activeWidget == exp, "Reverse cycle to widget " + std::to_string(static_cast<int>(exp)));
    }
}

void TestVisualFocusBackgrounds() {
    auto bePtr = dbg::CreateBackend(dbg::BackendKind::Mock, "");
    auto& be = *bePtr;
    dbg::TextScreen screen(80, 30);
    dbg::UiState ui;

    // 1. Regs focused (active = blue, passive widgets = green/off)
    ui.activeWidget = dbg::WidgetId::Regs;
    screen.Clear();
    dbg::PaintClassicFrame(screen, be, ui);
    Check((screen.AttrAt(1, 1) >> 4) == 1, "Regs body has blue background when focused");
    Check((screen.AttrAt(39, 1) >> 4) != 1, "Watches panel remains passive when Regs focused");
    Check((screen.AttrAt(72, 1) >> 4) != 1, "Ports panel remains passive when Regs focused");
    Check((screen.AttrAt(72, 12) >> 4) != 1, "Stack panel remains passive when Regs focused");
    Check((screen.AttrAt(1, 28) >> 4) != 1, "Bottom bar remains passive when Regs focused");

    // 2. Trace focused
    ui.activeWidget = dbg::WidgetId::Trace;
    screen.Clear();
    dbg::PaintClassicFrame(screen, be, ui);
    Check((screen.AttrAt(1, 6) >> 4) == 1, "Trace body has blue background when focused");

    // 3. Memory focused
    ui.activeWidget = dbg::WidgetId::Memory;
    screen.Clear();
    dbg::PaintClassicFrame(screen, be, ui);
    Check((screen.AttrAt(34, 15) >> 4) == 1, "Memory body has blue background when focused");
    Check((screen.AttrAt(39, 1) >> 4) != 1, "Watches panel remains passive when Memory focused");

    // 4. Pages focused
    ui.activeWidget = dbg::WidgetId::Pages;
    screen.Clear();
    dbg::PaintClassicFrame(screen, be, ui);
    Check((screen.AttrAt(72, 23) >> 4) == 1 || screen.AttrAt(72, 23) == dbg::kWCurs, "Pages body has blue background when focused");
    Check((screen.AttrAt(72, 12) >> 4) != 1, "Stack panel remains passive when Pages focused");
}

void TestReadOnlyWidgets() {
    auto bePtr = dbg::CreateBackend(dbg::BackendKind::Mock, "");
    auto& be = *bePtr;
    dbg::UiState ui;

    // Pages is focusable and strictly read-only
    ui.activeWidget = dbg::WidgetId::Pages;
    ui.pagesCurs = 2;
    Check(!dbg::HandleCharInput(be, ui, 'A'), "Pages rejects char input");
    Check(!dbg::HandleEnterKey(be, ui), "Pages rejects enter key");
    Check(!ui.isEditing, "Pages does not enter editing mode");
}

void TestFieldNavigation() {
    auto bePtr = dbg::CreateBackend(dbg::BackendKind::Mock, "");
    auto& be = *bePtr;
    dbg::UiState ui;

    // Regs navigation
    ui.activeWidget = dbg::WidgetId::Regs;
    ui.regsCurs = 0; // A
    dbg::HandleArrowKey(be, ui, dbg::NavKey::Right, false);
    Check(ui.regsCurs == 1, "Regs: right from A moves to F");
    dbg::HandleArrowKey(be, ui, dbg::NavKey::Down, false);
    Check(ui.regsCurs == 2, "Regs: down from F moves to BC");

    // Trace navigation
    ui.activeWidget = dbg::WidgetId::Trace;
    ui.traceCurs = 0x8000;
    dbg::HandleArrowKey(be, ui, dbg::NavKey::Down, false);
    Check(ui.traceCurs > 0x8000, "Trace: down advances instruction cursor");

    // Memory navigation
    ui.activeWidget = dbg::WidgetId::Memory;
    ui.memTop = 0xC000;
    ui.memCurs = 0xC000;
    dbg::HandleArrowKey(be, ui, dbg::NavKey::Right, false);
    Check(ui.memCurs == 0xC001, "Memory: right advances byte");
    dbg::HandleArrowKey(be, ui, dbg::NavKey::Down, false);
    Check(ui.memCurs == 0xC009, "Memory: down advances 8 bytes");

    // Pages navigation (strictly read-only cursor movement)
    ui.activeWidget = dbg::WidgetId::Pages;
    ui.pagesCurs = 0;
    dbg::HandleArrowKey(be, ui, dbg::NavKey::Down, false);
    Check(ui.pagesCurs == 1 && ui.selBank == 1, "Pages: down advances bank cursor");
    dbg::HandleArrowKey(be, ui, dbg::NavKey::Up, false);
    Check(ui.pagesCurs == 0 && ui.selBank == 0, "Pages: up moves bank cursor back");
}

void TestEditingLifecycle() {
    auto bePtr = dbg::CreateBackend(dbg::BackendKind::Mock, "");
    auto& be = *bePtr;
    dbg::UiState ui;

    // 1. Edit 8-bit register A
    ui.activeWidget = dbg::WidgetId::Regs;
    ui.regsCurs = 0; // A
    Check(dbg::HandleCharInput(be, ui, '4'), "Reg A accepts '4'");
    Check(ui.isEditing && ui.editBuf == "4", "Reg A enters editing mode with '4'");
    Check(dbg::HandleCharInput(be, ui, '2'), "Reg A accepts '2'");
    Check(ui.editBuf == "42", "Reg A edit buffer is '42'");
    Check(dbg::HandleEnterKey(be, ui), "Reg A Enter key commits");
    Check(!ui.isEditing && ui.editBuf.empty(), "Editing mode cleared after commit");
    Check(be.GetRegs(0).a == 0x42, "Register A updated in backend to 0x42");

    // 2. Edit 16-bit register BC
    ui.regsCurs = 2; // BC
    dbg::HandleCharInput(be, ui, '1');
    dbg::HandleCharInput(be, ui, '2');
    dbg::HandleCharInput(be, ui, '3');
    dbg::HandleCharInput(be, ui, '4');
    Check(ui.editBuf == "1234", "BC buffer is '1234'");
    dbg::HandleEnterKey(be, ui);
    Check(be.GetRegs(0).bc == 0x1234, "Register BC updated to 0x1234");

    // 3. Toggle flag bit (CF is index 25)
    ui.regsCurs = 25; // CF
    const uint8_t initF = be.GetRegs(0).f;
    dbg::HandleSpaceKey(be, ui);
    Check((be.GetRegs(0).f & 1) == !(initF & 1), "Flag CF toggled via Space key");

    // 4. Edit Memory in hex mode
    ui.activeWidget = dbg::WidgetId::Memory;
    ui.memCurs = 0x8000;
    ui.memAscii = false;
    dbg::HandleCharInput(be, ui, 'A');
    dbg::HandleCharInput(be, ui, 'B'); // 2 digits auto-commit in hex mode
    const auto memBytes = be.ReadMemory(0, 0x8000, 1);
    Check(memBytes[0] == 0xAB, "Memory byte written to 0xAB");
    Check(ui.memCurs == 0x8001, "Memory cursor advanced after byte write");

    // 5. Jump address in Trace
    ui.activeWidget = dbg::WidgetId::Trace;
    ui.traceCurs = 0x0000;
    dbg::HandleCharInput(be, ui, '8');
    dbg::HandleCharInput(be, ui, '0');
    dbg::HandleCharInput(be, ui, '0');
    dbg::HandleCharInput(be, ui, '0');
    dbg::HandleEnterKey(be, ui);
    Check(ui.traceCurs == 0x8000, "Trace jumped to address 0x8000");

    // 6. Escape cancels editing
    ui.activeWidget = dbg::WidgetId::Regs;
    ui.regsCurs = 0;
    dbg::HandleCharInput(be, ui, '9');
    Check(ui.isEditing, "Editing active before Escape");
    Check(dbg::HandleEscapeKey(ui), "Escape handled");
    Check(!ui.isEditing && ui.editBuf.empty(), "Editing cancelled by Escape");
}

void TestMouseInteractivity() {
    auto bePtr = dbg::CreateBackend(dbg::BackendKind::Mock, "");
    auto& be = *bePtr;
    dbg::UiState ui;

    // 1. Click on Regs panel
    // Click on AF (row 1, x=4 -> A)
    Check(dbg::HandleMouseClick(be, ui, 4, 1, false), "Click on Regs (A) handled");
    Check(ui.activeWidget == dbg::WidgetId::Regs, "Regs widget focused by click");
    Check(ui.regsCurs == 0, "Reg A field selected");

    // Click on SP (row 1, x=20 -> SP)
    dbg::HandleMouseClick(be, ui, 20, 1, false);
    Check(ui.activeWidget == dbg::WidgetId::Regs, "Regs still focused");
    Check(ui.regsCurs == 9, "Reg SP field selected");

    // Click on SF flag (row 4, x=25 -> SF)
    dbg::HandleMouseClick(be, ui, 25, 4, false);
    Check(ui.regsCurs == 18, "Flag SF field selected");

    // 2. Click on Trace panel
    ui.traceTop = 0x8000;
    Check(dbg::HandleMouseClick(be, ui, 10, 8, false), "Click on Trace handled");
    Check(ui.activeWidget == dbg::WidgetId::Trace, "Trace widget focused by click");
    const auto disasm = be.Disassemble(0, ui.traceTop, 21, true);
    Check(ui.traceCurs == disasm[2].addr, "Trace cursor set to instruction on row 2");

    // 3. Click on Memory panel
    ui.memTop = 0x8000;
    // Click in hex area: row 2 (y=17), byte 3 (x = 39 + 3*3 = 48)
    Check(dbg::HandleMouseClick(be, ui, 48, 17, false), "Click on Memory hex handled");
    Check(ui.activeWidget == dbg::WidgetId::Memory, "Memory widget focused by click");
    Check(ui.memCurs == 0x8000 + 2 * 8 + 3, "Memory cursor positioned at row 2 byte 3");
    Check(!ui.memAscii, "Memory in HEX mode");

    // Click in ASCII area: row 2 (y=17), byte 5 (x = 63 + 5 = 68)
    Check(dbg::HandleMouseClick(be, ui, 68, 17, false), "Click on Memory ascii handled");
    Check(ui.memCurs == 0x8000 + 2 * 8 + 5, "Memory cursor positioned at row 2 byte 5");
    Check(ui.memAscii, "Memory switched to ASCII mode");

    // 4. Click on Pages panel (row 1: y=24, x=74)
    Check(dbg::HandleMouseClick(be, ui, 74, 24, false), "Click on Pages handled");
    Check(ui.activeWidget == dbg::WidgetId::Pages, "Pages widget focused by click");
    Check(ui.pagesCurs == 1, "Pages row 1 selected");

    // 5. Clicks on non-focusable widgets return false and do not change focus
    Check(!dbg::HandleMouseClick(be, ui, 40, 3, false), "Click on Watches rejected");
    Check(ui.activeWidget == dbg::WidgetId::Pages, "Focus remains on Pages");

    Check(!dbg::HandleMouseClick(be, ui, 74, 3, false), "Click on Ports rejected");
    Check(ui.activeWidget == dbg::WidgetId::Pages, "Focus remains on Pages");

    Check(!dbg::HandleMouseClick(be, ui, 74, 7, false), "Click on Beta128 rejected");
    Check(ui.activeWidget == dbg::WidgetId::Pages, "Focus remains on Pages");

    Check(!dbg::HandleMouseClick(be, ui, 74, 15, false), "Click on Stack rejected");
    Check(ui.activeWidget == dbg::WidgetId::Pages, "Focus remains on Pages");

    Check(!dbg::HandleMouseClick(be, ui, 10, 28, false), "Click on Bottom (time) rejected");
    Check(ui.activeWidget == dbg::WidgetId::Pages, "Focus remains on Pages");

    Check(!dbg::HandleMouseClick(be, ui, 43, 28, false), "Click on Bottom (AY) rejected");
    Check(ui.activeWidget == dbg::WidgetId::Pages, "Focus remains on Pages");

    // 6. Click commits active edit
    ui.activeWidget = dbg::WidgetId::Regs;
    ui.regsCurs = 0; // A
    dbg::HandleCharInput(be, ui, '4');
    dbg::HandleCharInput(be, ui, '2');
    Check(ui.isEditing, "Editing active before click");
    // Click on DE register (row 3, x=4)
    dbg::HandleMouseClick(be, ui, 4, 3, false);
    Check(!ui.isEditing, "Editing ended by click");
    Check(be.GetRegs(0).a == 0x42, "Previous edit to A committed on mouse click");
    Check(ui.regsCurs == 3, "DE field selected");
}

dbg::KeyPress Press(dbg::Key key, unsigned mods = 0, char ch = 0) {
    dbg::KeyPress k;
    k.key = key;
    k.mods = mods;
    k.ch = ch;
    return k;
}

// UnrealSpeccy's keys (unreal.ini [SYSTEM.KEYS]) over the mock: PC 8011 = call 8020, 8014 = jr 8014
void TestUnrealKeys() {
    auto bePtr = dbg::CreateBackend(dbg::BackendKind::Mock, "");
    auto& be = *bePtr;
    dbg::UiState ui;
    bool quit = false;
    dbg::ActionContext ctx{be, ui, false, nullptr, [&] { quit = true; }};
    ui.activeWidget = dbg::WidgetId::Trace;
    for (unsigned a = 0; a < 0x10000; ++a) be.SetBpBits(static_cast<uint16_t>(a), 0);  // the sample's own ones

    Check(be.GetRegs(0).pc == 0x8011, "keys: starts at the call");
    Check(dbg::HandleKey(ctx, Press(dbg::Key::F7)), "F7 handled");
    Check(be.GetRegs(0).pc == 0x8020 && ui.traceCurs == 0x8020, "F7 steps into the call, cursor follows");
    Check(dbg::HandleKey(ctx, Press(dbg::Key::F11)), "F11 handled");
    Check(be.GetRegs(0).pc == 0x8014, "F11 runs to the return address on the stack");

    be.WriteReg(0, dbg::IDebuggerBackend::RegField::Pc, 0x8011);
    dbg::HandleKey(ctx, Press(dbg::Key::F8));
    Check(be.GetRegs(0).pc == 0x8014, "F8 steps over the call");

    ui.traceCurs = 0x8020;
    dbg::HandleKey(ctx, Press(dbg::Key::Char, 0, 'z'));
    Check(be.GetRegs(0).pc == 0x8020, "Z sets the PC to the cursor");
    ui.traceCurs = 0x8022;
    dbg::HandleKey(ctx, Press(dbg::Key::Space));
    Check((be.BpBitsAt(0x8022) & 1) != 0, "Space sets a breakpoint at the cursor");
    ui.traceCurs = 0x8000;
    dbg::HandleKey(ctx, Press(dbg::Key::Home));
    Check(ui.traceCurs == 0x8020, "Home puts the cursor at the PC");
    dbg::HandleKey(ctx, Press(dbg::Key::Esc));
    Check(be.GetRegs(0).pc == 0x8022, "Esc (mon.emul) runs to the breakpoint");
    dbg::HandleKey(ctx, Press(dbg::Key::Space));
    Check((be.BpBitsAt(0x8022) & 1) == 0, "Space clears it");
    ui.traceCurs = 0x8028;
    dbg::HandleKey(ctx, Press(dbg::Key::F4));
    Check(be.GetRegs(0).pc == 0x8028, "F4 runs to the cursor");

    dbg::HandleKey(ctx, Press(dbg::Key::Char, 0, 'g'));
    Check(ui.isEditing, "G opens the address entry");
    dbg::HandleKey(ctx, Press(dbg::Key::Esc));
    Check(!ui.isEditing && be.GetRegs(0).pc == 0x8028, "Esc cancels the entry and does not run");

    // Window keys
    dbg::HandleKey(ctx, Press(dbg::Key::Tab));
    Check(ui.activeWidget == dbg::WidgetId::Memory, "Tab: next window");
    dbg::HandleKey(ctx, Press(dbg::Key::Tab, dbg::kModShift));
    Check(ui.activeWidget == dbg::WidgetId::Trace, "Shift+Tab: previous window");
    dbg::HandleKey(ctx, Press(dbg::Key::Char, dbg::kModCtrl, 'p'));
    Check(ui.activeWidget == dbg::WidgetId::Trace, "Ctrl+P is a memory window key");
    ui.activeWidget = dbg::WidgetId::Memory;
    dbg::HandleKey(ctx, Press(dbg::Key::Char, dbg::kModCtrl, 'p'));
    Check(ui.memCurs == 0x8028, "Ctrl+P: dump at the PC");
    const bool ascii = ui.memAscii;
    dbg::HandleKey(ctx, Press(dbg::Key::Tab, dbg::kModCtrl));
    Check(ui.memAscii != ascii, "Ctrl+Tab: hex / text");

    // Register keys
    ui.activeWidget = dbg::WidgetId::Regs;
    dbg::HandleKey(ctx, Press(dbg::Key::Char, 0, 'h'));
    Check(ui.regsCurs == 4 && ui.isEditing, "H edits HL");
    dbg::HandleKey(ctx, Press(dbg::Key::Char, 0, '1'));
    dbg::HandleKey(ctx, Press(dbg::Key::Char, 0, '2'));
    dbg::HandleKey(ctx, Press(dbg::Key::Enter));
    Check(be.GetRegs(0).hl == 0x12, "HL takes the typed value");
    const uint8_t f = be.GetRegs(0).f;
    dbg::HandleKey(ctx, Press(dbg::Key::Char, dbg::kModAlt, 'z'));
    Check((be.GetRegs(0).f ^ f) == 0x40, "Alt+Z toggles ZF");

    // The system menu: F10 opens it, its items run, Esc closes it
    dbg::HandleKey(ctx, Press(dbg::Key::F10));
    Check(ui.menuOpen, "F10 opens the menu");
    dbg::TextScreen screen(80, 30);
    screen.Clear();
    dbg::PaintSystemMenu(screen, ui, false, false);
    std::string rows;
    for (int y = 0; y < 30; ++y)
        for (int x = 0; x < 80; ++x) rows += static_cast<char>(screen.CharAt(x, y));
    Check(rows.find("monitor") != std::string::npos, "menu title painted");
    Check(rows.find("step over") != std::string::npos && rows.find("F8") != std::string::npos,
          "menu items show their keys");
    dbg::HandleKey(ctx, Press(dbg::Key::Down));  // continue -> step
    const uint16_t pc = be.GetRegs(0).pc;
    dbg::HandleKey(ctx, Press(dbg::Key::Enter));
    Check(!ui.menuOpen && be.GetRegs(0).pc != pc, "menu item runs and closes the menu");
    dbg::HandleKey(ctx, Press(dbg::Key::F10));
    dbg::HandleKey(ctx, Press(dbg::Key::Esc));
    Check(!ui.menuOpen, "Esc closes the menu");
    Check(!quit, "nothing quit yet");
    dbg::HandleKey(ctx, Press(dbg::Key::F4, dbg::kModAlt));
    Check(quit, "Alt+F4 exits");
}

// The terminal's events as keys: Ctrl and Alt arrive as control characters and ESC prefixes
void TestTerminalKeys() {
    dbg::KeyPress k = dbg::KeyFromTerminal(ftxui::Event::Character('Z'));
    Check(k.key == dbg::Key::Char && k.ch == 'z' && k.mods == dbg::kModShift, "terminal: Z is Shift+z");
    k = dbg::KeyFromTerminal(ftxui::Event::Special("\x10"));
    Check(k.key == dbg::Key::Char && k.ch == 'p' && k.mods == dbg::kModCtrl, "terminal: 0x10 is Ctrl+P");
    k = dbg::KeyFromTerminal(ftxui::Event::Special("\x1bz"));
    Check(k.key == dbg::Key::Char && k.ch == 'z' && k.mods == dbg::kModAlt, "terminal: ESC z is Alt+Z");
    k = dbg::KeyFromTerminal(ftxui::Event::TabReverse);
    Check(k.key == dbg::Key::Tab && k.mods == dbg::kModShift, "terminal: Shift+Tab");
    k = dbg::KeyFromTerminal(ftxui::Event::F8);
    Check(k.key == dbg::Key::F8, "terminal: F8");
    k = dbg::KeyFromTerminal(ftxui::Event::Character(' '));
    Check(k.key == dbg::Key::Space, "terminal: Space");
}

// A server with a list of instances that the test changes; every instance request answers an empty object
class FakeServer : public dbg::IApiTransport {
public:
    std::string list = "{\"emulators\":[]}";
    bool up = true;
    int requests = 0;

    std::string Name() const override { return "fake"; }
    void SetTimeoutSeconds(int) override {}
    dbg::HttpReply Request(const std::string& method, const std::string& path, const std::string& body) override {
        (void)method;
        (void)body;
        ++requests;
        dbg::HttpReply reply;
        if (!up) {
            reply.error = "connection refused";
            return reply;
        }
        reply.ok = true;
        reply.status = 200;
        reply.body = path == "/api/v1/emulator" ? list : "{}";
        return reply;
    }
};

// No emulator: the debugger waits under its dialog, keys do nothing; one appears: attached; it goes: waiting again
void TestWaitsAndReconnects() {
    auto owned = std::make_unique<FakeServer>();
    FakeServer& server = *owned;
    dbg::WebApiBackend be(std::move(owned));
    be.SetEndpoint("ws://localhost:8090");
    Check(!be.Attach(), "offline: no instance to attach to");
    Check(!be.Online(), "offline: not online");
    Check(be.WaitingFor() == "an emulator at ws://localhost:8090", "offline: the dialog names what it waits for");
    Check(!be.HasState(), "never attached: no state to paint (no made-up zeros)");

    // The whole screen paints under the dialog with nothing fetched yet (the panels index what the backend gives)
    dbg::UiState ui;
    dbg::TextScreen screen(80, 30);
    screen.Clear();
    dbg::PaintClassicFrame(screen, be, ui);
    dbg::PaintOffline(screen, be.WaitingFor());
    Check(screen.Grayed(0, 0) && screen.FrameGrayed(0), "offline: the screen and the panel frames are grayed out");
    Check(screen.AttrAt(0, 0) == dbg::kBackgr, "offline: the colours stay (the renderers dim them)");
    Check(!screen.Grayed(40, 15) && !screen.FrameGrayed(screen.Frames().size() - 1), "offline: the dialog is not");

    const int before = server.requests;
    dbg::ActionContext ctx{be, ui, false, nullptr, nullptr};
    Check(dbg::HandleKey(ctx, dbg::KeyPress{dbg::Key::F7, 0, 0}), "offline: the dialog takes the keys");
    Check(server.requests == before, "offline: a key asks the server nothing (one look a second)");

    server.list = "{\"emulators\":[{\"id\":\"e1\",\"model\":\"PENTAGON\"}]}";
    be.RetryNow();
    Check(be.PollExternalChanges(), "an emulator appears: paint again");
    Check(be.Online() && be.InstanceId() == "e1", "attached to it");

    server.list = "{\"emulators\":[]}";
    be.RetryNow();
    Check(be.PollExternalChanges(), "it goes: paint the dialog");
    Check(!be.Online(), "waiting again");
    Check(be.LastError().empty(), "reconnecting is the normal course: no error");
    Check(be.HasState(), "lost: the last state stays to paint");
    screen.Clear();
    dbg::PaintClassicFrame(screen, be, ui);
    Check(!screen.Grayed(0, 0), "a new frame starts live");

    server.list = "{\"emulators\":[{\"id\":\"e2\",\"model\":\"PENTAGON\"}]}";
    be.RetryNow();
    Check(be.PollExternalChanges() && be.Online() && be.InstanceId() == "e2", "the next one attaches");
    server.up = false;
    be.RetryNow();
    be.PollExternalChanges();
    Check(!be.Online(), "the server stops: waiting");
}

// An emulator with canned replies by route (the instance part cut off); every request is kept
class ScriptedServer : public dbg::IApiTransport {
public:
    std::map<std::string, std::pair<int, std::string>> routes;  // "GET /ports" -> status, body
    std::vector<std::string> requests;                          // "POST /ports/out {...}"
    // Asked before the routes: a status 0 passes the request on
    std::function<std::pair<int, std::string>(const std::string& route, const std::string& body)> handler;

    std::string Name() const override { return "scripted"; }
    void SetTimeoutSeconds(int) override {}
    dbg::HttpReply Request(const std::string& method, const std::string& path, const std::string& body) override {
        dbg::HttpReply reply;
        reply.ok = true;
        reply.status = 200;
        if (path == "/api/v1/emulator") {
            reply.body = "{\"emulators\":[{\"id\":\"e1\",\"model\":\"PENTAGON\"}]}";
            return reply;
        }
        const std::string prefix = "/api/v1/emulator/e1";
        const std::string route = method + " " + (path.compare(0, prefix.size(), prefix) == 0 ? path.substr(prefix.size()) : path);
        requests.push_back(route + (body.empty() ? "" : " " + body));
        if (handler) {
            const auto answer = handler(route, body);
            if (answer.first != 0) {
                reply.status = answer.first;
                reply.body = answer.second;
                return reply;
            }
        }
        if (route.compare(0, 14, "GET /memory/0x") == 0) {
            reply.body = "{\"data\":[";
            for (int i = 0; i < 4096; ++i) reply.body += i == 0 ? "7" : ",7";
            reply.body += "]}";
            return reply;
        }
        const auto it = routes.find(route);
        if (it != routes.end()) {
            reply.status = it->second.first;
            reply.body = it->second.second;
        } else {
            reply.body = "{}";
        }
        return reply;
    }
    bool Asked(const std::string& start) const {
        for (const std::string& r : requests)
            if (r.compare(0, start.size(), start) == 0) return true;
        return false;
    }
};

// The panels read the emulator: T and HALT, the port latches, Beta 128, the AY; the writes go to it (a port with its
// effects, IM); a condition becomes the emulator's breakpoint, of the kind its operands need
void TestReadsTheEmulator() {
    auto owned = std::make_unique<ScriptedServer>();
    ScriptedServer& server = *owned;
    server.routes["GET /registers"] = {
        200,
        "{\"main\":{\"af\":0,\"bc\":0,\"de\":0,\"hl\":0},\"alternate\":{\"af_\":0,\"bc_\":0,\"de_\":0,\"hl_\":0},"
        "\"index\":{\"ix\":0,\"iy\":0},\"special\":{\"pc\":32768,\"sp\":65000,\"i\":63,\"r\":0,\"t\":70000},"
        "\"interrupt\":{\"iff1\":1,\"iff2\":1,\"im\":1,\"halted\":true}}"};
    server.routes["GET /video/beam"] = {200, "{\"frame\":3,\"frame_timing\":{\"frame_tstates\":71680}}"};
    server.routes["GET /ports"] = {
        200, "{\"entries\":[{\"port\":\"0x00FE\",\"latch_value\":250},{\"port\":\"0xFFFD\"},"
             "{\"port\":\"0x7FFD\",\"latch_value\":48},{\"port\":\"0x1FFD\",\"latch_value\":2}]}"};
    server.routes["GET /state/paging"] = {200, "{\"trdos_active\":true,\"banks\":[]}"};
    server.routes["GET /state/fdc"] = {
        200, "{\"available\":true,\"registers\":{\"command\":136,\"data\":9,\"sector\":3,\"status\":129,\"track\":5},"
             "\"beta128_register\":60,\"signals\":{\"intrq\":true,\"drq\":false},\"selected_drive\":1,"
             "\"drives\":[{\"track\":0},{\"track\":6}]}"};
    server.routes["GET /state/audio/ay"] = {200, "{\"available\":true,\"turbo_sound\":false,\"active_chip\":0}"};
    server.routes["GET /state/audio/ay/0"] = {
        200, "{\"latched_register\":13,\"registers\":{\" R7 - Mixer Control Enable\":56,"
             "\"R10 - Channel A - Amplitude\":15,\"R15 - Envelope shape\":10}}"};
    dbg::WebApiBackend be(std::move(owned));
    Check(be.Attach(), "scripted: attached");

    const dbg::Z80Regs regs = be.GetRegs(0);
    Check(regs.t == 70000 && regs.halted, "registers: the T-state in the frame and HALT");
    Check(be.GetAbsoluteT(0) == 3u * 71680u + 70000u, "time: the frame from the beam times its length, plus T");
    be.SetDebugMark(0, 3u * 71680u);
    Check(be.GetTimeDelta(0) == 70000, "time delta: from the mark");
    Check(be.GetMachineInfo().frameLength == 71680, "frame length: the beam's");

    const dbg::PortsState ports = be.GetPorts();
    Check(ports.fe == 250 && ports.p7ffd == 48 && ports.lock48, "ports: #FE and #7FFD as latched");
    Check(ports.hasExtPort && ports.extPort == 0x1FFD && ports.extValue == 2, "ports: the model's extended port");
    Check(ports.dosPortsActive, "ports: the DOS ROM from the paging");

    const dbg::Beta128State fdc = be.GetBeta128();
    Check(fdc.present && fdc.cmd == 136 && fdc.statusRaw == 129 && fdc.track == 5 && fdc.sector == 3,
          "beta128: the WD1793's registers");
    Check(fdc.system == 60 && fdc.rqs == 0x80 && fdc.headTrack == 6, "beta128: system, INTRQ, the drive's head");

    const dbg::AyState ay = be.GetAy();
    Check(ay.scheme == dbg::AyScheme::Single && ay.latchedReg[0] == 13, "ay: one chip, its latched register");
    Check(ay.regs[0][7] == 56 && ay.regs[0][8] == 15 && ay.regs[0][13] == 10, "ay: the registers (octal names)");

    be.Out(0x7FFD, 0x17);
    Check(server.Asked("POST /ports/out {\"port\":\"0x7FFD\",\"value\":\"0x17\"}"), "out: to the emulator's decoder");
    be.SetExtPort(0x04);
    Check(server.Asked("POST /ports/out {\"port\":\"0x1FFD\",\"value\":\"0x04\"}"), "ext port: the model's one");
    be.WriteReg(0, dbg::IDebuggerBackend::RegField::Im, 2);
    Check(server.Asked("PUT /registers/IM {\"value\":2}"), "IM: written");

    // RD needs a read breakpoint: the execution one is refused, the read one taken
    bool readTaken = false;
    server.handler = [&readTaken](const std::string& route, const std::string& body) -> std::pair<int, std::string> {
        if (route != "POST /breakpoints") return {0, ""};
        if (readTaken && body.find("\"type\":\"read\"") != std::string::npos) return {200, "{\"id\":41}"};
        return {400, "{\"message\":\"condition: RD is not supplied by this breakpoint's trigger at column 1\"}"};
    };
    dbg::ExprCompileResult added = be.AddCondition(0, "RD == #5C3A");
    Check(!added.ok && added.error.find("not supplied") != std::string::npos, "condition: no kind supplies RD");
    readTaken = true;
    added = be.AddCondition(0, "RD == #5C3A");
    Check(added.ok && be.ConditionCount(0) == 1, "condition: the emulator's breakpoint");
    Check(server.Asked("POST /breakpoints {\"type\":\"exec\",\"address\":0,\"address_end\":65535,\"condition\":\"RD == #5C3A\""),
          "condition: over the whole 64K");
    be.DeleteCondition(0, 0);
    Check(server.Asked("DELETE /breakpoints/41") && be.ConditionCount(0) == 0, "condition: deleted there");
    added = be.AddCondition(0, "A == 10 && PG0 == 0ff && C == 'x'");
    Check(added.ok && server.Asked("POST /breakpoints {\"type\":\"read\",\"address\":0,\"address_end\":65535,"
                                        "\"condition\":\"A == #10 && PG0 == #0FF && C == 'x'\""),
          "condition: the shared language's hex numbers marked for the emulator");
    be.DeleteCondition(0, 0);
    server.handler = [](const std::string& route, const std::string&) -> std::pair<int, std::string> {
        if (route != "POST /breakpoints") return {0, ""};
        return {400, "{\"message\":\"condition: an operand expected at column 5\"}"};
    };
    added = be.AddCondition(0, "A ==");
    Check(!added.ok && !added.error.empty(), "condition: a syntax error answered here");
}

// The waiting screen as the window draws it: at start (nothing read: no panels) and after a loss (the last state)
void TestOfflineLook(const std::string& shots) {
    const auto check = [](bool ok, const std::string& what) { Check(ok, what); };
    dbg::TextScreen start(80, 30);
    start.Clear();
    dbg::PaintOffline(start, "an emulator at ws://127.0.0.1:18700");
    dbg::testing::CheckOfflineLook(start, "classic-start", false, shots, check);

    dbg::MockBackend be(dbg::CreateTestSetByName("golden-sample"));
    dbg::UiState ui;
    dbg::TextScreen lost(80, 30);
    lost.Clear();
    dbg::PaintClassicFrame(lost, be, ui);
    dbg::PaintOffline(lost, "an emulator at ws://127.0.0.1:18700");
    dbg::testing::CheckOfflineLook(lost, "classic-lost", true, shots, check);
}

}  // namespace

// --shots <folder>: also write the waiting screens as PPM pictures there
int main(int argc, char** argv) {
    const std::string shots = argc == 3 && std::string(argv[1]) == "--shots" ? argv[2] : "";
    TestWidgetTraversal();
    TestVisualFocusBackgrounds();
    TestReadOnlyWidgets();
    TestFieldNavigation();
    TestEditingLifecycle();
    TestMouseInteractivity();
    TestUnrealKeys();
    TestTerminalKeys();
    TestWaitsAndReconnects();
    TestReadsTheEmulator();
    TestOfflineLook(shots);

    if (failures == 0) {
        std::printf("all classic tests passed\n");
    } else {
        std::printf("%d classic failure(s)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
