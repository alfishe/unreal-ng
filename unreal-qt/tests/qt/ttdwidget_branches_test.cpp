// The TTD panel's branches (TTD branches surfaces.md §1): nothing about branches shows while the session has the
// trunk alone; after a resume in the past (recording goes on as a new branch, the trunk keeps its old future) the
// Branches button names the count, the status line the branch the timeline ends with and where the old future
// stays, the slider marks the fork, and the Branches row holds one lane per branch with the actions on the chosen
// one (switch, rename, pin, delete)

#include <gtest/gtest.h>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdsession.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "widgets/branchlanes.h"
#include "widgets/journalspanslider.h"
#include "widgets/ttdwidget.h"

class TtdWidget_Branches_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(_settingsDir.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, _settingsDir.path());
        _machine = EmulatorManager::GetInstance()->CreateEmulatorWithModel("ttdwidget-branches", "48K", LoggerLevel::LogError);
        ASSERT_NE(_machine, nullptr);
        _machine->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _machine->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _ttd = _machine->GetContext()->pTimeTravelController;
    }

    void TearDown() override
    {
        _machine.reset();
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    ttd::TTDReply Run(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        return ttd::TTDControl(_machine->GetContext()).Execute({verb, std::move(options)});
    }

    /// 20 frames, then a resume at the 8th: 6 frames on a new branch (named by the fork's time, "+00:00:00.159"),
    /// the trunk keeps its old future
    void RecordAFork()
    {
        ASSERT_TRUE(_ttd->StartRecording());
        _start = _machine->GetContext()->emulatorState.frame_counter;
        _machine->RunNFrames(20, true);
        _ttd->StopRecording();
        ASSERT_TRUE(_ttd->SeekTo({_start + 8, 0}));
        ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 8, 0}));
        _machine->RunNFrames(6, true);
        _ttd->StopRecording();
    }

    /// Branch 1's name, as the session gives it
    QString Fork1() const
    {
        const ttd::TTDSessionInfo info = _ttd->ReadSessionInfo();
        const ttd::TTDBranchInfo* b = info.branches ? info.branches->Find(1) : nullptr;
        return b ? QString::fromStdString(b->meta.name) : QString();
    }

    static QToolButton* Tool(TtdWidget& w, const QString& prefix)
    {
        for (QToolButton* b : w.findChildren<QToolButton*>())
            if (b->text().startsWith(prefix))
                return b;
        return nullptr;
    }
    static QPushButton* Button(TtdWidget& w, const QString& text)
    {
        for (QPushButton* b : w.findChildren<QPushButton*>())
            if (b->text() == text)
                return b;
        return nullptr;
    }
    static QString Status(TtdWidget& w)
    {
        for (QLabel* l : w.findChildren<QLabel*>())
            if (l->text().contains(QStringLiteral("Memory")))
                return l->text();
        return {};
    }

    template <typename T>
    static T* Find(TtdWidget& w)
    {
        for (QWidget* c : w.findChildren<QWidget*>())
            if (T* t = dynamic_cast<T*>(c))
                return t;
        return nullptr;
    }

    /// A telemetry pass now (the timer's 100 ms, without waiting for it)
    void Refresh(TtdWidget& w)
    {
        w.updateState(nullptr);
        w.updateState(_machine);
    }

    QTemporaryDir _settingsDir;
    std::shared_ptr<Emulator> _machine;
    ttd::TimeTravelController* _ttd = nullptr;
    uint64_t _start = 0;
};

TEST_F(TtdWidget_Branches_Test, NothingShowsWithoutBranches)
{
    TtdWidget w;
    w.setVisibleByUser(true);
    w.updateState(_machine);
    ASSERT_TRUE(_ttd->StartRecording());
    _machine->RunNFrames(5, true);
    _ttd->StopRecording();
    Refresh(w);

    QToolButton* branches = Tool(w, QStringLiteral("Branches"));
    ASSERT_NE(branches, nullptr);
    EXPECT_FALSE(branches->isVisibleTo(&w)) << "no branch button while the history is one line";
    EXPECT_FALSE(Status(w).contains(QStringLiteral("Branch"))) << Status(w).toStdString();
    JournalSpanSlider* slider = Find<JournalSpanSlider>(w);
    ASSERT_NE(slider, nullptr);
    EXPECT_TRUE(slider->forkMarks().empty());
    EXPECT_TRUE(slider->branchSpans().empty());
}

