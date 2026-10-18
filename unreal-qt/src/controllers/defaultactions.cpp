#include "defaultactions.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QObject>

#include <memory>

#include "controllers/actionregistry.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"

namespace
{
QString Tr(const char* text)
{
    return QObject::tr(text);
}
}  // namespace

void RegisterDefaultActions(ActionRegistry& registry, EmulatorBinding* binding, DefaultActionHooks hooks)
{
    auto noMachine = [binding]() -> QString { return binding->emulator() == nullptr ? Tr("no machine") : QString(); };
    auto needsPause = [binding]() -> QString {
        Emulator* emulator = binding->emulator();
        if (emulator == nullptr)
            return Tr("no machine");
        return emulator->IsPaused() ? QString() : Tr("needs a pause");
    };
    auto slot = [hooks](const char* name) {
        return [debugger = hooks.debugger, name](bool) -> QString {
            if (debugger == nullptr || !QMetaObject::invokeMethod(debugger, name))
                return Tr("the debugger is not available");
            return {};
        };
    };
    auto add = [&registry](const char* id, const QString& label, const QString& group,
                           std::function<QString(bool)> run, std::function<QString()> unavailable,
                           bool held = false) {
        registry.Register({id, label, group, std::move(run), std::move(unavailable), held});
    };

    // 3.1 Run control
    const QString run = Tr("Run");
    add("run.pause_toggle", Tr("Pause / continue"), run,
        [binding, hooks](bool) -> QString {
            Emulator* emulator = binding->emulator();
            if (hooks.debugger == nullptr || emulator == nullptr)
                return Tr("no machine");
            QMetaObject::invokeMethod(hooks.debugger, emulator->IsPaused() ? "continueExecution" : "pauseExecution");
            return {};
        },
        noMachine);
    add("run.step_in", Tr("Step in"), run, slot("stepIn"), needsPause);
    add("run.step_over", Tr("Step over"), run, slot("stepOver"), needsPause);
    add("run.step_out", Tr("Step out"), run, slot("stepOut"), needsPause);
    add("run.frame_step", Tr("Frame step"), run, slot("frameStep"), needsPause);
    add("run.wait_int", Tr("Run to interrupt"), run, slot("runToInterrupt"), needsPause);
    add("run.to_scanline", Tr("Run to scanline"), run, slot("runToScanline"), needsPause);
    add("run.n_scanlines", Tr("Run N scanlines"), run, slot("runNScanlines"), needsPause);
    add("run.turbo_hold", Tr("Turbo while held"), run,
        [binding](bool pressed) -> QString {
            Emulator* emulator = binding->emulator();
            if (emulator == nullptr)
                return Tr("no machine");
            if (pressed)
                emulator->EnableTurboMode();
            else
                emulator->DisableTurboMode();
            return {};
        },
        noMachine, true);

    // 3.2 Time travel: the TTDControl verbs
    const QString ttd = Tr("Time travel");
    auto verb = [binding](const char* name, std::map<std::string, std::string> options = {}) {
        return [binding, name, options](bool) -> QString {
            EmulatorContext* context = binding->context();
            if (context == nullptr)
                return Tr("no machine");
            const ttd::TTDReply reply = ttd::TTDControl(context).Execute({name, options});
            return reply.Ok() ? QString() : QString::fromStdString(reply.message);
        };
    };
    add("ttd.step_back_frame", Tr("Back one frame"), ttd, verb("step-back"), noMachine);
    add("ttd.step_forward_frame", Tr("Forward one frame"), ttd, verb("step-forward"), noMachine);
    add("ttd.step_back_instruction", Tr("Back one instruction"), ttd, verb("step-instruction", {{"dir", "back"}}),
        noMachine);
    add("ttd.step_forward_instruction", Tr("Forward one instruction"), ttd,
        verb("step-instruction", {{"dir", "forward"}}), noMachine);
    add("ttd.reverse_step", Tr("Reverse step"), ttd, verb("reverse-step"), noMachine);
    add("ttd.reverse_continue", Tr("Reverse continue"), ttd, verb("reverse-continue"), noMachine);
    add("ttd.bookmark_add", Tr("Add a bookmark"), ttd, verb("bookmark-add"), noMachine);
    add("ttd.resume_live", Tr("Resume live"), ttd, verb("resume"), noMachine);
    add("ttd.record_toggle", Tr("Start / stop recording"), ttd,
        [binding](bool) -> QString {
            EmulatorContext* context = binding->context();
            if (context == nullptr)
                return Tr("no machine");
            const bool recording = context->pTimeTravelController != nullptr &&
                                   context->pTimeTravelController->GetState() == ttd::TTDSessionState::Recording;
            const ttd::TTDReply reply = ttd::TTDControl(context).Execute({recording ? "stop" : "start", {}});
            return reply.Ok() ? QString() : QString::fromStdString(reply.message);
        },
        noMachine);
    const QString later = Tr("not available yet");
    for (const char* id : {"ttd.scrub_back", "ttd.scrub_forward", "ttd.find_last_write", "ttd.bookmark_prev",
                           "ttd.bookmark_next", "ttd.step_back_scanline"})
        add(id, QString::fromLatin1(id), ttd, nullptr, [later] { return later; });

    // 3.3 Debugger UI
    const QString debug = Tr("Debugger");
    add("dbg.open", Tr("Open the debugger"), debug,
        [hooks](bool) -> QString {
            if (hooks.showDebugger)
                hooks.showDebugger();
            return {};
        },
        nullptr);
    for (const char* id : {"dbg.focus_next_pane", "dbg.focus_prev_pane", "dbg.breakpoint_toggle_pc",
                           "dbg.memory_follow", "dbg.cursor_up", "dbg.cursor_down", "dbg.labels_toggle"})
        add(id, QString::fromLatin1(id), debug, nullptr, [later] { return later; });

    // 3.4 Capture and machine
    const QString machine = Tr("Capture and machine");
    add("cap.screenshot", Tr("Screenshot"), machine,
        [hooks](bool) -> QString {
            if (hooks.screenshot)
                hooks.screenshot();
            return {};
        },
        noMachine);
    // Reset fires on a release after a 1 s hold, so it is never an accident
    auto held = std::make_shared<QElapsedTimer>();
    add("mach.reset", Tr("Reset (hold 1 s)"), machine,
        [binding, held](bool pressed) -> QString {
            if (pressed)
            {
                held->start();
                return {};
            }
            if (!held->isValid() || held->elapsed() < 1000)
                return {};
            held->invalidate();
            if (Emulator* emulator = binding->emulator())
                emulator->Reset();
            return {};
        },
        noMachine, true);
    add("mach.nmi", Tr("NMI"), machine,
        [binding](bool) -> QString {
            if (Emulator* emulator = binding->emulator())
                emulator->RequestNMI();
            return {};
        },
        noMachine);
    add("mach.turbo_toggle", Tr("Turbo"), machine,
        [binding](bool) -> QString {
            Emulator* emulator = binding->emulator();
            if (emulator == nullptr)
                return Tr("no machine");
            if (emulator->IsTurboMode())
                emulator->DisableTurboMode();
            else
                emulator->EnableTurboMode();
            return {};
        },
        noMachine);
    for (const char* id : {"cap.record_toggle", "cap.clip_last", "osd.keyboard", "osd.quick_menu"})
        add(id, QString::fromLatin1(id), machine, nullptr, [later] { return later; });
}
