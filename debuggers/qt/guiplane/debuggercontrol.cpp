#include "debuggercontrol.h"

#include <QApplication>
#include <QCoreApplication>
#include <QJsonValue>
#include <QKeyEvent>

#include <algorithm>
#include <cstdlib>

#include "dialogs/debugger/debuggerwindow.h"
#include "dialogs/debugger/disasmview.h"
#include "dialogs/debugger/memoryview.h"
#include "guicontrol/host.h"
#include "guicontrol/node.h"

using guicontrol::Call;
using guicontrol::Done;
using guicontrol::Node;
using guicontrol::Result;
namespace errors = guicontrol::errors;
namespace params = guicontrol::params;

namespace
{
constexpr const char* kWindow = "debugger";
constexpr const char* kDisassembly = "disassembly";
constexpr const char* kMemory = "memory";
constexpr int kDisassemblyPage = 16;  ///< rows a page moves the disassembly cursor (the interface's page)
constexpr unsigned int kLine = 16;    ///< bytes a line of the dump

Result Gone()
{
    return Result::Failure(errors::kNotFound, QStringLiteral("the debugger window is gone"));
}

Result NoMachine()
{
    return Result::Failure(errors::kNotNow, QStringLiteral("the debugger shows no machine"));
}

QJsonObject CursorBody(MemoryView* view)
{
    return QJsonObject{{"cursor", params::Hex(static_cast<quint16>(view->source().origin + view->cursor()))}};
}

QJsonObject CursorBody(DisasmView* code)
{
    return QJsonObject{{"cursor", params::Hex(static_cast<quint16>(code->cursor_address()))}};
}

/// A CPU address in the dump: its offset there, clamped to the space it shows
unsigned int Offset(MemoryView* view, unsigned int address)
{
    const MemorySource& source = view->source();
    const unsigned int offset = address >= source.origin ? address - source.origin : 0;
    return std::min(offset, source.size > 0 ? source.size - 1 : 0);
}
}  // namespace

DebuggerControl::DebuggerControl(guicontrol::Host& host, DebuggerWindow* debugger, Hooks hooks)
    : QObject(debugger), _host(&host), _debugger(debugger), _hooks(std::move(hooks))
{
    BindHost();
    BindDisassembly();
    BindMemory();

    // The views say when they move; the window when it read the machine again (a stop, a running machine's tick)
    connect(debugger, &DebuggerWindow::views_refreshed, this, &DebuggerControl::PublishInstance);
    connect(qApp, &QApplication::focusChanged, this, &DebuggerControl::PublishFocus);
    for (DisasmView* code : debugger->findChildren<DisasmView*>())
    {
        connect(code, &DisasmView::cursor_moved, this, &DebuggerControl::Publish);
        connect(code, &DisasmView::top_moved, this, &DebuggerControl::Publish);
    }
    for (MemoryView* view : debugger->findChildren<MemoryView*>())
        connect(view, &MemoryView::cursor_moved, this, &DebuggerControl::Publish);
    PublishInstance();
}

DebuggerControl::~DebuggerControl()
{
    if (!_host)
        return;
    if (Node* window = _host->Root().Child(QLatin1String(kWindow)))
    {
        window->RemoveChild(kDisassembly);
        window->RemoveChild(kMemory);
    }
}

Node* DebuggerControl::View(const char* id) const
{
    if (!_host)
        return nullptr;
    Node* window = _host->Root().Child(QLatin1String(kWindow));
    return window != nullptr ? window->Child(QLatin1String(id)) : nullptr;
}