TEST_F(TtdWidget_Branches_Test, AForkShowsTheButtonTheMarkAndTheNotice)
{
    TtdWidget w;
    w.setVisibleByUser(true);
    w.updateState(_machine);
    ASSERT_TRUE(_ttd->StartRecording());
    _start = _machine->GetContext()->emulatorState.frame_counter;
    _machine->RunNFrames(20, true);
    _ttd->StopRecording();
    Refresh(w);   // the panel sees the trunk alone first
    ASSERT_TRUE(_ttd->SeekTo({_start + 8, 0}));
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({_start + 8, 0}));
    _machine->RunNFrames(6, true);
    _ttd->StopRecording();
    Refresh(w);

    QToolButton* branches = Tool(w, QStringLiteral("Branches"));
    ASSERT_NE(branches, nullptr);
    EXPECT_TRUE(branches->isVisibleTo(&w));
    EXPECT_EQ(branches->text(), QStringLiteral("Branches (2)"));
    EXPECT_TRUE(Status(w).contains(QStringLiteral("Branch: %1 (2)").arg(Fork1()))) << Status(w).toStdString();
    EXPECT_TRUE(Status(w).contains(QStringLiteral("New branch '%1', previous future kept on 'trunk'").arg(Fork1())))
        << Status(w).toStdString();

    JournalSpanSlider* slider = Find<JournalSpanSlider>(w);
    ASSERT_NE(slider, nullptr);
    ASSERT_EQ(slider->forkMarks().size(), 1u) << "the trunk's old future goes on past frame 8";
    EXPECT_EQ(slider->forkMarks()[0].branches, (std::vector<uint32_t>{0}));
    EXPECT_TRUE(slider->forkMarks()[0].tip.contains(QStringLiteral("trunk - ")))
        << slider->forkMarks()[0].tip.toStdString();
    EXPECT_EQ(slider->branchSpans().size(), 1u) << "the new branch's part of the line is tinted, the trunk's is not";
}

TEST_F(TtdWidget_Branches_Test, TheRowListsTheBranchesAndSwitches)
{
    TtdWidget w;
    w.setVisibleByUser(true);
    w.updateState(_machine);
    ASSERT_NO_FATAL_FAILURE(RecordAFork());
    Refresh(w);
    const int closed = w.desiredHeight();

    QToolButton* branches = Tool(w, QStringLiteral("Branches"));
    ASSERT_NE(branches, nullptr);
    branches->click();
    BranchLanes* lanes = Find<BranchLanes>(w);
    ASSERT_NE(lanes, nullptr);
    EXPECT_TRUE(lanes->isVisibleTo(&w));
    EXPECT_GT(w.desiredHeight(), closed) << "the row takes its height";
    ASSERT_EQ(lanes->lanes().size(), 2u);
    EXPECT_EQ(lanes->lanes()[0].name, QStringLiteral("trunk"));
    EXPECT_FALSE(lanes->lanes()[0].active);
    EXPECT_TRUE(lanes->lanes()[0].onLineage) << "the trunk holds the line's first 8 frames";
    EXPECT_EQ(lanes->lanes()[1].name, Fork1());
    EXPECT_EQ(Fork1(), QStringLiteral("+00:00:00.159")) << "8 frames of a 48K: 8 x 69888 T at 3.5 MHz";
    EXPECT_EQ(lanes->lanes()[1].depth, 1);
    EXPECT_TRUE(lanes->lanes()[1].active);

    // The active branch's own lane: no switch; a delete (it switches to the parent first)
    QPushButton* switchBtn = Button(w, QStringLiteral("Switch"));
    QPushButton* deleteBtn = Button(w, QStringLiteral("Delete"));
    ASSERT_NE(switchBtn, nullptr);
    ASSERT_NE(deleteBtn, nullptr);
    EXPECT_FALSE(switchBtn->isEnabled());
    EXPECT_TRUE(deleteBtn->isEnabled());

    // The trunk: a switch takes its old future back; it goes only with Clear
    lanes->setSelected(0);
    Refresh(w);
    EXPECT_TRUE(switchBtn->isEnabled());
    EXPECT_FALSE(deleteBtn->isEnabled());
    switchBtn->click();
    EXPECT_EQ(_ttd->ReadSessionInfo().activeBranch, ttd::kTTDTrunk);
    Refresh(w);
    EXPECT_TRUE(Status(w).contains(QStringLiteral("Branch: trunk (2)"))) << Status(w).toStdString();
    JournalSpanSlider* slider = Find<JournalSpanSlider>(w);
    ASSERT_NE(slider, nullptr);
    EXPECT_TRUE(slider->branchSpans().empty()) << "the trunk's line is not tinted";
    ASSERT_EQ(slider->forkMarks().size(), 1u) << "the new branch leaves the trunk at frame 8";
    EXPECT_EQ(slider->forkMarks()[0].branches, (std::vector<uint32_t>{1}));
    ASSERT_EQ(lanes->lanes().size(), 2u);
    EXPECT_TRUE(lanes->lanes()[0].active);
    EXPECT_FALSE(lanes->lanes()[1].onLineage);
}

