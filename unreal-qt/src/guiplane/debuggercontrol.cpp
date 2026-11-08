#include "debuggercontrol.h"

#include <QApplication>
#include <QHexView/qhexview.h>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMetaObject>

#include <cstdlib>

#include "debugger/debuggerwindow.h"
#include "debugger/disassemblerwidget.h"
#include "debugger/metadatasettings.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/memory/memory.h"
#include "emulator/state/statenodejson.h"
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
constexpr const char* kDisassembly = "disassembly";
constexpr const char* kMemory = "memory";
constexpr int kDisassemblyPage = 16;  ///< rows a page moves the disassembly cursor (PageUp / PageDown)

Result Gone()
{
    return Result::Failure(errors::kNotFound, QStringLiteral("the debugger window is gone"));
}

/// The switches of Debug > Metadata by their plane names
const std::vector<std::pair<QString, MetadataSettings::Switch>>& Switches()
{
    static const std::vector<std::pair<QString, MetadataSettings::Switch>> switches = {
        {QStringLiteral("metadata"), MetadataSettings::Switch::Metadata},
        {QStringLiteral("regions"), MetadataSettings::Switch::Regions},
        {QStringLiteral("colours"), MetadataSettings::Switch::Colours},
        {QStringLiteral("notes"), MetadataSettings::Switch::Notes},
        {QStringLiteral("labels"), MetadataSettings::Switch::Labels}};
    return switches;
}

/// A key into a widget, as the pane focus sends it
void SendKey(QWidget* receiver, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(receiver, &press);
    QCoreApplication::sendEvent(receiver, &release);
}

/// A core action of the view's machine, its answer back on the GUI thread (the registry answers on its worker)
void SubmitCore(QObject* context, const std::string& id, StateNode actionParams, const std::string& instance,
                const QString& client, Done done)
{
    actions::ActionValue value;
    value.kind = actions::ValueKind::Press;
    value.pressed = true;
    value.params = std::move(actionParams);
    actions::ActionOrigin origin;
    origin.origin = execution::Origin::Gui;  // as the debugger's own button
    origin.identity = "gui-control|" + client.toStdString();
    origin.client = client.toStdString();
    const QPointer<QObject> alive(context);
    actions::ActionRegistry::Instance().Submit(
        id, std::move(value), instance, std::move(origin), [alive, done](actions::ActionResult result) {
            const QJsonObject body = QJsonDocument::fromJson(QByteArray::fromStdString(StateNodeToJsonText(result.body)))
                                         .object();
            Result answer = result.ok ? Result::Success(body)
                                      : Result::Failure(QString::fromStdString(result.error).isEmpty()
                                                            ? QString(errors::kFailed)
                                                            : QString::fromStdString(result.error),
                                                        QString::fromStdString(result.message), body);
            if (!alive)
                return;
            QMetaObject::invokeMethod(alive.data(), [done, answer]() { done(answer); }, Qt::QueuedConnection);
        });
}
}  // namespace

DebuggerControl::DebuggerControl(guicontrol::Host& host, DebuggerWindow* debugger, EmulatorBinding* binding,
                                 Hooks hooks)
    : QObject(debugger), _host(&host), _debugger(debugger), _binding(binding), _hooks(std::move(hooks))
{
    BindHost();
    BindDisassembly();
    BindMemory();

    connect(debugger->disassembler(), &DisassemblerWidget::viewChanged, this, &DebuggerControl::Publish);
    connect(debugger->memoryView(), &QHexView::positionChanged, this, &DebuggerControl::Publish);
    connect(debugger, &DebuggerWindow::memoryViewChanged, this, &DebuggerControl::Publish);
    connect(debugger, &DebuggerWindow::visibilityChanged, this, &DebuggerControl::Publish);
    connect(&MetadataSettings::Instance(), &MetadataSettings::changed, this, &DebuggerControl::Publish);
    connect(qApp, &QApplication::focusChanged, this, &DebuggerControl::PublishFocus);
    if (binding != nullptr)
    {
        connect(binding, &EmulatorBinding::bound, this, &DebuggerControl::PublishInstance);
        connect(binding, &EmulatorBinding::unbound, this, &DebuggerControl::PublishInstance);
    }
    PublishInstance();
    Publish();
}