void DebuggerControl::BindHost()
{
    Node& root = _host->Root();
    const QPointer<DebuggerControl> self(this);
    // A pane command opens a hidden debugger first, as unreal-qt's does
    const auto opened = [self]() -> DebuggerWindow* {
        DebuggerWindow* window = self ? self->_debugger.data() : nullptr;
        if (window != nullptr && !window->isVisible())
            window->show();
        return window;
    };

    root.AddInterface(QStringLiteral("debugger"));
    root.SetCommand(QStringLiteral("debugger.open"), [opened](const Call&) {
        DebuggerWindow* window = opened();
        if (window == nullptr)
            return Gone();
        window->raise();
        window->activateWindow();
        return Result::Success();
    });
    root.SetCommand(QStringLiteral("debugger.close"), [self](const Call&) {
        if (!self || !self->_debugger)
            return Gone();
        // Hidden, not closed: the process and its plane stay, debugger.open brings the window back
        self->_debugger->hide();
        return Result::Success();
    });
    root.SetCommand(QStringLiteral("debugger.focus"), [opened](const Call& call) {
        DebuggerWindow* window = opened();
        if (window == nullptr)
            return Gone();
        const QString pane = call.params.value(QStringLiteral("pane")).toString();
        if (!window->focus_pane(pane))
            return Result::Failure(errors::kBadRequest, QStringLiteral("pane: disassembly, registers, memory or stack"));
        return Result::Success(QJsonObject{{"pane", pane}});
    });
    for (const int delta : {1, -1})
        root.SetCommand(delta > 0 ? QStringLiteral("debugger.focus_next") : QStringLiteral("debugger.focus_prev"),
                        [opened, delta](const Call&) {
                            DebuggerWindow* window = opened();
                            if (window == nullptr)
                                return Gone();
                            window->move_pane_focus(delta);
                            return Result::Success(QJsonObject{{"pane", window->focused_pane()}});
                        });
    root.SetCommand(QStringLiteral("debugger.cursor_move"), [opened](const Call& call) {
        DebuggerWindow* window = opened();
        if (window == nullptr)
            return Gone();
        int dx = 0;
        int dy = 0;
        Result failure;
        if (!params::Direction(call.params, dx, dy, failure))
            return failure;
        window->send_pane_key(dy < 0 ? Qt::Key_Up : dy > 0 ? Qt::Key_Down : dx < 0 ? Qt::Key_Left : Qt::Key_Right);
        return Result::Success(QJsonObject{{"pane", window->focused_pane()}});
    });
    root.SetCommand(QStringLiteral("debugger.page"), [opened](const Call& call) {
        DebuggerWindow* window = opened();
        if (window == nullptr)
            return Gone();
        int pages = 1;
        Result failure;
        if (!params::Pages(call.params, pages, failure))
            return failure;
        for (int n = 0; n < std::abs(pages); ++n)
            window->send_pane_key(pages < 0 ? Qt::Key_PageUp : Qt::Key_PageDown);
        return Result::Success(QJsonObject{{"pane", window->focused_pane()}});
    });
    root.SetCommand(QStringLiteral("debugger.activate"), [opened](const Call&) {
        DebuggerWindow* window = opened();
        if (window == nullptr)
            return Gone();
        window->send_pane_key(Qt::Key_Return);
        return Result::Success(QJsonObject{{"pane", window->focused_pane()}});
    });
}

