#include "defaultactions.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QObject>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "controllers/actionregistry.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "router/analog.h"

namespace
{
QString Tr(const char* text)
{
    return QObject::tr(text);
}

struct Place
{
    int64_t frame = 0;
    int64_t tinframe = 0;
    bool operator==(const Place& other) const
    {
        return frame == other.frame && tinframe == other.tinframe;
    }
};

/// The position verb's place and the history's end frame; false without a session
bool Position(EmulatorContext* context, Place& place, int64_t& end)
{
    const ttd::TTDReply reply = ttd::TTDControl(context).Execute({"position", {}});
    const StateNode* current = reply.body.find("current");
    const StateNode* last = reply.body.find("session_end");
    const StateNode* frame = current != nullptr ? current->find("frame") : nullptr;
    const StateNode* tinframe = current != nullptr ? current->find("tinframe") : nullptr;
    const StateNode* lastFrame = last != nullptr ? last->find("frame") : nullptr;
    if (!reply.Ok() || frame == nullptr || tinframe == nullptr || lastFrame == nullptr)
        return false;
    place = {frame->i, tinframe->i};
    end = lastFrame->i;
    return true;
}

/// Move @p frames through the history (back < 0), stopping at its ends: "" when it moved, else why not.
/// A seek to frame N stands at N's end, which the position names as the start of N + 1: the last move's frame is
/// kept with the place it left, so the next move counts from N, not from N + 1 (else a step back would not move)
QString MoveFrames(EmulatorContext* context, int64_t frames)
{
    struct Last
    {
        EmulatorContext* context = nullptr;
        Place place;
        int64_t frame = 0;
    };
    static Last last;
    Place place;
    int64_t end = 0;
    if (!Position(context, place, end))
        return Tr("no time travel history");
    const int64_t from = last.context == context && last.place == place ? last.frame : place.frame;
    // The end of frame end - 1 is the end of the history: frames past it are the same place
    const int64_t target = std::clamp<int64_t>(from + frames, 0, std::max<int64_t>(0, end - 1));
    if (target == from)
        return frames < 0 ? Tr("the start of the history") : Tr("the end of the history");
    const ttd::TTDReply reply =
        ttd::TTDControl(context).Execute({"seek", {{"frame", std::to_string(target)}}});
    if (!reply.Ok())
        return QString::fromStdString(reply.message);
    const StateNode* reached = reply.body.find("reached");
    if (reached != nullptr && reached->kind == StateNode::Kind::Bool && !reached->b)
        return frames < 0 ? Tr("the start of the history") : Tr("the end of the history");
    if (Position(context, place, end))
        last = {context, place, target};
    return {};
}

/// ttd.scrub_back / ttd.scrub_forward (debug-and-ttd.md §3.2): each level is a speed on the scrub curve, both held
/// count against each other; a 20 ms timer moves by the frames the speed covered. It stops on the release, and at
/// either end of the history or on a refusal with the reason
class Scrub
{
public:
    Scrub(EmulatorBinding* binding, std::function<void(const QString&)> notice)
        : _binding(binding), _notice(std::move(notice))
    {
        _timer.setInterval(20);
        QObject::connect(&_timer, &QTimer::timeout, [this] { Tick(); });
    }

    QString Set(bool forward, float level)
    {
        if (_binding->context() == nullptr)
            return Tr("no machine");
        (forward ? _forward : _back) = level;
        if (_back == 0 && _forward == 0)
        {
            Stop();
            return {};
        }
        if (!_timer.isActive())
        {
            _owed = 0;
            _clock.start();
            _timer.start();
        }
        return {};
    }

private:
    void Stop()
    {
        _timer.stop();
        _owed = 0;
    }

    void Tick()
    {
        const double seconds = static_cast<double>(_clock.restart()) / 1000.0;
        _owed += (controllers::ScrubSpeed(_forward) - controllers::ScrubSpeed(_back)) * seconds;
        const auto frames = static_cast<int64_t>(_owed);  // toward zero: the rest waits for the next tick
        if (frames == 0)
            return;
        _owed -= static_cast<double>(frames);
        EmulatorContext* context = _binding->context();
        const QString why = context != nullptr ? MoveFrames(context, frames) : Tr("no machine");
        if (!why.isEmpty())
        {
            // Held on: it starts again with the next change of the level
            Stop();
            if (_notice)
                _notice(Tr("Scrub stopped: %1").arg(why));
        }
    }

