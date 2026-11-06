// Tools > Stream Deck... (Stream Deck integration daemon.md §7): the page shows the supervisor's state and the
// daemon's, changes the same settings every plane changes, and approves the daemon's pairing here - the GUI is the
// only approver (companion client design §2). The supervisor runs on the test's clock with a fake daemon.

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>

#include <memory>
#include <mutex>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "controllers/streamdeckpage.h"
#include "controllers/studiolauncher.h"
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
    std::string pack = "scenario";  ///< the deck's icon pack setting
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
    HttpResult Call(int, const std::string& method, const std::string& path, const std::string& body, int) override
    {
        if (!_d.up)
            return {0, "", "refused"};
        if (method == "GET" && path == "/api/v1/health")
            return {200,
                    R"({"ok":true,"version":"1.0","decks":1,"viewers":1,"surface":"connected","host":"http://127.0.0.1:8090","pid":)" +
                        std::to_string(_d.pid) + R"(,"pairing":)" + _d.pairing + "}",
                    ""};
        if (method == "GET" && path == "/api/v1/decks")
            return {200,
                    R"({"decks":[{"serial":"A1B2C3","model":"Stream Deck + XL","real":true,"brightness":55,)"
                    R"("pack":"magicwb","pack_setting":")" +
                        _d.pack + R"("}]})",
                    ""};
        if (method == "GET" && path == "/api/v1/packs")
            return {200, R"({"packs":[{"name":"magicwb","title":"MagicWB"},{"name":"os4","title":"OS4"}],)"
                         R"("default":"magicwb"})",
                    ""};
        if (method == "PUT" && path == "/api/v1/decks/A1B2C3/pack")
        {
            _d.pack = body.find("os4") != std::string::npos ? "os4" : "scenario";
            return {200, R"({"serial":"A1B2C3","pack":"os4","setting":")" + _d.pack + R"(","scenario_pack":""})", ""};
        }
        return {200, "{}", ""};
    }

private:
    Daemon& _d;
};

/// Stream Deck Studio as the launcher sees it: its automation port (answers or not), what was started
struct FakeStudio
{
    std::mutex mutex;
    std::string connection;  ///< GET /api/v1/connection's body; "" = the port does not answer
    std::vector<std::string> calls;
    std::vector<QStringList> launches;
    bool launchFails = false;
    qint64 now = 0;

    StudioLauncher::Hooks Hooks()
    {
        StudioLauncher::Hooks h;
        h.http = [this](const std::string& method, const std::string& path, const std::string&) {
            std::lock_guard lock(mutex);
            calls.push_back(method + " " + path);
            netsock::HttpReply r;
            if (connection.empty())
                r.error = "refused";
            else
                r = {200, method == "GET" ? connection : std::string("{}"), {}};
            return r;
        };
        h.launch = [this](const QString&, const QStringList& args, QString& error) {
            std::lock_guard lock(mutex);
            launches.push_back(args);
            if (launchFails)
                error = "cannot start it";
            return !launchFails;
        };
        h.clock = [this] { return now; };
        return h;
    }
    void Answer(const std::string& body)
    {
        std::lock_guard lock(mutex);
        connection = body;
    }
    bool Called(const std::string& call)
    {
        std::lock_guard lock(mutex);
        return std::find(calls.begin(), calls.end(), call) != calls.end();
    }
    size_t Launches()
    {
        std::lock_guard lock(mutex);
        return launches.size();
    }
};

const std::string kMirror = R"({"mode":"client","state":"joined","role":"mirror","deck":"A1B2C3",)"
                            R"("model_name":"Stream Deck + XL"})";

/// The answer of the probe in flight, on the GUI thread
bool Settled(StudioLauncher& studio)
{
    return TestWait::For([&] {
        QApplication::processEvents();
        return !studio.Probing();
    });
}
} // namespace

TEST(StudioLauncher_Test, OpensStudioShowsItStartingThenWhatItsDeckShowsAndRaisesItWhenItRuns)
{
    FakeStudio fake;
    StudioLauncher studio(fake.Hooks(), "/apps/Stream Deck Studio.app");
    studio.Probe();
    ASSERT_TRUE(Settled(studio));
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Off);
    EXPECT_EQ(studio.ButtonText(), "Open Studio");
    EXPECT_TRUE(studio.CanOpen());

    studio.SetDaemonPorts(37150, 37153);
    studio.Open();
    ASSERT_EQ(fake.Launches(), 1u);
    EXPECT_EQ(fake.launches[0], (QStringList{"--daemon", "127.0.0.1:37150", "--daemon-api", "37153"}))
        << "it joins the daemon unreal-qt supervises";
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Starting);
    EXPECT_FALSE(studio.CanOpen()) << "no second start while the first comes up";
    ASSERT_TRUE(Settled(studio));
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Starting) << "the port does not answer yet";

    fake.Answer(kMirror);
    studio.Probe();
    ASSERT_TRUE(Settled(studio));
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Running);
    EXPECT_EQ(studio.StatusText(), "Running: mirrors deck A1B2C3 (Stream Deck + XL)");
    EXPECT_EQ(studio.ButtonText(), "Show Studio");

    studio.Open();
    EXPECT_TRUE(TestWait::For([&] { return fake.Called("PUT /api/v1/window"); })) << "its window to the front";