void DebuggerControl::BindDisassembly()
{
    Node& view = _host->Root().AddChild(QLatin1String(kWindow)).AddChild(kDisassembly);
    view.AddInterface(kDisassembly);
    const QPointer<DebuggerControl> self(this);
    // The listing in front; null with the refusal when there is none or no machine is shown
    const auto code = [self](Result& failure) -> DisasmView* {
        if (!self || !self->_debugger)
        {
            failure = Gone();
            return nullptr;
        }
        DisasmView* listing = self->_debugger->current_code();
        if (listing == nullptr || !self->_hooks.instance || self->_hooks.instance().isEmpty())
        {
            failure = NoMachine();
            return nullptr;
        }
        return listing;
    };
    // After a command the state goes out at once, before the answer
    const auto answer = [self](Result result) {
        if (self)
            self->Publish();
        return result;
    };

    view.SetCommand(QStringLiteral("disassembly.goto"), [code, answer](const Call& call) {
        quint16 address = 0;
        Result failure;
        if (!params::Address(call.params, "addr", address, failure))
            return failure;
        DisasmView* listing = code(failure);
        if (listing == nullptr)
            return failure;
        listing->show_at_top(address);
        return answer(Result::Success(QJsonObject{{"top", params::Hex(static_cast<quint16>(listing->top_address()))}}));
    });
    view.SetCommand(QStringLiteral("disassembly.follow_pc"), [code, answer](const Call&) {
        Result failure;
        DisasmView* listing = code(failure);
        if (listing == nullptr)
            return failure;
        listing->show_pc();
        return answer(Result::Success(QJsonObject{{"top", params::Hex(static_cast<quint16>(listing->top_address()))}}));
    });
    view.SetCommand(QStringLiteral("disassembly.cursor"), [code, answer](const Call& call) {
        quint16 address = 0;
        Result failure;
        if (!params::Address(call.params, "addr", address, failure))
            return failure;
        DisasmView* listing = code(failure);
        if (listing == nullptr)
            return failure;
        listing->place_cursor(address);
        return answer(Result::Success(CursorBody(listing)));
    });
    view.SetCommand(QStringLiteral("disassembly.cursor_move"), [code, answer](const Call& call) {
        int dx = 0;
        int dy = 0;
        Result failure;
        if (!params::Direction(call.params, dx, dy, failure))
            return failure;
        if (dy == 0)
            return Result::Failure(errors::kBadRequest, QStringLiteral("dir: the disassembly moves up or down"));
        DisasmView* listing = code(failure);
        if (listing == nullptr)
            return failure;
        listing->move_cursor(dy, false);
        return answer(Result::Success(CursorBody(listing)));
    });
    view.SetCommand(QStringLiteral("disassembly.page"), [code, answer](const Call& call) {
        int pages = 1;
        Result failure;
        if (!params::Pages(call.params, pages, failure))
            return failure;
        DisasmView* listing = code(failure);
        if (listing == nullptr)
            return failure;
        listing->move_cursor(pages * kDisassemblyPage, true);
        return answer(Result::Success(CursorBody(listing)));
    });
    view.SetCommand(QStringLiteral("disassembly.scroll_mode"), [self, answer](const Call& call) {
        if (!self || !self->_debugger)
            return Gone();
        DisasmView* listing = self->_debugger->current_code();
        if (listing == nullptr)
            return NoMachine();
        const QJsonValue mode = call.params.value(QStringLiteral("mode"));
        bool bytes = !listing->byte_scroll();
        if (!mode.isUndefined())
        {
            if (mode.toString() != QLatin1String("byte") && mode.toString() != QLatin1String("command"))
                return Result::Failure(errors::kBadRequest, QStringLiteral("mode: byte or command (absent: flip)"));
            bytes = mode.toString() == QLatin1String("byte");
        }
        // Every processor's listing scrolls the same way, as unreal-qt's one setting
        for (DisasmView* each : self->_debugger->findChildren<DisasmView*>())
            each->set_byte_scroll(bytes);
        return answer(Result::Success(QJsonObject{{"scroll_mode", bytes ? "byte" : "command"}}));
    });
    view.SetCommand(QStringLiteral("disassembly.run_to_cursor"), [self, code, answer](const Call&) {
        Result failure;
        DisasmView* listing = code(failure);
        if (listing == nullptr)
            return failure;
        const unsigned int address = listing->cursor_address();
        // F4: the window's own run, a breakpoint on the way stops it
        if (!self->_debugger->run_to_cursor())
            return Result::Failure(errors::kNotNow, QStringLiteral("the machine is not held: pause it first"));
        return answer(Result::Success(QJsonObject{{"addr", params::Hex(static_cast<quint16>(address))}}));
    });
    view.SetCommand(QStringLiteral("disassembly.breakpoint_toggle_cursor"), [self, code, answer](const Call&) {
        Result failure;
        DisasmView* listing = code(failure);
        if (listing == nullptr)
            return failure;
        // F2: the listing's own toggle, its marks and the breakpoints tab repainted
        if (!self->_debugger->toggle_breakpoint_at_cursor())
            return Result::Failure(errors::kNotNow, QStringLiteral("the machine is only seen: the emulator is gone"));
        return answer(Result::Success(CursorBody(listing)));
    });
}