    EmulatorBinding* _binding = nullptr;
    std::function<void(const QString&)> _notice;
    QTimer _timer;
    QElapsedTimer _clock;
    float _back = 0;
    float _forward = 0;
    double _owed = 0;  ///< frames covered and not moved yet (back < 0)
};
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
        registry.Register({id, label, group, std::move(run), std::move(unavailable), held, nullptr});
    };
    auto addAmount = [&registry](const char* id, const QString& label, const QString& group,
                                 std::function<QString(float)> amount, std::function<QString()> unavailable) {
        registry.Register({id, label, group, nullptr, std::move(unavailable), false, std::move(amount)});
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
    // Analog: a trigger's level (`curve: scrub`); a button press is the full level
    auto scrub = std::make_shared<Scrub>(binding, hooks.notice);
    addAmount("ttd.scrub_back", Tr("Scrub back"), ttd, [scrub](float level) { return scrub->Set(false, level); },
              noMachine);
    addAmount("ttd.scrub_forward", Tr("Scrub forward"), ttd,
              [scrub](float level) { return scrub->Set(true, level); }, noMachine);
    // An encoder's turn (the Surface Dial): a frame or an instruction per step, clockwise forward
    addAmount("ttd.jog_frames", Tr("Jog frames"), ttd,
              [binding](float steps) -> QString {
                  EmulatorContext* context = binding->context();
                  if (context == nullptr)
                      return Tr("no machine");
                  const auto frames = static_cast<int64_t>(std::lround(steps));
                  return frames == 0 ? QString() : MoveFrames(context, frames);
              },
              noMachine);
    addAmount("ttd.jog_instructions", Tr("Jog instructions"), ttd,
              [binding](float steps) -> QString {
                  EmulatorContext* context = binding->context();
                  if (context == nullptr)
                      return Tr("no machine");
                  const long count = std::min(64L, std::labs(std::lround(steps)));  // a fast spin: 64 at most
                  for (long i = 0; i < count; i++)
                  {
                      const ttd::TTDReply reply = ttd::TTDControl(context).Execute(
                          {"step-instruction", {{"dir", steps < 0 ? "back" : "forward"}}});
                      if (!reply.Ok())
                          return QString::fromStdString(reply.message);
                  }
                  return {};
              },
              noMachine);
    const QString later = Tr("not available yet");
    for (const char* id : {"ttd.find_last_write", "ttd.bookmark_prev", "ttd.bookmark_next", "ttd.step_back_scanline"})
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
    // Pane focus opens a closed debugger first; the cursor keys go to the pane in focus and repeat while held
    auto pane = [hooks](const char* name) {
        return [hooks, name](bool) -> QString {
            auto* window = qobject_cast<QWidget*>(hooks.debugger);
            if (window == nullptr)
                return Tr("the debugger is not available");
            if (!window->isVisible() && hooks.showDebugger)
                hooks.showDebugger();
            QMetaObject::invokeMethod(window, name);
            return {};
        };
    };
    auto noDebugger = [hooks]() -> QString {
        return hooks.debugger == nullptr ? Tr("the debugger is not available") : QString();
    };
    auto key = [hooks](int code, bool repeats) {
        auto timer = std::make_shared<QTimer>();
        auto send = [debugger = hooks.debugger, code] {
            if (debugger != nullptr)
                QMetaObject::invokeMethod(debugger, "paneKey", Q_ARG(int, code));
        };
        QObject::connect(timer.get(), &QTimer::timeout, [timer = timer.get(), send] {
            timer->setInterval(60);
            send();
        });
        return [timer, send, repeats](bool pressed) -> QString {
            timer->stop();
            if (!pressed)
                return {};
            send();
            if (repeats)
                timer->start(300);
            return {};
        };
    };
    add("dbg.focus_next_pane", Tr("Next debugger pane"), debug, pane("focusNextPane"), noDebugger);
    add("dbg.focus_prev_pane", Tr("Previous debugger pane"), debug, pane("focusPrevPane"), noDebugger);
    add("dbg.cursor_up", Tr("Cursor up in the pane"), debug, key(Qt::Key_Up, true), noDebugger, true);
    add("dbg.cursor_down", Tr("Cursor down in the pane"), debug, key(Qt::Key_Down, true), noDebugger, true);
    add("dbg.cursor_left", Tr("Cursor left in the pane"), debug, key(Qt::Key_Left, true), noDebugger, true);
    add("dbg.cursor_right", Tr("Cursor right in the pane"), debug, key(Qt::Key_Right, true), noDebugger, true);
    add("dbg.page_up", Tr("Page up in the pane"), debug, key(Qt::Key_PageUp, true), noDebugger, true);
    add("dbg.page_down", Tr("Page down in the pane"), debug, key(Qt::Key_PageDown, true), noDebugger, true);
    add("dbg.memory_follow", Tr("Memory follows the value under the cursor"), debug, key(Qt::Key_Return, false),
        noDebugger, true);
    for (const char* id : {"dbg.breakpoint_toggle_pc", "dbg.labels_toggle"})
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
    add("osd.keyboard", Tr("On-screen keyboard"), machine,
        [hooks](bool) -> QString {
            if (hooks.toggleKeyboard)
                hooks.toggleKeyboard();
            return {};
        },
        nullptr);
    for (const char* id : {"cap.record_toggle", "cap.clip_last", "osd.quick_menu"})
        add(id, QString::fromLatin1(id), machine, nullptr, [later] { return later; });
}
