// The GUI control plane (guicontrol/): a host and its clients on one thread, the window binding on an offscreen
// widget, discovery in a scratch directory. Built into unreal-qt-tests (it needs the QApplication of that runner).

#include "_helpers/testpathhelper.h"

#include "guicontrol/client.h"
#include "guicontrol/corebridge.h"
#include "guicontrol/discovery.h"
#include "guicontrol/host.h"
#include "guicontrol/interfaces.h"
#include "guicontrol/node.h"
#include "guicontrol/windowbinding.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QWidget>

#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

using namespace guicontrol;

namespace
{

/// Runs the event loop until `done` or `timeoutMs`
bool Pump(const std::function<bool()>& done, int timeoutMs = 1000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done())
    {
        if (clock.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

class GuiControl_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _directory = QString::fromStdString(TestPathHelper::GetUniqueTestScratchPath("guicontrol"));
        QDir().mkpath(_directory);
    }

    std::unique_ptr<Host> MakeHost(int tcpPort = -1, qint64 backlogLimit = 4 * 1024 * 1024)
    {
        HostOptions options;
        options.program = QStringLiteral("guicontrol-test");
        options.version = QStringLiteral("1.0");
        options.directory = _directory;
        options.tcpPort = tcpPort;
        if (tcpPort >= 0)
            options.tcpBind = QStringLiteral("127.0.0.1");
        options.backlogLimit = backlogLimit;
        options.endpoints = []() { return QJsonArray{QStringLiteral("http://127.0.0.1:8090")}; };
        auto host = std::make_unique<Host>(std::move(options));
        QString error;
        EXPECT_TRUE(host->Start(&error)) << error.toStdString();
        return host;
    }

    /// A view node with one command answering its own path
    static Node& AddView(Host& host, const QString& window, const QString& view, const QString& interfaceName)
    {
        Node& node = host.Root().AddChild(window).AddChild(view);
        node.AddInterface(interfaceName);
        const QString method = interfaceName + QStringLiteral(".probe");
        node.SetCommand(method, [path = node.Path()](const Call& call) {
            return Result::Success(QJsonObject{{"path", path}, {"echo", call.params.value("value")}});
        });
        return node;
    }

    QString _directory;
};

}  // namespace

TEST_F(GuiControl_Test, WelcomeListAndDiscovery)
{
    std::unique_ptr<Host> host = MakeHost();
    AddView(*host, "debugger", "disasm", "disassembly");

    const std::vector<discovery::Entry> entries = discovery::List(_directory);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().id, host->Id());
    EXPECT_EQ(entries.front().socket, host->SocketName());
    EXPECT_EQ(entries.front().endpoints.size(), 1);

    Client client;
    QString error;
    ASSERT_TRUE(client.Connect(entries.front().socket, 1000, &error)) << error.toStdString();
    EXPECT_EQ(client.Welcome().value("host").toString(), host->Id());
    EXPECT_EQ(client.Welcome().value("protocol").toInt(), kProtocolVersion);
    EXPECT_FALSE(client.Welcome().value("auth").toBool());

    const Result list = client.Call("list");
    ASSERT_TRUE(list.ok) << list.message.toStdString();
    EXPECT_EQ(list.body.value("program").toString(), "guicontrol-test");
    const QJsonArray windows = list.body.value("windows").toArray();
    ASSERT_EQ(windows.size(), 1);
    EXPECT_EQ(windows[0].toObject().value("id").toString(), "debugger");
    const QJsonObject view = windows[0].toObject().value("views").toArray().at(0).toObject();
    EXPECT_EQ(view.value("id").toString(), "disasm");
    EXPECT_TRUE(view.value("commands").toArray().contains(QJsonValue("disassembly.probe")));

    const QString file = host->DiscoveryFile();
    EXPECT_TRUE(QFile::exists(file));
    host->Stop();
    EXPECT_FALSE(QFile::exists(file));
}

