// The TTD panel's recording options (owner decision 2026-10-08): an Advanced row, hidden by
// default, holds the write journal (on by default in the panel) and the history limit (all by
// default); both and whether the row is open are remembered. A new instance gets the journal
// choice before anything records on it, Start Rec records with it, and "Build Journal" shows
// only for a session the journal does not cover

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdsession.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "widgets/ttdwidget.h"

class TtdWidget_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // The panel's settings in a folder of their own, never the user's
        ASSERT_TRUE(_settingsDir.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, _settingsDir.path());
    }

    void TearDown() override
    {
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    std::shared_ptr<Emulator> Machine(const std::string& name)
    {
        std::shared_ptr<Emulator> emulator =
            EmulatorManager::GetInstance()->CreateEmulatorWithModel(name, "48K", LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr);
        if (emulator)
        {
            emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
            emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        }
        return emulator;
    }

    static QCheckBox* Journal(TtdWidget& w) { return w.findChild<QCheckBox*>(); }
    static QComboBox* History(TtdWidget& w) { return w.findChild<QComboBox*>(); }
    static QToolButton* Advanced(TtdWidget& w)
    {
        for (QToolButton* b : w.findChildren<QToolButton*>())
            if (b->text() == QStringLiteral("Advanced"))
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
            if (l->text().contains(QStringLiteral("Memory")) || l->text().startsWith(QStringLiteral("TTD")))
                return l->text();
        return {};
    }

    /// Start Rec, a few frames, Stop Rec - through the panel's button
    static void RecordThroughThePanel(TtdWidget& w, Emulator& emulator)
    {
        QPushButton* rec = Button(w, QStringLiteral("Start Rec"));
        ASSERT_NE(rec, nullptr);
        rec->click();
        emulator.RunNFrames(5, true);
        rec->click();   // now "Stop Rec"
        w.updateState(nullptr);   // the next attach is a change: telemetry runs for it
    }

    QTemporaryDir _settingsDir;
};

TEST_F(TtdWidget_Test, TheOptionsAreHiddenWithDefaultsAndRemembered)
{
    {
        TtdWidget w;
        w.setVisibleByUser(true);
        ASSERT_NE(Journal(w), nullptr);
        ASSERT_NE(History(w), nullptr);
        ASSERT_NE(Advanced(w), nullptr);
        EXPECT_TRUE(Journal(w)->isChecked()) << "the panel records with the journal by default";
        EXPECT_EQ(History(w)->currentData().toULongLong(), 0u) << "and keeps all history";
        EXPECT_FALSE(Journal(w)->isVisibleTo(&w)) << "the Advanced row starts hidden";
        const int closed = w.desiredHeight();

        Advanced(w)->click();
        EXPECT_TRUE(Journal(w)->isVisibleTo(&w));
        EXPECT_GT(w.desiredHeight(), closed) << "the row takes its height";
        Journal(w)->setChecked(false);
        History(w)->setCurrentIndex(History(w)->findData(QVariant::fromValue<qulonglong>(qulonglong(2) << 30)));
    }
    TtdWidget again;
    EXPECT_TRUE(Journal(again)->isVisibleTo(&again)) << "the row stays open";
    EXPECT_FALSE(Journal(again)->isChecked()) << "the journal choice is remembered";
    EXPECT_EQ(History(again)->currentData().toULongLong(), qulonglong(2) << 30) << "and the history limit";
}

TEST_F(TtdWidget_Test, TheJournalChoiceReachesTheRecordingAndBuildJournalShowsOnlyWhereItIsMissing)
{
    TtdWidget w;
    w.setVisibleByUser(true);

    // On (the default): a new instance gets it before anything records; the session is covered
    std::shared_ptr<Emulator> covered = Machine("ttdwidget-journal");
    ASSERT_NE(covered, nullptr);
    w.updateState(covered);
    EXPECT_TRUE(covered->GetContext()->pTimeTravelController->GetEnableWriteJournal());
    ASSERT_NO_FATAL_FAILURE(RecordThroughThePanel(w, *covered));
    w.updateState(covered);
    EXPECT_TRUE(ttd::TTDSessionRef(covered->GetContext())->GetPublishedSessionInfo().writeJournalComplete);
    ASSERT_NE(Button(w, QStringLiteral("Build Journal")), nullptr);
    EXPECT_TRUE(Button(w, QStringLiteral("Build Journal"))->isHidden()) << "nothing to build";
    EXPECT_FALSE(Status(w).contains(QStringLiteral("No journal"))) << Status(w).toStdString();

    // Off: recorded without it, so Build Journal is offered, and the closed row says so
    Journal(w)->setChecked(false);
    std::shared_ptr<Emulator> bare = Machine("ttdwidget-bare");
    ASSERT_NE(bare, nullptr);
    w.updateState(bare);
    EXPECT_FALSE(bare->GetContext()->pTimeTravelController->GetEnableWriteJournal());
    ASSERT_NO_FATAL_FAILURE(RecordThroughThePanel(w, *bare));
    w.updateState(bare);
    EXPECT_FALSE(ttd::TTDSessionRef(bare->GetContext())->GetPublishedSessionInfo().writeJournalComplete);
    EXPECT_FALSE(Button(w, QStringLiteral("Build Journal"))->isHidden()) << "the session has no journal";
    EXPECT_TRUE(Status(w).contains(QStringLiteral("No journal"))) << Status(w).toStdString();
}
