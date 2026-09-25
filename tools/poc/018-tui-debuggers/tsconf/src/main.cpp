// main.cpp - dbgtsconf entry point: backend + FTXUI terminal loop (fork, 157x30).
//
// Command slice (the input milestone wires the full §9 key map):
//   F5/r run    F11/s step    F9/b toggle BPX at trace cursor
//   ` (TIL) continue-until-break    Up/Down move trace cursor
//   1/2/3/4 focus regs/trace/mem/banks    Tab next window (§3)
//   t reset time-delta mark    q/Esc/F10 quit
//   mouse click on the board selects the control (visual-only, §5.3)
//   --backend mock|rest|ws|ipc [--endpoint <url/pipe>]
#include <cstdio>
#include <memory>
#include <string>

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>

#include "backend/debugger-backend.h"
#include "backend/mock-backend.h"
#include "gui/sdl-runner.h"
#include "panels/paint-tsconf.h"
#include "panels/paint.h"
#include "screen/palette.h"
#include "screen/textscreen.h"
#include "tui/frame-render.h"
#include "ui/keymap.h"

namespace {

using dbg::BackendKind;
using dbg::IDebuggerBackend;
using dbg::Palette;
using dbg::TextScreen;
using dbg::UiState;

constexpr int kWidth = 157;  // §1: TSConf buffer 157x30 (88 visible off-model)

struct TuiApp {
    std::unique_ptr<IDebuggerBackend> be;
    UiState ui;
    TextScreen screen{kWidth, 30, 50};  // fork frame-list capacity: 50 (§0.2)
    Palette palette{dbg::Theme::UnrealClassic};

    void Repaint() {
        screen.Clear();
        dbg::PaintTsconfFrame(screen, *be, ui);
    }

    // Keep the trace cursor visible: reset the window origin when the PC or
    // the cursor walks out of the painted range.
    void FollowCursor() {
        const std::vector<dbg::DisasmLine> lines = be->Disassemble(0, ui.traceTop, 21, false);
        if (lines.empty()) {
            ui.traceTop = ui.traceCurs;
            return;
        }
        const uint16_t last = lines.back().addr;
        const uint16_t curs = ui.traceCurs;
        if (curs < ui.traceTop || (curs > last && last >= ui.traceTop)) {
            ui.traceTop = curs;  // wrap-around handled by the range check above
        }
    }

    void ApplySeed() {
        // Golden-sample UI seed (focus/cursors the dumps assume); other
        // backends start from the defaults above.
        if (be->Name() != "mock") return;
        dbg::MockBackend* mock = dynamic_cast<dbg::MockBackend*>(be.get());
        if (mock == nullptr) return;
        const dbg::UiStateSeed& seed = mock->UiSeed();
        ui.traceTop = seed.traceTop;
        ui.traceCurs = seed.traceCurs;
        ui.memTop = seed.memTop;
        ui.memCurs = seed.memCurs;
        ui.activeWindow = seed.activeWindow;
        ui.selBank = seed.selBank;
        ui.showBank = seed.showBank;
    }

    // §3: focus cycle REGS -> TRACE -> MEM -> BANKS; showBank tracks BANKS.
    void SetFocus(int window) {
        ui.activeWindow = window;
        ui.showBank = window == 3;
    }
};

}  // namespace