TEST_F(GuiControl_Test, StaleDiscoveryEntryIsSkipped)
{
    discovery::Entry dead;
    dead.id = QStringLiteral("dead");
    dead.pid = 0x7FFFFFF0;  // no such process
    dead.socket = QStringLiteral("unreal-gui-dead");
    ASSERT_TRUE(discovery::Write(_directory + "/dead.json", dead));
    QFile garbage(_directory + "/garbage.json");
    ASSERT_TRUE(garbage.open(QIODevice::WriteOnly));
    garbage.write("{not json");
    garbage.close();

    EXPECT_TRUE(discovery::List(_directory).empty());
    std::unique_ptr<Host> host = MakeHost();
    const std::vector<discovery::Entry> entries = discovery::List(_directory);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().id, host->Id());
    // The next host removes the dead one's file, not the unreadable one
    EXPECT_FALSE(QFile::exists(_directory + "/dead.json"));
    EXPECT_TRUE(QFile::exists(_directory + "/garbage.json"));
}

TEST_F(GuiControl_Test, CallsAndErrors)
{
    std::unique_ptr<Host> host = MakeHost();
    AddView(*host, "debugger", "memory1", "memory");
    Client client;
    ASSERT_TRUE(client.Connect(host->SocketName()));

    Result result = client.Call("memory.probe", "", QJsonObject{{"value", 7}});
    ASSERT_TRUE(result.ok) << result.message.toStdString();
    EXPECT_EQ(result.body.value("path").toString(), "debugger/memory1");
    EXPECT_EQ(result.body.value("echo").toInt(), 7);

    result = client.Call("memory.nothing");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, errors::kNotFound);

    result = client.Call("memory.probe", "debugger/nowhere");
    EXPECT_EQ(result.error, errors::kNotFound);

    result = client.Call("");
    EXPECT_EQ(result.error, errors::kBadRequest);

    // A host's own id before the path is the same node
    result = client.Call("memory.probe", host->Id() + "/debugger/memory1");
    EXPECT_TRUE(result.ok) << result.message.toStdString();
}

TEST_F(GuiControl_Test, ResolutionByFocusAndInstance)
{
    std::unique_ptr<Host> host = MakeHost();
    Node& first = AddView(*host, "debugger", "memory1", "memory");
    Node& second = AddView(*host, "debugger", "memory2", "memory");
    first.SetInstance("http://127.0.0.1:8090", "aaaa");
    second.SetInstance("http://127.0.0.1:8090", "bbbb");
    Client client;
    ASSERT_TRUE(client.Connect(host->SocketName()));

    Result result = client.Call("memory.probe", "memory");
    EXPECT_EQ(result.error, errors::kAmbiguous);
    EXPECT_EQ(result.body.value("candidates").toArray().size(), 2);

    host->SetFocus("memory", &second);
    result = client.Call("memory.probe", "memory");
    ASSERT_TRUE(result.ok) << result.message.toStdString();
    EXPECT_EQ(result.body.value("path").toString(), "debugger/memory2");

    result = client.Call("memory.probe", "memory@aaaa");
    ASSERT_TRUE(result.ok) << result.message.toStdString();
    EXPECT_EQ(result.body.value("path").toString(), "debugger/memory1");

    // No view shows that machine: refused, never another machine's view
    result = client.Call("memory.probe", "memory@cccc");
    EXPECT_EQ(result.error, errors::kNotFound);
    EXPECT_TRUE(result.message.contains("cccc") && result.message.contains("debugger/memory1 (aaaa)"))
        << result.message.toStdString();

    // Removing the focused node forgets the focus
    host->Root().Child("debugger")->RemoveChild("memory2");
    result = client.Call("memory.probe", "memory");
    ASSERT_TRUE(result.ok) << result.message.toStdString();
    EXPECT_EQ(result.body.value("path").toString(), "debugger/memory1");
    EXPECT_TRUE(host->FocusRecords().isEmpty());
}

TEST_F(GuiControl_Test, ChooseHostByLatestFocus)
{
    std::unique_ptr<Host> left = MakeHost();
    std::unique_ptr<Host> right = MakeHost();
    ASSERT_NE(left->Id(), right->Id());
    Node& leftView = AddView(*left, "debugger", "memory1", "memory");
    Node& rightView = AddView(*right, "debugger", "memory1", "memory");

    const auto choose = [this](size_t& chosen) {
        std::vector<HostChoice> choices;
        for (const discovery::Entry& entry : discovery::List(_directory))
        {
            Client probe;
            EXPECT_TRUE(probe.Connect(entry.socket));
            choices.push_back({entry, probe.Call("resolve", {}, QJsonObject{{"method", "memory.probe"},
                                                                           {"target", "memory"}})});
        }
        return ChooseHost(choices, chosen);
    };

    size_t chosen = 99;
    Result result = choose(chosen);
    EXPECT_EQ(result.error, errors::kAmbiguous);

    left->SetFocus("memory", &leftView);
    // Focus times are milliseconds: the second one is later
    Pump([&]() { return QDateTime::currentMSecsSinceEpoch() > left->FocusRecords().value("memory").toObject().value("at").toInteger(); });
    right->SetFocus("memory", &rightView);
    result = choose(chosen);
    ASSERT_TRUE(result.ok) << result.message.toStdString();
    const std::vector<discovery::Entry> entries = discovery::List(_directory);
    ASSERT_LT(chosen, entries.size());
    EXPECT_EQ(entries[chosen].id, right->Id());
}