void DebuggerControl::BindMemory()
{
    Node& view = _host->Root().AddChild(QLatin1String(kWindow)).AddChild(kMemory);
    view.AddInterface(kMemory);
    const QPointer<DebuggerControl> self(this);
    // The dump of the processor in front, its tab brought to the front; null with the refusal
    const auto dump = [self](Result& failure) -> MemoryView* {
        if (!self || !self->_debugger)
        {
            failure = Gone();
            return nullptr;
        }
        MemoryView* memory = self->_debugger->current_memory(true);
        if (memory == nullptr || !self->_hooks.instance || self->_hooks.instance().isEmpty())
        {
            failure = NoMachine();
            return nullptr;
        }
        return memory;
    };
    const auto answer = [self](Result result) {
        if (self)
            self->Publish();
        return result;
    };

    view.SetCommand(QStringLiteral("memory.goto"), [dump, answer](const Call& call) {
        quint16 address = 0;
        Result failure;
        if (!params::Address(call.params, "addr", address, failure))
            return failure;
        MemoryView* memory = dump(failure);
        if (memory == nullptr)
            return failure;
        memory->go_to(Offset(memory, address));
        return answer(Result::Success(CursorBody(memory)));
    });
    view.SetCommand(QStringLiteral("memory.cursor"), [dump, answer](const Call& call) {
        quint16 address = 0;
        Result failure;
        if (!params::Address(call.params, "addr", address, failure))
            return failure;
        MemoryView* memory = dump(failure);
        if (memory == nullptr)
            return failure;
        memory->set_cursor(Offset(memory, address), false);
        return answer(Result::Success(CursorBody(memory)));
    });
    view.SetCommand(QStringLiteral("memory.cursor_move"), [dump, answer](const Call& call) {
        int dx = 0;
        int dy = 0;
        Result failure;
        if (!params::Direction(call.params, dx, dy, failure))
            return failure;
        MemoryView* memory = dump(failure);
        if (memory == nullptr)
            return failure;
        // A byte left or right, a line up or down; at the space's edges the cursor stays
        const long long target = static_cast<long long>(memory->cursor()) + dx + dy * static_cast<long long>(kLine);
        if (target >= 0 && target < static_cast<long long>(memory->source().size))
            memory->set_cursor(static_cast<unsigned int>(target), false);
        return answer(Result::Success(CursorBody(memory)));
    });
    view.SetCommand(QStringLiteral("memory.page"), [dump, answer](const Call& call) {
        int pages = 1;
        Result failure;
        if (!params::Pages(call.params, pages, failure))
            return failure;
        MemoryView* memory = dump(failure);
        if (memory == nullptr)
            return failure;
        // The dump's own Page Up / Page Down: a screen of lines, the rows shown with it
        for (int n = 0; n < std::abs(pages); ++n)
        {
            QKeyEvent press(QEvent::KeyPress, pages < 0 ? Qt::Key_PageUp : Qt::Key_PageDown, Qt::NoModifier);
            QCoreApplication::sendEvent(memory, &press);
        }
        return answer(Result::Success(CursorBody(memory)));
    });
    view.SetCommand(QStringLiteral("memory.follow"), [self, answer](const Call& call) {
        if (!self || !self->_debugger)
            return Gone();
        const QString name = call.params.value(QStringLiteral("register")).toString().toLower();
        if (!self->_debugger->set_memory_follow(name))
            return Result::Failure(errors::kBadRequest, QStringLiteral("register: pc, sp, bc, de, hl, ix, iy or off"));
        return answer(Result::Success(QJsonObject{{"follow", name}}));
    });
    view.SetCommand(QStringLiteral("memory.select"), [dump, answer](const Call& call) {
        quint16 from = 0;
        quint16 to = 0;
        Result failure;
        if (!params::Address(call.params, "from", from, failure) || !params::Address(call.params, "to", to, failure))
            return failure;
        MemoryView* memory = dump(failure);
        if (memory == nullptr)
            return failure;
        if (to < from)
            std::swap(from, to);
        // The dump shows the whole space: any range is on it
        memory->select(Offset(memory, from), Offset(memory, to));
        return answer(Result::Success(QJsonObject{{"from", params::Hex(from)}, {"to", params::Hex(to)}}));
    });
    view.SetCommand(QStringLiteral("memory.follow_cursor"), [dump, answer](const Call&) {
        Result failure;
        MemoryView* memory = dump(failure);
        if (memory == nullptr)
            return failure;
        const MemorySource& source = memory->source();
        const unsigned int at = memory->cursor();
        if (!source.read || at + 1 >= source.size)
            return Result::Failure(errors::kNotNow, QStringLiteral("no word at the cursor"));
        const unsigned int target = (source.read(at) & 0xFF) | (source.read(at + 1) & 0xFF) << 8;
        memory->go_to(Offset(memory, target));
        return answer(Result::Success(CursorBody(memory)));
    });
    for (const bool any : {false, true})
        view.SetAsyncCommand(
            any ? QStringLiteral("memory.find_last") : QStringLiteral("memory.find_last_write"),
            [self, dump, any](const Call& call, Done done) {
                Result failure;
                MemoryView* memory = dump(failure);
                if (memory == nullptr)
                {
                    done(failure);
                    return;
                }
                // The history records the machine's own processor: a card's (General Sound) has none
                if (memory->source().cpu != nullptr && memory->source().cpu != self->_debugger->main_cpu())
                {
                    done(Result::Failure(errors::kNotNow,
                                         QStringLiteral("the history records the main processor: its tab in front")));
                    return;
                }
                if (!self->_hooks.coreAction)
                {
                    done(Result::Failure(errors::kNotNow, QStringLiteral("no core actions here")));
                    return;
                }
                QJsonObject actionParams{{"addr", static_cast<int>(memory->source().origin + memory->cursor())}};
                if (any)
                {
                    const QString access = call.params.value(QStringLiteral("access")).toString();
                    actionParams["access"] = access.isEmpty() ? QStringLiteral("write") : access;
                }
                self->_hooks.coreAction(any ? QStringLiteral("ttd.find_last") : QStringLiteral("ttd.find_last_write"),
                                        actionParams, [self, done](const Result& result) {
                                            if (self)
                                                self->Publish();
                                            done(result);
                                        });
            });
}

