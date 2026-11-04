// main.cpp - dbgtsconf entry point: backend + FTXUI terminal loop (fork, 157x30).
//
// Keys are UnrealSpeccy's (unreal.ini [SYSTEM.KEYS], ui/actions.h): F7 step, F8 step over, F11 exit subroutine,
// F4 run to cursor, ` continue / pause, Space breakpoint, G go to, Home cursor to PC, Z PC to cursor, Tab next
// window; F10 opens the system menu with every command and its key.
//   --backend ws|rest|mock [--endpoint <url> | --port <n>] [--emulator <id>]  (default: ws://localhost:8090)
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>

#include "backend/debugger-backend.h"
#include "backend/mock-backend.h"
#include "gui/sdl-info.h"
#include "gui/sdl-runner.h"
#include "panels/paint-tsconf.h"
#include "panels/paint.h"
#include "screen/palette.h"
#include "screen/textscreen.h"
#include "tui/frame-render.h"
#include "tui/key-input.h"
#include "ui/actions.h"
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
        if (be->HasState()) {
            dbg::PaintTsconfFrame(screen, *be, ui);
            dbg::PaintSystemMenu(screen, ui, true, be->IsRunning());
        }
        if (!be->Online()) dbg::PaintOffline(screen, be->WaitingFor());
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
    BackendKind kind = BackendKind::WebSocket;  // a running unreal-ng on this host
    std::string endpoint;
    std::string emulator;
    int port = 0;
    bool forceTui = false;
    float scale = 1.5f;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--backend" && i + 1 < argc) {
            const std::string name = argv[++i];
            if (name == "rest") kind = BackendKind::Rest;
            else if (name == "ws") kind = BackendKind::WebSocket;
            else if (name == "ipc") kind = BackendKind::Ipc;
            else if (name == "mock") kind = BackendKind::Mock;
            else {
                std::fprintf(stderr, "unknown backend '%s': ws, rest or mock\n", name.c_str());
                return 2;
            }
        } else if (arg == "--endpoint" && i + 1 < argc) {
            endpoint = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
            if (port < 1 || port > 65535) {
                std::fprintf(stderr, "bad port '%s': 1..65535\n", argv[i]);
                return 2;
            }
        } else if (arg == "--emulator" && i + 1 < argc) {
            emulator = argv[++i];
        } else if (arg == "--scale" && i + 1 < argc) {
            char* end = nullptr;
            const float val = std::strtof(argv[++i], &end);
            if (end != argv[i] && val >= 0.5f && val <= 5.0f) {
                scale = val;
            }
        } else if (arg == "--tui" || arg == "--terminal") {
            forceTui = true;
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: %s [--backend ws|rest|mock] [--endpoint <url> | --port <n>] [--emulator <id>] [--tui]\n"
                        "       [--scale <0.5..5>]\n"
                        "  --backend   ws: a running unreal-ng through its WebSocket (default, ws://localhost:8090)\n"
                        "              rest: the same through REST requests (http://localhost:8090)\n"
                        "              mock: the built-in Z80 machine of the tests\n"
                        "  --port      ws, rest: the emulator on this host at that port (its UNREAL_WEBAPI_PORT)\n"
                        "  --emulator  ws, rest: only this instance (its id or symbolic id), else the first\n"
                        "  --tui       the terminal even when the pixel window can open\n"
                        "pixel window: %s\n",
                        argv[0], dbg::SdlWindowInfo().c_str());
            return 0;
        }
    }

    if (port != 0 && endpoint.empty())
        endpoint = (kind == BackendKind::Rest ? "http://localhost:" : "ws://localhost:") + std::to_string(port);

    TuiApp app;
    app.be = dbg::CreateBackend(kind, endpoint, emulator);
    if (!app.be) {
        if (kind == BackendKind::Ipc)  // a remote backend that did not attach has said why
            std::fprintf(stderr, "no IPC transport: use --backend ws, rest or mock\n");
        return 2;
    }
    app.ApplySeed();
    app.ui.traceCurs = app.be->GetRegs(0).pc;
    app.FollowCursor();  // the listing opens at the PC
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
        // The ticker's wake-up: another client of the emulator stepped or stopped it, or changed its breakpoints
        if (e == ftxui::Event::Custom) {
            if (app.be->PollExternalChanges()) {
                app.ui.traceCurs = app.be->GetRegs(0).pc;
                app.FollowCursor();
                app.Repaint();
            }
            return true;
        }
        dbg::ActionContext ctx{*app.be, app.ui, true, [&] { app.FollowCursor(); }, [&] { terminal.Exit(); }};
        if (e.is_mouse()) {
            if (e.mouse().button == ftxui::Mouse::Left && e.mouse().motion == ftxui::Mouse::Pressed) {
                if (dbg::HandleMenuClick(ctx, e.mouse().x, e.mouse().y) ||
                    dbg::HandleMouseClick(*app.be, app.ui, e.mouse().x, e.mouse().y, true)) {
                    if (app.ui.activeWidget == dbg::WidgetId::Trace) app.FollowCursor();
                    app.Repaint();
                    return true;
                }
            }
            return false;
        }
        if (dbg::HandleKey(ctx, dbg::KeyFromTerminal(e))) {
            app.Repaint();
            return true;
        }
        return false;
    });

    // The loop waits for input: a ticker wakes it ten times a second to look at the emulator
    std::atomic<bool> done{false};
    std::thread ticker([&] {
        while (!done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!done) terminal.PostEvent(ftxui::Event::Custom);
        }
    });
    terminal.Loop(body);
    done = true;
    ticker.join();
    return 0;
}