TEST_F(GuiControl_Test, StateIsCoalesced)
{
    std::unique_ptr<Host> host = MakeHost();
    Node& view = AddView(*host, "debugger", "disasm", "disassembly");
    Client client;
    ASSERT_TRUE(client.Connect(host->SocketName()));
    ASSERT_TRUE(client.Subscribe({"state"}, 10));

    for (int i = 0; i <= 200; ++i)
        view.SetState("address", i);

    // One push at once (the first change), one more at the rate's end with the last value
    int last = -1;
    int pushes = 0;
    ASSERT_TRUE(Pump(
        [&]() {
            while (std::optional<QJsonObject> event = client.NextEvent(0))
            {
                ++pushes;
                EXPECT_EQ(event->value("target").toString(), "debugger/disasm");
                last = event->value("state").toObject().value("address").toInt();
            }
            return last == 200;
        },
        1000));
    EXPECT_LE(pushes, 2);
}

TEST_F(GuiControl_Test, NodesFocusAndActivityPushes)
{
    std::unique_ptr<Host> host = MakeHost();
    Client watcher;
    ASSERT_TRUE(watcher.Connect(host->SocketName()));
    ASSERT_TRUE(watcher.Subscribe({"nodes", "focus", "activity"}));

    Node& view = AddView(*host, "debugger", "memory1", "memory");
    host->SetFocus("memory", &view);
    Client caller;
    ASSERT_TRUE(caller.Connect(host->SocketName()));
    ASSERT_TRUE(caller.Call("memory.probe").ok);

    QStringList seen;
    ASSERT_TRUE(Pump(
        [&]() {
            while (std::optional<QJsonObject> event = watcher.NextEvent(0))
                seen.append(event->value("topic").toString());
            return seen.contains("activity");
        },
        1000));
    EXPECT_TRUE(seen.contains("nodes"));
    EXPECT_TRUE(seen.contains("focus"));
}

TEST_F(GuiControl_Test, SlowClientIsDropped)
{
    std::unique_ptr<Host> host = MakeHost(-1, 64 * 1024);
    Node& view = AddView(*host, "debugger", "disasm", "disassembly");
    QString reason;
    QObject::connect(host.get(), &Host::clientDropped, [&reason](const QString& why) { reason = why; });

    // A client that reads nothing: Qt takes at most 1 KB off the socket, the rest piles up at the host
    QLocalSocket slow;
    slow.setReadBufferSize(1024);
    slow.connectToServer(host->SocketName());
    ASSERT_TRUE(slow.waitForConnected(1000));
    slow.write(Encode(QJsonObject{{"op", "subscribe"}, {"rid", 1}, {"topics", QJsonArray{"state"}}, {"max_hz", 50}}));
    slow.flush();
    ASSERT_TRUE(Pump([&]() { return host->ClientCount() == 1; }));

    const QString big(256 * 1024, QChar('x'));
    int round = 0;
    ASSERT_TRUE(Pump(
        [&]() {
            view.SetState("text", big + QString::number(round++));
            return !reason.isEmpty();
        },
        1000));
    EXPECT_EQ(reason, "slow_consumer");
    EXPECT_EQ(host->ClientCount(), 0);
}

TEST_F(GuiControl_Test, TcpListener)
{
    std::unique_ptr<Host> host = MakeHost(0);
    ASSERT_GT(host->TcpPort(), 0);
    AddView(*host, "debugger", "memory1", "memory");
    EXPECT_EQ(discovery::List(_directory).front().tcpPort, host->TcpPort());

    Client client;
    QString error;
    ASSERT_TRUE(client.ConnectTcp("127.0.0.1", host->TcpPort(), 1000, &error)) << error.toStdString();
    const Result result = client.Call("memory.probe");
    EXPECT_TRUE(result.ok) << result.message.toStdString();
}

