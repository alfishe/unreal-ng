// Tools > Stream Deck... (Stream Deck integration daemon.md §7): the page shows the supervisor's state and the
// daemon's, changes the same settings every plane changes, and approves the daemon's pairing here - the GUI is the
// only approver (companion client design §2). The supervisor runs on the test's clock with a fake daemon.

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

#include "controllers/streamdeckpage.h"
#include "emulator/companions/streamdecksupervisor.h"
#include "emulator/execution/companionregistry.h"

using namespace companion::streamdeck;

namespace
{
struct Daemon
{
    bool up = false;
    int pid = 0;
    std::string pairing = R"({"state":"idle","paired":false,"companion_id":""})";
};

class FakeProcess : public IProcess
{
public:
    explicit FakeProcess(int pid) : _pid(pid) {}
    int Pid() const override { return _pid; }
    bool Exited(int&, bool&) override { return false; }
    void Terminate() override {}
    void Kill() override {}

private:
    int _pid;
};

class FakeLauncher : public ILauncher
{
public:
    explicit FakeLauncher(Daemon& d) : _d(d) {}
    std::unique_ptr<IProcess> Start(const LaunchSpec&, std::string&) override
    {
        _d.up = true;
        _d.pid = 2001;
        return std::make_unique<FakeProcess>(2001);
    }

private:
    Daemon& _d;
};

class FakeHttp : public IDaemonHttp
{
public:
    explicit FakeHttp(Daemon& d) : _d(d) {}
    HttpResult Call(int, const std::string& method, const std::string& path, const std::string&, int) override
    {
        if (!_d.up)
            return {0, "", "refused"};
        if (method == "GET" && path == "/api/v1/health")
            return {200,
                    R"({"ok":true,"version":"1.0","decks":1,"viewers":1,"surface":"connected","host":"http://127.0.0.1:8090","pid":)" +
                        std::to_string(_d.pid) + R"(,"pairing":)" + _d.pairing + "}",
                    ""};
        if (method == "GET" && path == "/api/v1/decks")
            return {200, R"({"decks":[{"serial":"A1B2C3","model":"Stream Deck + XL","real":true,"brightness":55}]})", ""};
        return {200, "{}", ""};
    }

private:
    Daemon& _d;
};
} // namespace

class StreamDeckDialog_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Supervisor::Instance().ResetForTesting();
        companion::CompanionRegistry::Instance().ResetForTesting();
        companion::CompanionRegistry::Instance().SetApprovalAvailable(true);
        Supervisor::Instance().SetLauncher(std::make_shared<FakeLauncher>(_daemon));
        Supervisor::Instance().SetHttp(std::make_shared<FakeHttp>(_daemon));
        Host host;
        host.ownPid = 4242;
        host.defaultBinary = "/bin/unreal-streamdeck";
        host.decksAttached = [] { return 1; };
        Supervisor::Instance().Attach(host, Settings{}, false);
    }
    void TearDown() override
    {
        Supervisor::Instance().ResetForTesting();
        companion::CompanionRegistry::Instance().ResetForTesting();
    }
    Daemon _daemon;
};

TEST_F(StreamDeckDialog_Test, ShowsTheDaemonAndChangesTheSettingsEveryPlaneSees)
{
    StreamDeckDialog dialog(nullptr);
    dialog.refresh();
    EXPECT_TRUE(dialog.stateLabel()->text().contains("Waiting")) << dialog.stateLabel()->text().toStdString();
    EXPECT_EQ(dialog.modeBox()->currentData().toString(), "deck");
    EXPECT_EQ(dialog.activityBox()->currentData().toString(), "flash") << "Q-3: a key flash by default";

    Supervisor::Instance().Step(0);    // a deck is attached: started
    Supervisor::Instance().Step(200);  // it answers
    dialog.refresh();
    EXPECT_TRUE(dialog.stateLabel()->text().contains("Running")) << dialog.stateLabel()->text().toStdString();
    ASSERT_EQ(dialog.decksTable()->rowCount(), 1);
    EXPECT_EQ(dialog.decksTable()->item(0, 0)->text(), "A1B2C3");
    EXPECT_FALSE(dialog.approveButton()->isEnabled()) << "nothing to approve";

    dialog.activityBox()->setCurrentIndex(dialog.activityBox()->findData("line"));
    emit dialog.activityBox()->activated(dialog.activityBox()->currentIndex());
    EXPECT_EQ(Supervisor::Instance().GetSettings().activity, "line");
    dialog.modeBox()->setCurrentIndex(dialog.modeBox()->findData("always"));
    emit dialog.modeBox()->activated(dialog.modeBox()->currentIndex());
    EXPECT_EQ(Supervisor::Instance().GetSettings().mode, Mode::Always);
}

TEST_F(StreamDeckDialog_Test, TheDaemonsPairingIsApprovedHere)
{
    auto& registry = companion::CompanionRegistry::Instance();
    const companion::PairingRequestResult request =
        registry.RequestPairing("Stream Deck daemon", "Stream Deck daemon (unreal-streamdeck)", 0, "127.0.0.1");
    ASSERT_EQ(request.error, "") << request.message;
    const std::string code = registry.CodeOf(request.pairing.id);
    _daemon.pairing = R"({"state":"pending","pairing_id":")" + request.pairing.id + R"(","code":")" + code +
                      R"(","paired":false,"companion_id":""})";
    Supervisor::Instance().Step(0);
    Supervisor::Instance().Step(200);

    StreamDeckDialog dialog(nullptr);
    dialog.refresh();
    EXPECT_TRUE(dialog.approveButton()->isEnabled());
    EXPECT_TRUE(dialog.pairingLabel()->text().contains(QString::fromStdString(code.substr(0, 3))))
        << "the code to compare with the deck's strip";
    dialog.approveButton()->click();
    EXPECT_EQ(registry.PollPairing(request.pairing.id).state, "approved") << "approved at the GUI";
}