#ifdef Q_OS_MACOS
    EXPECT_EQ(fake.Launches(), 2u) << "Launch Services activates the running app";
    EXPECT_TRUE(fake.launches[1].isEmpty());
#else
    EXPECT_EQ(fake.Launches(), 1u) << "not started twice";
#endif

    fake.Answer("");  // closed
    studio.Probe();
    ASSERT_TRUE(Settled(studio));
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Off);
    EXPECT_EQ(studio.ButtonText(), "Open Studio");
}

TEST(StudioLauncher_Test, AStartThatFailsOrNeverAnswersSaysSoAndMayBeTriedAgain)
{
    FakeStudio fake;
    fake.launchFails = true;
    StudioLauncher studio(fake.Hooks(), "/bin/unreal-streamdeck-studio");
    studio.Open();
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Failed);
    EXPECT_EQ(studio.StatusText(), "Did not start: cannot start it");
    EXPECT_TRUE(studio.CanOpen());

    fake.launchFails = false;
    studio.Open();
    ASSERT_TRUE(Settled(studio));
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Starting);
    fake.now += StudioLauncher::kStartTimeoutMs;
    studio.Probe();
    ASSERT_TRUE(Settled(studio));
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Failed);
    EXPECT_TRUE(studio.StatusText().contains("did not answer on 127.0.0.1:37052"))
        << studio.StatusText().toStdString();
    studio.Probe();
    ASSERT_TRUE(Settled(studio));
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Failed) << "said until the next start";

    // a Studio whose own port is off: the daemon counts its window
    studio.SetViewers(1);
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Running);
    studio.SetViewers(0);
    EXPECT_EQ(studio.GetState(), StudioLauncher::State::Off);

    StudioLauncher missing(fake.Hooks(), "");
    EXPECT_EQ(missing.GetState(), StudioLauncher::State::Missing);
    EXPECT_FALSE(missing.CanOpen());
    missing.Open();
    EXPECT_EQ(fake.Launches(), 2u) << "nothing to start";
}

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

TEST_F(StreamDeckDialog_Test, ADecksIconPackIsChosenHereAndKept)
{
    Supervisor::Instance().Step(0);
    Supervisor::Instance().Step(200);
    StreamDeckDialog dialog(nullptr);
    // the daemon's packs come on a worker thread: the row is built again with them
    ASSERT_TRUE(TestWait::For([&] {
        QApplication::processEvents();
        return dialog.decksTable()->rowCount() == 1 && dialog.packBox(0) && dialog.packBox(0)->count() == 3;
    })) << "its scenario's, then the daemon's two packs";
    QComboBox* pack = dialog.packBox(0);
    EXPECT_EQ(pack->currentData().toString(), "scenario");
    EXPECT_EQ(pack->itemData(2).toString(), "os4");

    pack->setCurrentIndex(2);
    emit pack->activated(2);
    ASSERT_TRUE(TestWait::For([&] {
        QApplication::processEvents();
        return Supervisor::Instance().GetSettings().pack.count("A1B2C3") == 1;
    })) << "set at the daemon, kept for that deck (the same verb every plane calls)";
    EXPECT_EQ(_daemon.pack, "os4");
    EXPECT_EQ(Supervisor::Instance().GetSettings().pack.at("A1B2C3"), "os4");

    // set elsewhere (the studio): the box follows the daemon's report
    _daemon.pack = "scenario";
    Supervisor::Instance().Step(2200);
    dialog.refresh();
    EXPECT_EQ(dialog.packBox(0)->currentData().toString(), "scenario");
    EXPECT_TRUE(Supervisor::Instance().GetSettings().pack.empty()) << "the deck's setting cleared";
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

TEST_F(StreamDeckDialog_Test, TheStudioButtonOpensStudioAndTheLineFollowsIt)
{
    FakeStudio fake;
    auto* studio = new StudioLauncher(fake.Hooks(), "/apps/Stream Deck Studio.app");
    StreamDeckDialog dialog(nullptr, nullptr, studio);
    ASSERT_TRUE(Settled(*studio));
    EXPECT_TRUE(dialog.studioLabel()->text().contains("Not running")) << dialog.studioLabel()->text().toStdString();
    EXPECT_EQ(dialog.studioButton()->text(), "Open Studio");

    dialog.studioButton()->click();
    EXPECT_EQ(fake.Launches(), 1u);
    EXPECT_TRUE(dialog.studioLabel()->text().contains("Starting")) << dialog.studioLabel()->text().toStdString();
    EXPECT_FALSE(dialog.studioButton()->isEnabled());

    fake.Answer(kMirror);
    ASSERT_TRUE(Settled(*studio));
    dialog.refresh();
    ASSERT_TRUE(Settled(*studio));
    EXPECT_TRUE(dialog.studioLabel()->text().contains("mirrors deck A1B2C3"))
        << dialog.studioLabel()->text().toStdString();
    EXPECT_EQ(dialog.studioButton()->text(), "Show Studio");
    EXPECT_TRUE(dialog.studioButton()->isEnabled());
}