TEST_F(GuiControl_Test, WindowBinding)
{
    std::unique_ptr<Host> host = MakeHost();
    auto* window = new QWidget();
    window->setWindowTitle("Debugger");
    window->resize(300, 200);
    new WindowBinding(*host, window, "debugger");
    window->show();

    Client client;
    ASSERT_TRUE(client.Connect(host->SocketName()));
    Result result = client.Call("window.move", "debugger", QJsonObject{{"x", 40}, {"y", 50}, {"w", 320}, {"h", 240}});
    ASSERT_TRUE(result.ok) << result.message.toStdString();
    EXPECT_EQ(window->size(), QSize(320, 240));
    const Node* node = host->Root().Child("debugger");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->State().value("title").toString(), "Debugger");
    EXPECT_EQ(node->State().value("geometry").toObject().value("w").toInt(), 320);
    EXPECT_TRUE(node->State().value("visible").toBool());

    result = client.Call("window.move", "window", QJsonObject{{"w", "wide"}});
    EXPECT_EQ(result.error, errors::kBadRequest);

    const QString path = _directory + "/shot.png";
    result = client.Call("window.screenshot", "debugger", QJsonObject{{"path", path}});
    ASSERT_TRUE(result.ok) << result.message.toStdString();
    EXPECT_TRUE(QFile::exists(path));
    EXPECT_GT(result.body.value("width").toInt(), 0);

    ASSERT_TRUE(client.Call("window.hide", "debugger").ok);
    EXPECT_FALSE(window->isVisible());

    // The node goes with the window
    delete window;
    EXPECT_EQ(host->Root().Child("debugger"), nullptr);
    EXPECT_EQ(client.Call("window.raise").error, errors::kNotFound);
}

TEST_F(GuiControl_Test, InProcessInvoke)
{
    std::unique_ptr<Host> host = MakeHost();
    AddView(*host, "debugger", "memory1", "memory");
    std::optional<Result> answer;
    host->Invoke("memory.probe", "memory", QJsonObject{{"value", 3}}, "binding", [&answer](Result result) {
        answer = std::move(result);
    });
    ASSERT_TRUE(answer.has_value());
    EXPECT_TRUE(answer->ok);
    EXPECT_EQ(answer->body.value("echo").toInt(), 3);
}

TEST_F(GuiControl_Test, StateCallByAnyTarget)
{
    std::unique_ptr<Host> host = MakeHost();
    Node& view = AddView(*host, "debugger", "memory1", "memory");
    view.SetState(QJsonObject{{"top", "0x4000"}});
    view.SetInstance("http://127.0.0.1:8090", "emu-1");
    Client client;
    ASSERT_TRUE(client.Connect(host->SocketName(), 1000));

    // A node without the method asked: the path, the interface, the interface on a machine
    for (const QString& target : {QStringLiteral("debugger/memory1"), QStringLiteral("memory"),
                                  QStringLiteral("memory@emu-1")})
    {
        const Result state = client.Call("state", {}, QJsonObject{{"target", target}});
        ASSERT_TRUE(state.ok) << target.toStdString() << ": " << state.message.toStdString();
        EXPECT_EQ(state.body.value("target").toString(), "debugger/memory1");
        EXPECT_EQ(state.body.value("state").toObject().value("top").toString(), "0x4000");
        EXPECT_EQ(state.body.value("instance").toObject().value("id").toString(), "emu-1");
    }
    EXPECT_EQ(client.Call("state", {}, QJsonObject{{"target", "nowhere"}}).error, errors::kNotFound);
    EXPECT_EQ(client.Call("state").error, errors::kBadRequest);
}