DebuggerControl::~DebuggerControl()
{
    if (!_host)
        return;
    if (Node* window = _host->Root().Child(QStringLiteral("debugger")))
    {
        window->RemoveChild(kDisassembly);
        window->RemoveChild(kMemory);
    }
}

Node* DebuggerControl::View(const char* id) const
{
    if (!_host)
        return nullptr;
    Node* window = _host->Root().Child(QStringLiteral("debugger"));
    return window != nullptr ? window->Child(QLatin1String(id)) : nullptr;
}

void DebuggerControl::BindHost()
{
    Node& root = _host->Root();
    const QPointer<DebuggerControl> self(this);
    const auto debugger = [self]() -> DebuggerWindow* { return self ? self->_debugger.data() : nullptr; };
    // A pane command opens a closed debugger first, as the controller bindings always did
    const auto opened = [self, debugger]() -> DebuggerWindow* {
        DebuggerWindow* window = debugger();
        if (window != nullptr && !window->isVisible() && self->_hooks.showDebugger)
            self->_hooks.showDebugger();
        return window;
    };

    root.AddInterface(QStringLiteral("debugger"));
    root.SetCommand(QStringLiteral("debugger.open"), [self, debugger](const Call&) {
        DebuggerWindow* window = debugger();
        if (window == nullptr)
            return Gone();
        if (self->_hooks.showDebugger)
            self->_hooks.showDebugger();
        window->raise();
        window->activateWindow();
        return Result::Success();
    });
    root.SetCommand(QStringLiteral("debugger.close"), [debugger](const Call&) {
        DebuggerWindow* window = debugger();
        if (window == nullptr)
            return Gone();
        window->hide();
        return Result::Success();
    });
    root.SetCommand(QStringLiteral("debugger.focus"), [opened](const Call& call) {
        DebuggerWindow* window = opened();
        if (window == nullptr)
            return Gone();
        const QString pane = call.params.value(QStringLiteral("pane")).toString();
        if (!window->focusPane(pane))
            return Result::Failure(errors::kBadRequest, QStringLiteral("pane: disassembly, registers, memory or stack"));
        return Result::Success(QJsonObject{{"pane", pane}});
    });
    for (const int delta : {1, -1})
        root.SetCommand(delta > 0 ? QStringLiteral("debugger.focus_next") : QStringLiteral("debugger.focus_prev"),
                        [opened, delta](const Call&) {
                            DebuggerWindow* window = opened();
                            if (window == nullptr)
                                return Gone();
                            window->movePaneFocus(delta);
                            return Result::Success(QJsonObject{{"pane", window->focusedPane()}});
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
        window->sendPaneKey(dy < 0 ? Qt::Key_Up : dy > 0 ? Qt::Key_Down : dx < 0 ? Qt::Key_Left : Qt::Key_Right);
        return Result::Success(QJsonObject{{"pane", window->focusedPane()}});
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
            window->sendPaneKey(pages < 0 ? Qt::Key_PageUp : Qt::Key_PageDown);
        return Result::Success(QJsonObject{{"pane", window->focusedPane()}});
    });
    root.SetCommand(QStringLiteral("debugger.activate"), [opened](const Call&) {
        DebuggerWindow* window = opened();
        if (window == nullptr)
            return Gone();
        window->sendPaneKey(Qt::Key_Return);
        return Result::Success(QJsonObject{{"pane", window->focusedPane()}});
    });

    root.AddInterface(QStringLiteral("metadata"));
    root.SetCommand(QStringLiteral("metadata.set"), [](const Call& call) {
        const QString name = call.params.value(QStringLiteral("switch")).toString();
        for (const auto& [switchName, which] : Switches())
            if (name == switchName)
            {
                bool on = false;
                Result failure;
                if (!params::Toggle(call.params, MetadataSettings::Instance().IsOn(which), on, failure))
                    return failure;
                MetadataSettings::Instance().Set(which, on);
                return Result::Success(QJsonObject{{"switch", name}, {"on", on}});
            }
        return Result::Failure(errors::kBadRequest, QStringLiteral("switch: metadata, regions, colours, notes or labels"));
    });
}

