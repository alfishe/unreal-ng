// The GUI control plane (guicontrol/): a host and its clients on one thread, the window binding on an offscreen
// widget, discovery in a scratch directory. Built into unreal-qt-tests (it needs the QApplication of that runner).

#include "_helpers/testpathhelper.h"

#include "guicontrol/client.h"
#include "guicontrol/discovery.h"
#include "guicontrol/host.h"
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

#include <functional>
#include <memory>
#include <optional>

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

    // No view shows that machine: the focused one
    result = client.Call("memory.probe", "memory@cccc");
    ASSERT_TRUE(result.ok) << result.message.toStdString();
    EXPECT_EQ(result.body.value("path").toString(), "debugger/memory2");

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