TEST_F(GuiControl_Test, ParamHelpers)
{
    Result failure;
    quint16 address = 0;
    for (const auto& [text, expected] : std::vector<std::pair<QJsonValue, int>>{
             {QJsonValue("0x6A00"), 0x6A00}, {QJsonValue("#6a00"), 0x6A00}, {QJsonValue("$FFFF"), 0xFFFF},
             {QJsonValue("6A00h"), 0x6A00}, {QJsonValue("27136"), 0x6A00}, {QJsonValue(27136), 0x6A00}})
    {
        ASSERT_TRUE(params::Address(QJsonObject{{"addr", text}}, "addr", address, failure))
            << failure.message.toStdString();
        EXPECT_EQ(address, expected);
    }
    for (const QJsonValue& bad : {QJsonValue("0x10000"), QJsonValue(-1), QJsonValue("ram"), QJsonValue(1.5)})
        EXPECT_FALSE(params::Address(QJsonObject{{"addr", bad}}, "addr", address, failure));
    EXPECT_FALSE(params::Address(QJsonObject{}, "addr", address, failure));
    EXPECT_EQ(failure.error, errors::kBadRequest);
    EXPECT_EQ(params::Hex(0x6A00), "0x6A00");

    int dx = 0;
    int dy = 0;
    ASSERT_TRUE(params::Direction(QJsonObject{{"dir", "left"}}, dx, dy, failure));
    EXPECT_EQ(dx, -1);
    EXPECT_EQ(dy, 0);
    EXPECT_FALSE(params::Direction(QJsonObject{{"dir", "back"}}, dx, dy, failure));

    int pages = 0;
    ASSERT_TRUE(params::Pages(QJsonObject{}, pages, failure));
    EXPECT_EQ(pages, 1);
    ASSERT_TRUE(params::Pages(QJsonObject{{"n", -3}}, pages, failure));
    EXPECT_EQ(pages, -3);
    EXPECT_FALSE(params::Pages(QJsonObject{{"n", 0}}, pages, failure));
    EXPECT_FALSE(params::Pages(QJsonObject{{"n", 1000}}, pages, failure));

    bool on = false;
    ASSERT_TRUE(params::Toggle(QJsonObject{}, true, on, failure));
    EXPECT_FALSE(on);  // absent: flip
    ASSERT_TRUE(params::Toggle(QJsonObject{{"on", true}}, true, on, failure));
    EXPECT_TRUE(on);
    ASSERT_TRUE(params::Toggle(QJsonObject{{"on", "off"}}, true, on, failure));  // a word from the CLI
    EXPECT_FALSE(on);
    EXPECT_FALSE(params::Toggle(QJsonObject{{"on", "maybe"}}, true, on, failure));
}

TEST_F(GuiControl_Test, ProgramInterface)
{
    // The declared interfaces carry the debugger's; a program adds its own, a second add replaces the first
    ASSERT_NE(FindCommand(u"memory.goto"), nullptr);
    EXPECT_TRUE(FindCommand(u"memory.cursor_move")->repeats);
    EXPECT_FALSE(FindCommand(u"memory.goto")->repeats);
    AddProgramInterface({"testprog", Level::Host, {{"testprog.first", "First", {}, "one"}}, {}, "a test program"});
    AddProgramInterface({"testprog", Level::Host, {{"testprog.second", "Second", {}, "two"}}, {}, "a test program"});
    EXPECT_EQ(FindCommand(u"testprog.first"), nullptr);
    ASSERT_NE(FindCommand(u"testprog.second"), nullptr);
    EXPECT_EQ(InterfaceOf(u"testprog.second"), "testprog");
    size_t count = 0;
    for (const InterfaceSpec& spec : Interfaces())
        count += QLatin1String(spec.name) == QLatin1String("testprog") ? 1 : 0;
    EXPECT_EQ(count, 1u);
}

TEST_F(GuiControl_Test, ParamsAreChecked)
{
    // A declared command: unknown and missing parameters refused with what it takes; the handler never runs
    std::unique_ptr<Host> host = MakeHost();
    Node& view = host->Root().AddChild("debugger").AddChild("memory1");
    view.AddInterface("memory");
    int runs = 0;
    view.SetCommand("memory.goto", [&runs](const Call&) {
        ++runs;
        return Result::Success();
    });
    Client client;
    ASSERT_TRUE(client.Connect(host->SocketName(), 1000));

    Result result = client.Call("memory.goto", {}, QJsonObject{{"file", "x"}});
    EXPECT_EQ(result.error, errors::kBadRequest);
    EXPECT_EQ(result.message.toStdString(),
              "memory.goto: unknown parameter file; missing addr; memory.goto takes addr (an address: decimal, 0x.., "
              "#.., $.. or ..h)");
    EXPECT_EQ(result.body.value("unknown").toArray(), QJsonArray{"file"});
    EXPECT_EQ(result.body.value("missing").toArray(), QJsonArray{"addr"});
    EXPECT_EQ(client.Call("memory.goto", {}, QJsonObject{{"addr", 1}, {"space", "ram"}}).error, errors::kBadRequest);
    EXPECT_EQ(runs, 0);
    EXPECT_TRUE(client.Call("memory.goto", {}, QJsonObject{{"addr", 1}}).ok);
    EXPECT_EQ(runs, 1);

    // The same in process (the controller bindings)
    std::optional<Result> answer;
    host->Invoke("memory.goto", "memory", QJsonObject{{"adr", 1}}, "binding", [&answer](Result r) { answer = r; });
    ASSERT_TRUE(answer.has_value());
    EXPECT_TRUE(answer->message.contains("unknown parameter adr")) << answer->message.toStdString();
    EXPECT_TRUE(Host::CheckParams("window.raise", QJsonObject{}).ok);
    EXPECT_TRUE(Host::CheckParams("window.raise", QJsonObject{{"x", 1}}).message.contains("takes no parameters"));
}

