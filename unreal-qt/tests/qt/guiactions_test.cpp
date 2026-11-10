/// @file guiactions_test.cpp
/// @brief A controller binding naming a GUI control plane command runs on the program's host in process (design
/// §4.7): the device is the caller, a view command goes to the view showing the device's machine, a release does
/// nothing, a held repeating command repeats until released, an encoder turn calls once per step (at most 16), a
/// refusal and a missing host reach the status bar. No core registry and no machine are involved.

#include "controllers/guiactions.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QStringList>
#include <gtest/gtest.h>

#include <functional>
#include <memory>

#include "_helpers/testpathhelper.h"
#include "controllers/hostcontrollers.h"
#include "guicontrol/host.h"
#include "guicontrol/node.h"

using namespace guicontrol;

namespace
{

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

class GuiActions_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        HostOptions options;
        options.program = QStringLiteral("guiactions-test");
        options.version = QStringLiteral("1.0");
        options.directory = QString::fromStdString(TestPathHelper::GetUniqueTestScratchPath("guiactions"));
        QDir().mkpath(options.directory);
        _host = std::make_unique<Host>(std::move(options));
        QString error;
        ASSERT_TRUE(_host->Start(&error)) << error.toStdString();
        // Two memory views of two machines: each command answers which one it ran on, with the caller
        for (const char* instance : {"machine-a", "machine-b"})
        {
            Node& view = _host->Root().AddChild("debugger").AddChild(QStringLiteral("memory-%1").arg(instance));
            view.AddInterface(QStringLiteral("memory"));
            view.SetInstance(QStringLiteral("http://127.0.0.1:8090"), QString::fromLatin1(instance));
            const QString name = QString::fromLatin1(instance);
            view.SetCommand("memory.goto", [this, name](const Call& call) {
                _calls << name + ' ' + call.client + ' ' + QString::number(call.params.value("addr").toInt());
                return Result::Success();
            });
            view.SetCommand("memory.cursor_move", [this, name](const Call& call) {
                _calls << name + " move " + call.params.value("dir").toString();
                return Result::Success();
            });
        }
        GuiActionHooks hooks;
        hooks.notice = [this](const QString& text) { _notices << text; };
        _actions = std::make_unique<GuiActions>(nullptr, std::move(hooks));
        _actions->SetGuiHost(_host.get());
    }

    static HostActionEvent Press(const QString& action, const QString& params, const QString& instance)
    {
        HostActionEvent event;
        event.action = action;
        event.pressed = true;
        event.value = 1;
        event.device = QStringLiteral("pad:test");
        event.params = params;
        event.instance = instance;
        return event;
    }

    std::unique_ptr<Host> _host;
    std::unique_ptr<GuiActions> _actions;
    QStringList _calls;
    QStringList _notices;
};

}  // namespace

TEST_F(GuiActions_Test, APressRunsTheCommandOnTheViewOfTheDevicesMachine)
{
    _actions->Trigger(Press("memory.goto", R"({"addr": 16384})", "machine-b"));
    ASSERT_TRUE(Pump([this] { return !_calls.isEmpty(); }));
    EXPECT_EQ(_calls, QStringList{"machine-b pad:test 16384"}) << "the view showing the device's machine, the device calls";

    // An explicit target among the parameters names the node instead
    _calls.clear();
    _actions->Trigger(Press("memory.goto", R"({"addr": 1, "target": "debugger/memory-machine-a"})", "machine-b"));
    ASSERT_TRUE(Pump([this] { return !_calls.isEmpty(); })) << _notices.join("; ").toStdString();
    EXPECT_EQ(_calls, QStringList{"machine-a pad:test 1"});

    // A release of a command that does not repeat: nothing
    _calls.clear();
    HostActionEvent release = Press("memory.goto", R"({"addr": 2})", "machine-a");
    release.pressed = false;
    release.value = 0;
    _actions->Trigger(release);
    Pump([] { return false; }, 20);
    EXPECT_TRUE(_calls.isEmpty()) << _calls.join("; ").toStdString();
    EXPECT_TRUE(_notices.isEmpty()) << _notices.join("; ").toStdString();
}

TEST_F(GuiActions_Test, AHeldRepeatingCommandRepeatsUntilReleased)
{
    const HostActionEvent press = Press("memory.cursor_move", R"({"dir": "down"})", "machine-a");
    _actions->Trigger(press);
    // Once at once, then after 300 ms every 60 ms
    ASSERT_TRUE(Pump([this] { return _calls.size() >= 3; }, 1000)) << _calls.size();
    HostActionEvent release = press;
    release.pressed = false;
    release.value = 0;
    _actions->Trigger(release);
    const qsizetype held = _calls.size();
    Pump([] { return false; }, 150);
    EXPECT_EQ(_calls.size(), held) << "no repeat after the release";
    for (const QString& call : _calls)
        EXPECT_EQ(call, "machine-a move down");
}

TEST_F(GuiActions_Test, AnEncoderTurnCallsOncePerStepAtMostSixteen)
{
    HostActionEvent turn = Press("memory.cursor_move", R"({"dir": "up"})", "machine-a");
    turn.pressed = false;
    turn.turn = true;
    turn.value = -3;
    _actions->Trigger(turn);
    ASSERT_TRUE(Pump([this] { return _calls.size() >= 3; }));
    Pump([] { return false; }, 20);
    EXPECT_EQ(_calls.size(), 3) << "three steps back are three calls";

    _calls.clear();
    turn.value = 40;
    _actions->Trigger(turn);
    ASSERT_TRUE(Pump([this] { return _calls.size() >= 16; }));
    Pump([] { return false; }, 20);
    EXPECT_EQ(_calls.size(), 16) << "a fast turn is capped";
}

TEST_F(GuiActions_Test, ARefusalAndAMissingHostReachTheStatusBar)
{
    _actions->Trigger(Press("memory.goto", R"({"adr": 1})", "machine-a"));
    ASSERT_TRUE(Pump([this] { return !_notices.isEmpty(); }));
    EXPECT_TRUE(_notices.front().startsWith("memory.goto: ")) << _notices.front().toStdString();
    EXPECT_TRUE(_notices.front().contains("unknown parameter adr")) << _notices.front().toStdString();
    EXPECT_TRUE(_calls.isEmpty());

    _notices.clear();
    _actions->SetGuiHost(nullptr);
    _actions->Trigger(Press("memory.goto", R"({"addr": 1})", "machine-a"));
    ASSERT_TRUE(Pump([this] { return !_notices.isEmpty(); }));
    EXPECT_EQ(_notices.front(), "memory.goto: the GUI control plane is off");
    EXPECT_TRUE(_calls.isEmpty());
}