void DebuggerControl::BindDisassembly()
{
    Node& view = _host->Root().AddChild(QStringLiteral("debugger")).AddChild(kDisassembly);
    view.AddInterface(kDisassembly);
    const QPointer<DebuggerControl> self(this);
    const auto widget = [self]() -> DisassemblerWidget* {
        return self && self->_debugger ? self->_debugger->disassembler() : nullptr;
    };
    const auto machine = [self]() -> Emulator* { return self && self->_binding ? self->_binding->emulator() : nullptr; };

    view.SetCommand(QStringLiteral("disassembly.goto"), [widget, machine](const Call& call) {
        DisassemblerWidget* disassembly = widget();
        if (disassembly == nullptr)
            return Gone();
        quint16 address = 0;
        Result failure;
        if (!params::Address(call.params, "addr", address, failure))
            return failure;
        if (machine() == nullptr)
            return Result::Failure(errors::kNotNow, QStringLiteral("the debugger shows no machine"));
        disassembly->goToAddress(address);
        return Result::Success(QJsonObject{{"top", params::Hex(disassembly->topAddress())}});
    });
    view.SetCommand(QStringLiteral("disassembly.follow_pc"), [widget, machine](const Call&) {
        DisassemblerWidget* disassembly = widget();
        if (disassembly == nullptr)
            return Gone();
        if (machine() == nullptr)
            return Result::Failure(errors::kNotNow, QStringLiteral("the debugger shows no machine"));
        disassembly->followPc();
        return Result::Success(QJsonObject{{"top", params::Hex(disassembly->topAddress())}});
    });
    view.SetCommand(QStringLiteral("disassembly.cursor"), [widget, machine](const Call& call) {
        DisassemblerWidget* disassembly = widget();
        if (disassembly == nullptr)
            return Gone();
        quint16 address = 0;
        Result failure;
        if (!params::Address(call.params, "addr", address, failure))
            return failure;
        if (machine() == nullptr)
            return Result::Failure(errors::kNotNow, QStringLiteral("the debugger shows no machine"));
        disassembly->setCursorAddress(address);
        return Result::Success(QJsonObject{{"cursor", params::Hex(address)}});
    });
    view.SetCommand(QStringLiteral("disassembly.cursor_move"), [widget](const Call& call) {
        DisassemblerWidget* disassembly = widget();
        if (disassembly == nullptr)
            return Gone();
        int dx = 0;
        int dy = 0;
        Result failure;
        if (!params::Direction(call.params, dx, dy, failure))
            return failure;
        if (dy == 0)
            return Result::Failure(errors::kBadRequest, QStringLiteral("dir: the disassembly moves up or down"));
        disassembly->moveCursorBy(dy);
        const std::optional<uint16_t> cursor = disassembly->cursorAddress();
        return Result::Success(cursor ? QJsonObject{{"cursor", params::Hex(*cursor)}} : QJsonObject());
    });
    view.SetCommand(QStringLiteral("disassembly.page"), [widget](const Call& call) {
        DisassemblerWidget* disassembly = widget();
        if (disassembly == nullptr)
            return Gone();
        int pages = 1;
        Result failure;
        if (!params::Pages(call.params, pages, failure))
            return failure;
        disassembly->moveCursorBy(pages * kDisassemblyPage);
        const std::optional<uint16_t> cursor = disassembly->cursorAddress();
        return Result::Success(cursor ? QJsonObject{{"cursor", params::Hex(*cursor)}} : QJsonObject());
    });
    view.SetCommand(QStringLiteral("disassembly.scroll_mode"), [widget](const Call& call) {
        DisassemblerWidget* disassembly = widget();
        if (disassembly == nullptr)
            return Gone();
        const QJsonValue mode = call.params.value(QStringLiteral("mode"));
        if (mode.isUndefined())
            disassembly->setByteScroll(!disassembly->byteScroll());
        else if (mode.toString() == QLatin1String("byte") || mode.toString() == QLatin1String("command"))
            disassembly->setByteScroll(mode.toString() == QLatin1String("byte"));
        else
            return Result::Failure(errors::kBadRequest, QStringLiteral("mode: byte or command (absent: flip)"));
        return Result::Success(QJsonObject{{"scroll_mode", disassembly->byteScroll() ? "byte" : "command"}});
    });
    view.SetAsyncCommand(QStringLiteral("disassembly.run_to_cursor"), [self, widget, machine](const Call& call, Done done) {
        DisassemblerWidget* disassembly = widget();
        Emulator* emulator = machine();
        if (disassembly == nullptr || emulator == nullptr)
        {
            done(disassembly == nullptr ? Gone()
                                        : Result::Failure(errors::kNotNow, QStringLiteral("the debugger shows no machine")));
            return;
        }
        const std::optional<uint16_t> cursor = disassembly->cursorAddress();
        if (!cursor)
        {
            done(Result::Failure(errors::kNotNow, QStringLiteral("the disassembly has no cursor row: click one first")));
            return;
        }
        StateNode actionParams = StateNode::Object();
        actionParams["addr"] = static_cast<int>(*cursor);
        SubmitCore(self, "run.to_cursor", std::move(actionParams), emulator->GetId(), call.client, std::move(done));
    });
    view.SetCommand(QStringLiteral("disassembly.breakpoint_toggle_cursor"), [self](const Call&) {
        if (!self || !self->_debugger)
            return Gone();
        // The widget refuses while an automation session holds the machine, and repaints its marks
        self->_debugger->toggleBreakpointAtCursor();
        return Result::Success();
    });
}