// The core bridge (design §4.10): what the core's GuiPlane calls. From another thread (the WebAPI's) the call goes to
// the pool and reaches the host over its socket while the host's thread serves; on the host's thread (a script the
// GUI runs) it runs there. list, state and a command answer as `unreal-gui` does
TEST_F(GuiControl_Test, CoreBridgeCallsFromAnyThread)
{
    const QByteArray previous = qgetenv("UNREAL_GUI_CONTROL_DIR");
    qputenv("UNREAL_GUI_CONTROL_DIR", _directory.toUtf8());
    {
        std::unique_ptr<Host> host = MakeHost();
        AddView(*host, QStringLiteral("debugger"), QStringLiteral("memory"), QStringLiteral("memory"));

        auto parse = [](const std::string& text) {
            return QJsonDocument::fromJson(QByteArray::fromStdString(text)).object();
        };
        // From a thread of its own: the answer comes from a pool thread while this thread pumps the host
        std::atomic<bool> answered{false};
        std::string answer;
        std::thread caller([&answered, &answer]() {
            corebridge::Call("memory.probe", "debugger/memory", R"({"value": 7})", [&](const std::string& result) {
                answer = result;
                answered = true;
            });
        });
        EXPECT_TRUE(Pump([&answered] { return answered.load(); }, 5000));
        caller.join();
        QJsonObject result = parse(answer);
        EXPECT_TRUE(result.value("ok").toBool()) << answer;
        EXPECT_EQ(result.value("body").toObject().value("path").toString(), QStringLiteral("debugger/memory"));
        EXPECT_EQ(result.value("body").toObject().value("echo").toInt(), 7);

        // On the host's thread: inline, the own host served by the client's local event loop
        std::string inlineAnswer;
        corebridge::Call("state", "", R"({"target": "memory"})",
                         [&inlineAnswer](const std::string& text) { inlineAnswer = text; });
        result = parse(inlineAnswer);
        EXPECT_TRUE(result.value("ok").toBool()) << inlineAnswer;
        EXPECT_EQ(result.value("body").toObject().value("target").toString(), QStringLiteral("debugger/memory"));

        std::string list;
        corebridge::Call("list", "", "{}", [&list](const std::string& text) { list = text; });
        result = parse(list);
        ASSERT_EQ(result.value("body").toObject().value("hosts").toArray().size(), 1) << list;
        EXPECT_EQ(result.value("body").toObject().value("hosts").toArray()[0].toObject().value("program").toString(),
                  QStringLiteral("guicontrol-test"));

        // Refusals pass through; parameters that are not an object are refused before any host is asked
        std::string missing;
        corebridge::Call("memory.probe", "nowhere/none", "{}", [&missing](const std::string& text) { missing = text; });
        EXPECT_EQ(parse(missing).value("error").toString(), QLatin1String(errors::kNotFound)) << missing;
        std::string bad;
        corebridge::Call("memory.probe", "", "[1]", [&bad](const std::string& text) { bad = text; });
        EXPECT_EQ(parse(bad).value("error").toString(), QLatin1String(errors::kBadRequest)) << bad;

        EXPECT_NE(corebridge::CatalogueJson().find("unreal-ng-gui-catalogue"), std::string::npos);
    }
    if (previous.isNull())
        qunsetenv("UNREAL_GUI_CONTROL_DIR");
    else
        qputenv("UNREAL_GUI_CONTROL_DIR", previous);
}