int main(int argc, char** argv) {
    BackendKind kind = BackendKind::Mock;
    std::string endpoint;
    bool forceTui = false;
    float scale = 1.5f;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--backend" && i + 1 < argc) {
            const std::string name = argv[++i];
            if (name == "rest") kind = BackendKind::Rest;
            else if (name == "ws") kind = BackendKind::WebSocket;
            else if (name == "ipc") kind = BackendKind::Ipc;
            else kind = BackendKind::Mock;
        } else if (arg == "--endpoint" && i + 1 < argc) {
            endpoint = argv[++i];
        } else if (arg == "--scale" && i + 1 < argc) {
            char* end = nullptr;
            const float val = std::strtof(argv[++i], &end);
            if (end != argv[i] && val >= 0.5f && val <= 5.0f) {
                scale = val;
            }
        } else if (arg == "--tui" || arg == "--terminal") {
            forceTui = true;
        }
    }

    TuiApp app;
    app.be = dbg::CreateBackend(kind, endpoint);
    if (!app.be) {
        std::fprintf(stderr, "backend not wired yet (kind %d) - use --backend mock\n",
                     static_cast<int>(kind));
        return 2;
    }
    app.ApplySeed();
    app.ui.traceCurs = app.be->GetRegs(0).pc;
    app.Repaint();

    if (!forceTui) {
        dbg::SdlRunnerConfig sdlConfig;
        sdlConfig.scale = scale;
        if (dbg::RunSdlWindow(*app.be, app.ui, app.screen, app.palette,
                              [&]() { app.Repaint(); },
                              [&]() { app.FollowCursor(); },
                              [&](int w) { app.SetFocus(w); },
                              sdlConfig)) {
            return 0;
        }
        std::fprintf(stderr, "SDL3 window failed to open, falling back to terminal TUI mode\n");
    }

    ftxui::App terminal = ftxui::App::FullscreenAlternateScreen();
    ftxui::Component body = ftxui::Renderer([&] { return dbg::RenderFrame(app.screen, app.palette); });
    body |= ftxui::CatchEvent([&](ftxui::Event e) {
        if (e.is_mouse()) {
            if (e.mouse().button == ftxui::Mouse::Left && e.mouse().motion == ftxui::Mouse::Pressed) {
                if (dbg::HandleMouseClick(*app.be, app.ui, e.mouse().x, e.mouse().y, true)) {
                    if (app.ui.activeWidget == dbg::WidgetId::Trace) app.FollowCursor();
                    app.Repaint();
                    return true;
                }
            }
        }
        const bool isChar = e.is_character();
        const std::string& ch = e.character();
        if (e == ftxui::Event::Escape) {
            if (dbg::HandleEscapeKey(app.ui)) {
                app.Repaint();
                return true;
            }
            terminal.Exit();
            return true;
        }
        if (e == ftxui::Event::F10 || (isChar && ch == "q" && !app.ui.isEditing)) {
            terminal.Exit();
            return true;
        }
        if (e == ftxui::Event::Tab) {
            dbg::FocusNextWidget(app.ui, true);
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::TabReverse) {
            dbg::FocusPrevWidget(app.ui, true);
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::ArrowUp) {
            dbg::HandleArrowKey(*app.be, app.ui, dbg::NavKey::Up, true);
            if (app.ui.activeWidget == dbg::WidgetId::Trace) app.FollowCursor();
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::ArrowDown) {
            dbg::HandleArrowKey(*app.be, app.ui, dbg::NavKey::Down, true);
            if (app.ui.activeWidget == dbg::WidgetId::Trace) app.FollowCursor();
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::ArrowLeft) {
            dbg::HandleArrowKey(*app.be, app.ui, dbg::NavKey::Left, true);
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::ArrowRight) {
            dbg::HandleArrowKey(*app.be, app.ui, dbg::NavKey::Right, true);
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::PageUp) {
            dbg::HandleArrowKey(*app.be, app.ui, dbg::NavKey::PgUp, true);
            if (app.ui.activeWidget == dbg::WidgetId::Trace) app.FollowCursor();
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::PageDown) {
            dbg::HandleArrowKey(*app.be, app.ui, dbg::NavKey::PgDn, true);
            if (app.ui.activeWidget == dbg::WidgetId::Trace) app.FollowCursor();
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::Return) {
            dbg::HandleEnterKey(*app.be, app.ui);
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::Backspace) {
            if (dbg::HandleBackspaceKey(app.ui)) {
                app.Repaint();
                return true;
            }
        }
        if (isChar && ch == " ") {
            if (dbg::HandleSpaceKey(*app.be, app.ui)) {
                app.Repaint();
                return true;
            }
        }
        if (isChar && (ch == "m" || ch == "M") && app.ui.activeWidget == dbg::WidgetId::Memory && !app.ui.isEditing) {
            app.ui.memAscii = !app.ui.memAscii;
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::F5 || (isChar && ch == "r" && !app.ui.isEditing) || (isChar && ch == "`")) {
            app.be->RunUntilBreak(0, {});
            app.ui.traceCurs = app.be->GetRegs(0).pc;
            app.FollowCursor();
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::F11 || (isChar && ch == "s" && !app.ui.isEditing)) {
            app.be->Step(0);
            app.ui.traceCurs = app.be->GetRegs(0).pc;
            app.FollowCursor();
            app.Repaint();
            return true;
        }
        if (e == ftxui::Event::F9 || (isChar && ch == "b" && !app.ui.isEditing)) {
            const uint8_t bits = app.be->BpBitsAt(app.ui.traceCurs);
            app.be->SetBpBits(app.ui.traceCurs, static_cast<uint8_t>(bits ^ 1));
            app.Repaint();
            return true;
        }

        if (isChar && ch.size() == 1) {
            if (dbg::HandleCharInput(*app.be, app.ui, ch[0])) {
                app.Repaint();
                return true;
            }
        }
        return false;
    });

    terminal.Loop(body);
    return 0;
}