void DebuggerControl::BindMemory()
{
    Node& view = _host->Root().AddChild(QStringLiteral("debugger")).AddChild(kMemory);
    view.AddInterface(kMemory);
    const QPointer<DebuggerControl> self(this);
    const auto debugger = [self]() -> DebuggerWindow* { return self ? self->_debugger.data() : nullptr; };
    const auto machine = [self]() -> Emulator* { return self && self->_binding ? self->_binding->emulator() : nullptr; };
    // The cursor on a CPU address: the page stays when the address is on it, else the view shows the address's page
    const auto place = [](DebuggerWindow* window, quint16 address) {
        const std::optional<uint16_t> top = window->memoryTop();
        if (top && address >= *top && address < *top + 0x4000)
            window->memoryView()->hexCursor()->move(static_cast<qint64>(address - *top));
        else
            window->showMemoryAt(address);
    };
    const auto cursorBody = [](DebuggerWindow* window) {
        const std::optional<uint16_t> cursor = window->memoryCursor();
        return cursor ? QJsonObject{{"cursor", params::Hex(*cursor)}} : QJsonObject();
    };

    view.SetCommand(QStringLiteral("memory.goto"), [debugger, cursorBody](const Call& call) {
        DebuggerWindow* window = debugger();
        if (window == nullptr)
            return Gone();
        quint16 address = 0;
        Result failure;
        if (!params::Address(call.params, "addr", address, failure))
            return failure;
        if (!window->showMemoryAt(address))
            return Result::Failure(errors::kNotNow, QStringLiteral("the debugger shows no machine"));
        return Result::Success(cursorBody(window));
    });
    view.SetCommand(QStringLiteral("memory.cursor"), [debugger, machine, place, cursorBody](const Call& call) {
        DebuggerWindow* window = debugger();
        if (window == nullptr)
            return Gone();
        quint16 address = 0;
        Result failure;
        if (!params::Address(call.params, "addr", address, failure))
            return failure;
        if (machine() == nullptr)
            return Result::Failure(errors::kNotNow, QStringLiteral("the debugger shows no machine"));
        place(window, address);
        return Result::Success(cursorBody(window));
    });
    view.SetCommand(QStringLiteral("memory.cursor_move"), [debugger, place, cursorBody](const Call& call) {
        DebuggerWindow* window = debugger();
        if (window == nullptr)
            return Gone();
        int dx = 0;
        int dy = 0;
        Result failure;
        if (!params::Direction(call.params, dx, dy, failure))
            return failure;
        const std::optional<uint16_t> cursor = window->memoryCursor();
        if (!cursor)
            return Result::Failure(errors::kNotNow, QStringLiteral("the memory view shows no mapped page"));
        // A line is the view's width in bytes; past the page's edge the next page shows
        const int line = static_cast<int>(std::max(1u, window->memoryView()->lineLength()));
        place(window, static_cast<quint16>(*cursor + dx + dy * line));
        return Result::Success(cursorBody(window));
    });
    view.SetCommand(QStringLiteral("memory.page"), [debugger, cursorBody](const Call& call) {
        DebuggerWindow* window = debugger();
        if (window == nullptr)
            return Gone();
        int pages = 1;
        Result failure;
        if (!params::Pages(call.params, pages, failure))
            return failure;
        for (int n = 0; n < std::abs(pages); ++n)
            SendKey(window->memoryView(), pages < 0 ? Qt::Key_PageUp : Qt::Key_PageDown);
        return Result::Success(cursorBody(window));
    });
    view.SetCommand(QStringLiteral("memory.follow"), [debugger](const Call& call) {
        DebuggerWindow* window = debugger();
        if (window == nullptr)
            return Gone();
        const QString name = call.params.value(QStringLiteral("register")).toString().toLower();
        if (!window->setMemoryFollow(name))
            return Result::Failure(errors::kBadRequest, QStringLiteral("register: pc, sp, bc, de, hl, ix, iy or off"));
        return Result::Success(QJsonObject{{"follow", name}});
    });
    view.SetCommand(QStringLiteral("memory.select"), [debugger, machine, place](const Call& call) {
        DebuggerWindow* window = debugger();
        if (window == nullptr)
            return Gone();
        quint16 from = 0;
        quint16 to = 0;
        Result failure;
        if (!params::Address(call.params, "from", from, failure) || !params::Address(call.params, "to", to, failure))
            return failure;
        if (to < from)
            std::swap(from, to);
        if (machine() == nullptr)
            return Result::Failure(errors::kNotNow, QStringLiteral("the debugger shows no machine"));
        place(window, from);
        const std::optional<uint16_t> top = window->memoryTop();
        if (!top || to >= *top + 0x4000)
            return Result::Failure(errors::kBadRequest,
                                   QStringLiteral("from, to: one 16 KB page (the view shows one at a time)"));
        window->memoryView()->hexCursor()->select(static_cast<qint64>(to - *top));  // the last byte, inclusive
        return Result::Success(QJsonObject{{"from", params::Hex(from)}, {"to", params::Hex(to)}});
    });
    view.SetCommand(QStringLiteral("memory.follow_cursor"), [debugger, machine, cursorBody](const Call&) {
        DebuggerWindow* window = debugger();
        Emulator* emulator = machine();
        if (window == nullptr)
            return Gone();
        const std::optional<uint16_t> cursor = window->memoryCursor();
        if (emulator == nullptr || emulator->GetMemory() == nullptr || !cursor)
            return Result::Failure(errors::kNotNow, QStringLiteral("the memory view shows no mapped page"));
        Memory* memory = emulator->GetMemory();
        const uint16_t target = static_cast<uint16_t>(memory->DirectReadFromZ80Memory(*cursor) |
                                                      memory->DirectReadFromZ80Memory(static_cast<uint16_t>(*cursor + 1))
                                                          << 8);
        window->showMemoryAt(target);
        return Result::Success(cursorBody(window));
    });
    for (const bool any : {false, true})
        view.SetAsyncCommand(any ? QStringLiteral("memory.find_last") : QStringLiteral("memory.find_last_write"),
                             [self, debugger, machine, any](const Call& call, Done done) {
                                 DebuggerWindow* window = debugger();
                                 Emulator* emulator = machine();
                                 if (window == nullptr)
                                 {
                                     done(Gone());
                                     return;
                                 }
                                 const std::optional<uint16_t> cursor = window->memoryCursor();
                                 if (emulator == nullptr || !cursor)
                                 {
                                     done(Result::Failure(errors::kNotNow,
                                                          QStringLiteral("the memory view shows no mapped page")));
                                     return;
                                 }
                                 StateNode actionParams = StateNode::Object();
                                 actionParams["addr"] = static_cast<int>(*cursor);
                                 if (any)
                                 {
                                     const QString access = call.params.value(QStringLiteral("access")).toString();
                                     actionParams["access"] = access.isEmpty() ? std::string("write") : access.toStdString();
                                 }
                                 SubmitCore(self, any ? "ttd.find_last" : "ttd.find_last_write", std::move(actionParams),
                                            emulator->GetId(), call.client, std::move(done));
                             });
}