TEST_F(TtdWidget_Branches_Test, PinAndDeleteFromTheRow)
{
    TtdWidget w;
    w.setVisibleByUser(true);
    w.updateState(_machine);
    ASSERT_NO_FATAL_FAILURE(RecordAFork());
    ASSERT_TRUE(Run("branch-switch", {{"branch", "trunk"}}).Ok()) << "off the line, so the branch can go";
    Refresh(w);
    Tool(w, QStringLiteral("Branches"))->click();
    BranchLanes* lanes = Find<BranchLanes>(w);
    ASSERT_NE(lanes, nullptr);
    lanes->setSelected(1);
    Refresh(w);

    int asked = 0;
    w.confirm = [this, &asked](const QString&, const QString& question) {
        ++asked;
        EXPECT_TRUE(question.contains(Fork1())) << question.toStdString();
        return true;
    };
    QPushButton* pin = Button(w, QStringLiteral("Pin"));
    ASSERT_NE(pin, nullptr);
    pin->click();
    EXPECT_TRUE(_ttd->ReadSessionInfo().branches->Find(1)->meta.pinned);
    EXPECT_NE(Button(w, QStringLiteral("Unpin")), nullptr);
    Button(w, QStringLiteral("Unpin"))->click();
    EXPECT_FALSE(_ttd->ReadSessionInfo().branches->Find(1)->meta.pinned);

    // Asked every time; then the trunk is alone and the row goes
    Button(w, QStringLiteral("Delete"))->click();
    EXPECT_EQ(asked, 1);
    EXPECT_EQ(_ttd->ReadSessionInfo().branches, nullptr);
    Refresh(w);
    EXPECT_FALSE(Tool(w, QStringLiteral("Branches"))->isVisibleTo(&w));
    EXPECT_FALSE(lanes->isVisibleTo(&w));
}

TEST_F(TtdWidget_Branches_Test, TheResumeButtonSaysBranchWhereAFutureFollows)
{
    TtdWidget w;
    w.setVisibleByUser(true);
    w.updateState(_machine);
    ASSERT_TRUE(_ttd->StartRecording());
    _start = _machine->GetContext()->emulatorState.frame_counter;
    _machine->RunNFrames(20, true);
    _ttd->StopRecording();
    Refresh(w);
    EXPECT_NE(Button(w, QStringLiteral("Continue Rec")), nullptr) << "at the end it goes on with the session";

    ASSERT_TRUE(_ttd->SeekTo({_start + 8, 0}));
    Refresh(w);
    QPushButton* branch = Button(w, QStringLiteral("Branch Here"));
    ASSERT_NE(branch, nullptr) << "a recorded future follows: the button forks";
    EXPECT_TRUE(branch->toolTip().contains(QStringLiteral("new branch")));
}