void DebuggerControl::Publish()
{
    if (!_host || !_debugger)
        return;
    _host->Root().SetState(QJsonObject{{"open", _debugger->isVisible()}, {"pane", _debugger->focused_pane()}});

    const bool shown = _hooks.instance && !_hooks.instance().isEmpty();
    CPU* cpu = _debugger->current_cpu();
    const QJsonValue cpuName = cpu != nullptr ? QJsonValue(QString::fromStdString(cpu->name)) : QJsonValue();
    if (Node* view = View(kDisassembly))
    {
        DisasmView* listing = _debugger->current_code();
        const bool on = shown && listing != nullptr;
        view->SetState(QJsonObject{
            {"top", on ? QJsonValue(params::Hex(static_cast<quint16>(listing->top_address()))) : QJsonValue()},
            {"cursor", on ? QJsonValue(params::Hex(static_cast<quint16>(listing->cursor_address()))) : QJsonValue()},
            {"pc", on ? QJsonValue(params::Hex(static_cast<quint16>(cpu->get_pc()))) : QJsonValue()},
            {"scroll_mode", listing != nullptr && listing->byte_scroll() ? "byte" : "command"},
            {"cpu", cpuName}});
    }
    if (Node* view = View(kMemory))
    {
        MemoryView* memory = _debugger->current_memory();
        const bool on = shown && memory != nullptr;
        QJsonValue selection;
        unsigned int from = 0;
        unsigned int to = 0;
        if (on && memory->selection(from, to))
            selection = QJsonObject{{"from", params::Hex(static_cast<quint16>(memory->source().origin + from))},
                                    {"to", params::Hex(static_cast<quint16>(memory->source().origin + to))}};
        view->SetState(QJsonObject{
            {"top", on ? QJsonValue(params::Hex(static_cast<quint16>(memory->source().origin + memory->top())))
                       : QJsonValue()},
            {"cursor", on ? QJsonValue(params::Hex(static_cast<quint16>(memory->source().origin + memory->cursor())))
                          : QJsonValue()},
            {"selection", selection},
            {"follow", _debugger->memory_follow()},
            {"cpu", cpuName}});
    }
}

void DebuggerControl::PublishFocus()
{
    if (!_host || !_debugger)
        return;
    Publish();
    // The pane the user gives the focus is the focused node of its interface (design §4.5); only on a change
    const QString pane = _debugger->isActiveWindow() ? _debugger->focused_pane() : QString();
    if (pane == _lastPane)
        return;
    _lastPane = pane;
    if (pane == QLatin1String(kDisassembly))
        _host->SetFocus(kDisassembly, View(kDisassembly));
    else if (pane == QLatin1String(kMemory))
        _host->SetFocus(kMemory, View(kMemory));
}

void DebuggerControl::PublishInstance()
{
    // A node pushes only a change: asked on every refresh of the window, as a remote emulator comes and goes
    const QString id = _hooks.instance ? _hooks.instance() : QString();
    const QString endpoint = _hooks.endpoint ? _hooks.endpoint() : QString();
    for (const char* name : {kDisassembly, kMemory})
        if (Node* view = View(name))
            view->SetInstance(endpoint, id);
    Publish();
}