void DebuggerControl::Publish()
{
    if (!_host || !_debugger)
        return;
    _host->Root().SetState(QJsonObject{{"open", _debugger->isVisible()}, {"pane", _debugger->focusedPane()}});
    QJsonObject switches;
    for (const auto& [name, which] : Switches())
        switches[name] = MetadataSettings::Instance().IsOn(which);
    _host->Root().SetState(QStringLiteral("metadata"), switches);

    const bool shown = _binding && _binding->emulator() != nullptr;
    if (Node* view = View(kDisassembly))
    {
        DisassemblerWidget* disassembly = _debugger->disassembler();
        const std::optional<uint16_t> cursor = disassembly->cursorAddress();
        view->SetState(QJsonObject{
            {"top", shown ? QJsonValue(params::Hex(disassembly->topAddress())) : QJsonValue()},
            {"cursor", shown && cursor ? QJsonValue(params::Hex(*cursor)) : QJsonValue()},
            {"pc", shown ? QJsonValue(params::Hex(disassembly->pcAddress())) : QJsonValue()},
            {"scroll_mode", disassembly->byteScroll() ? "byte" : "command"}});
    }
    if (Node* view = View(kMemory))
    {
        const std::optional<uint16_t> top = shown ? _debugger->memoryTop() : std::nullopt;
        const std::optional<uint16_t> cursor = shown ? _debugger->memoryCursor() : std::nullopt;
        QJsonValue selection;
        QHexView* hex = _debugger->memoryView();
        if (top && hex->getDocument() != nullptr && hex->selectionStartOffset() != hex->selectionEndOffset())
            selection = QJsonObject{
                {"from", params::Hex(static_cast<uint16_t>(*top + hex->selectionStartOffset()))},
                {"to", params::Hex(static_cast<uint16_t>(*top + hex->selectionEndOffset()))}};
        view->SetState(QJsonObject{{"top", top ? QJsonValue(params::Hex(*top)) : QJsonValue()},
                                   {"cursor", cursor ? QJsonValue(params::Hex(*cursor)) : QJsonValue()},
                                   {"selection", selection},
                                   {"follow", _debugger->memoryFollow()}});
    }
}

void DebuggerControl::PublishFocus()
{
    if (!_host || !_debugger)
        return;
    Publish();
    // The pane the user gives the focus is the focused node of its interface (design §4.5); only on a change
    const QString pane = _debugger->isActiveWindow() ? _debugger->focusedPane() : QString();
    if (pane == _lastPane)
        return;
    _lastPane = pane;
    if (pane == QLatin1String("disassembly"))
        _host->SetFocus(kDisassembly, View(kDisassembly));
    else if (pane == QLatin1String("memory"))
        _host->SetFocus(kMemory, View(kMemory));
}

void DebuggerControl::PublishInstance()
{
    Emulator* emulator = _binding ? _binding->emulator() : nullptr;
    const QString endpoint = _hooks.endpoint ? _hooks.endpoint() : QString();
    const QString id = emulator != nullptr ? QString::fromStdString(emulator->GetId()) : QString();
    for (const char* name : {kDisassembly, kMemory})
        if (Node* view = View(name))
            view->SetInstance(endpoint, id);
    Publish();
}