TEST_F(TtdWidget_Branches_Test, TheCrossDeletesABranchAfterTheQuestionButNeverTheTrunk)
{
    TtdWidget w;
    w.setVisibleByUser(true);
    w.updateState(_machine);
    ASSERT_NO_FATAL_FAILURE(RecordAFork());
    Refresh(w);
    Tool(w, QStringLiteral("Branches"))->click();
    BranchLanes* lanes = Find<BranchLanes>(w);
    ASSERT_NE(lanes, nullptr);
    lanes->resize(400, 2 * BranchLanes::kRowHeight);
    ASSERT_EQ(lanes->lanes().size(), 2u);
    auto clickCross = [&](int row) {
        const QPointF at = QRectF(lanes->closeRect(row)).center();
        QMouseEvent press(QEvent::MouseButtonPress, at, lanes->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(lanes, &press);
    };
    bool answer = false;
    int asked = 0;
    w.confirm = [&](const QString&, const QString&) {
        ++asked;
        return answer;
    };

    EXPECT_TRUE(lanes->closeRect(0).isEmpty()) << "the trunk has no cross: it goes only with Clear";
    ASSERT_FALSE(lanes->closeRect(1).isEmpty());
    ASSERT_TRUE(lanes->closable(1));

    // The timeline runs on branch 1: asked, the question says the panel switches to the trunk first; No keeps it
    QString question;
    w.confirm = [&](const QString&, const QString& q) {
        ++asked;
        question = q;
        return answer;
    };
    clickCross(1);
    EXPECT_EQ(asked, 1);
    EXPECT_TRUE(question.contains(QStringLiteral("switches to 'trunk' first"))) << question.toStdString();
    ASSERT_NE(_ttd->ReadSessionInfo().branches, nullptr) << "No keeps the branch";
    EXPECT_EQ(_ttd->ReadSessionInfo().activeBranch, 1u) << "and the timeline where it was";

    // Yes: the switch to the trunk, then the delete
    answer = true;
    clickCross(1);
    EXPECT_EQ(asked, 2);
    EXPECT_EQ(_ttd->ReadSessionInfo().branches, nullptr);
    EXPECT_EQ(_ttd->ReadSessionInfo().activeBranch, ttd::kTTDTrunk);
}

/// Continue Rec at the end of a stopped recording while the machine runs on its own thread: the click returns (it
/// used to call Emulator::Start(), which ran the main loop on the GUI thread and froze the window) and the
/// recording goes on. A watchdog ends the run if the click never returns
TEST_F(TtdWidget_Branches_Test, ContinueRecReturnsAndRecords)
{
    TtdWidget w;
    w.setVisibleByUser(true);
    w.updateState(_machine);
    ASSERT_TRUE(_ttd->StartRecording());
    _machine->RunNFrames(10, true);
    _ttd->StopRecording();
    _machine->StartAsync();
    Refresh(w);
    QPushButton* cont = Button(w, QStringLiteral("Continue Rec"));
    ASSERT_NE(cont, nullptr);

    std::mutex mutex;
    std::condition_variable done;
    bool returned = false;
    std::thread watchdog([&] {
        std::unique_lock lock(mutex);
        if (!done.wait_for(lock, std::chrono::seconds(5), [&] { return returned; }))
        {
            std::fprintf(stderr, "Continue Rec froze the GUI thread\n");
            std::abort();
        }
    });
    cont->click();
    {
        std::lock_guard lock(mutex);
        returned = true;
    }
    done.notify_one();
    watchdog.join();

    EXPECT_TRUE(_ttd->IsRecording());
    EXPECT_FALSE(_machine->IsPaused());
    _machine->Pause();
}

TEST_F(TtdWidget_Branches_Test, ContinueRecIsOffUntilTheRecordingStops)
{
    TtdWidget w;
    w.setVisibleByUser(true);
    w.updateState(_machine);
    ASSERT_TRUE(_ttd->StartRecording());
    _machine->RunNFrames(10, true);
    Refresh(w);
    QPushButton* cont = Button(w, QStringLiteral("Continue Rec"));
    if (cont)   // the scrubber row may stay hidden while recording; if it shows, the button is off
        EXPECT_FALSE(cont->isEnabled()) << "nothing to continue while the recording runs";
    _ttd->StopRecording();
    Refresh(w);
    cont = Button(w, QStringLiteral("Continue Rec"));
    ASSERT_NE(cont, nullptr);
    EXPECT_TRUE(cont->isEnabled());
}
