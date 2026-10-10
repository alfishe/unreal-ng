// Automation sessions in the GUI (execution arbiter design §6a, X7c): while a session holds the machine every
// control of every window is locked except the exempt ones, the machine takes no input from the GUI; the user takes
// control back, the session's client is refused a re-take until the user allows it again

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTableWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include "common/uuid.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/remotecontrolguard.h"

class RemoteControlGuard_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("guard", "48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        auto* layout = new QVBoxLayout(&_window);
        _button = new QPushButton(QStringLiteral("Step"), &_window);
        _disabledByWindow = new QPushButton(QStringLiteral("Pause"), &_window);
        _disabledByWindow->setEnabled(false);
        _takeBack = new QPushButton(QStringLiteral("Take Back"), &_window);
        _scroll = new QScrollBar(&_window);
        layout->addWidget(_button);
        layout->addWidget(_disabledByWindow);
        layout->addWidget(_takeBack);
        layout->addWidget(_scroll);
        _action = new QAction(QStringLiteral("Reset"), &_window);
        _window.addAction(_action);
        _window.show();
        _guard.exempt(_takeBack);
    }

    void TearDown() override
    {
        _guard.setEmulator(nullptr);
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    EmulatorContext* Context() { return _emulator->GetContext(); }

    std::shared_ptr<Emulator> _emulator;
    QWidget _window;
    QPushButton* _button = nullptr;
    QPushButton* _disabledByWindow = nullptr;
    QPushButton* _takeBack = nullptr;
    QScrollBar* _scroll = nullptr;
    QAction* _action = nullptr;
    RemoteControlGuard _guard;
};

TEST_F(RemoteControlGuard_Test, ASessionLocksEveryControlButTheExemptOnesAndTheViews)
{
    _guard.setEmulator(_emulator);
    EXPECT_FALSE(_guard.active());
    EXPECT_TRUE(_button->isEnabled());

    int changed = 0;
    QObject::connect(&_guard, &RemoteControlGuard::sessionChanged, [&changed](bool) { ++changed; });
    const unreal::UUID owner = unreal::UUID::Generate();
    ASSERT_TRUE(Context()->TakeRunControl(owner, "mcp", nullptr, "agent", std::chrono::milliseconds(0)));
    _guard.refresh();

    EXPECT_TRUE(_guard.active());
    EXPECT_EQ(changed, 1);
    EXPECT_EQ(_guard.name(), QStringLiteral("agent via MCP"));
    EXPECT_TRUE(_guard.describe().startsWith(QStringLiteral("Remote control by agent via MCP")))
        << _guard.describe().toStdString();
    EXPECT_EQ(_guard.brief(), QStringLiteral("Remote control: agent via MCP \u00B7 local \u00B7"));
    EXPECT_FALSE(_button->isEnabled());
    EXPECT_FALSE(_action->isEnabled());
    EXPECT_TRUE(_takeBack->isEnabled()) << "exempt";
    EXPECT_TRUE(_scroll->isEnabled()) << "looking is not acting";
    EXPECT_TRUE(RemoteControlGuard::InputBlocked(_emulator.get()));

    // The window re-enables a control during the session: the next pass locks it again
    _button->setEnabled(true);
    _guard.refresh();
    EXPECT_FALSE(_button->isEnabled());

    Context()->ReleaseRunControl(owner);
    _guard.refresh();
    EXPECT_FALSE(_guard.active());
    EXPECT_TRUE(_button->isEnabled());
    EXPECT_TRUE(_action->isEnabled());
    EXPECT_FALSE(_disabledByWindow->isEnabled()) << "only what the guard locked is given back";
    EXPECT_FALSE(RemoteControlGuard::InputBlocked(_emulator.get()));
}

TEST_F(RemoteControlGuard_Test, TakingControlBackEndsTheSessionAndAllowAgainLiftsTheCooldown)
{
    _guard.setEmulator(_emulator);
    const unreal::UUID owner = unreal::UUID::Generate();
    ASSERT_TRUE(Context()->TakeRunControl(owner, "webapi", nullptr, "bot", std::chrono::milliseconds(0), "10.0.0.7:40001"));
    _guard.refresh();
    ASSERT_TRUE(_guard.active());

    QStringList takenBack;
    bool activeAtTheNews = true;
    QObject::connect(&_guard, &RemoteControlGuard::controlTakenBack, [&](const QString& who) {
        takenBack << who;
        activeAtTheNews = _guard.active();
    });
    _guard.takeBack();
    EXPECT_EQ(takenBack, (QStringList{QStringLiteral("bot via WebAPI from 10.0.0.7:40001")}));
    EXPECT_FALSE(activeAtTheNews) << "the news comes after the guard's state is current (the bar keeps the note)";
    EXPECT_FALSE(_guard.active());
    EXPECT_TRUE(_button->isEnabled());
    EXPECT_TRUE(Context()->IsRunControlRevoked(owner));

    // The same client is refused a re-take until the user allows it
    EXPECT_TRUE(_guard.hasCooldowns());
    std::string reason;
    EXPECT_FALSE(Context()->TakeRunControl(unreal::UUID::Generate(), "webapi", &reason, "bot", std::chrono::milliseconds(0)));
    _guard.allowAgain();
    EXPECT_FALSE(_guard.hasCooldowns());
    const unreal::UUID again = unreal::UUID::Generate();
    EXPECT_TRUE(Context()->TakeRunControl(again, "webapi", &reason, "bot", std::chrono::milliseconds(0))) << reason;
    Context()->ReleaseRunControl(again);
}

/// A widget that acts on keys and double clicks itself (the disassembler toggles breakpoints so)
class ActingWidget : public QWidget
{
public:
    using QWidget::QWidget;
    int keys = 0;
    int doubleClicks = 0;

protected:
    void keyPressEvent(QKeyEvent*) override { ++keys; }
    void mouseDoubleClickEvent(QMouseEvent*) override { ++doubleClicks; }
};

/// The lock cascades to every widget: editors read-only, tables not editable, acting input swallowed (looking keys
/// pass), a window shown during the session locked as it shows, the mutating places refuse - all given back after
TEST_F(RemoteControlGuard_Test, TheLockCascadesToEditorsViewsInputAndNewWindows)
{
    auto* edit = new QLineEdit(&_window);
    auto* table = new QTableWidget(2, 2, &_window);
    table->setEditTriggers(QAbstractItemView::DoubleClicked);
    auto* acting = new ActingWidget(&_window);
    _guard.setEmulator(_emulator);
    EXPECT_FALSE(RemoteControlGuard::Locked());

    const unreal::UUID owner = unreal::UUID::Generate();
    ASSERT_TRUE(Context()->TakeRunControl(owner, "mcp", nullptr, "agent", std::chrono::milliseconds(0)));
    _guard.refresh();
    EXPECT_TRUE(RemoteControlGuard::Locked());
    EXPECT_TRUE(RemoteControlGuard::Refuse(acting)) << "the places that change the machine refuse";
    EXPECT_TRUE(edit->isReadOnly());
    EXPECT_EQ(table->editTriggers(), QAbstractItemView::NoEditTriggers);

    QKeyEvent typing(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    QApplication::sendEvent(acting, &typing);
    EXPECT_EQ(acting->keys, 0) << "an acting key is swallowed";
    QKeyEvent paging(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
    QApplication::sendEvent(acting, &paging);
    EXPECT_EQ(acting->keys, 1) << "looking keys pass";
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(acting, &doubleClick);
    EXPECT_EQ(acting->doubleClicks, 0);

    // A dialog shown during the session is locked as it shows
    QWidget dialog;
    auto* apply = new QPushButton(QStringLiteral("Apply"), &dialog);
    dialog.show();
    EXPECT_FALSE(apply->isEnabled());

    Context()->ReleaseRunControl(owner);
    _guard.refresh();
    EXPECT_FALSE(RemoteControlGuard::Locked());
    EXPECT_FALSE(RemoteControlGuard::Refuse(acting));
    EXPECT_FALSE(edit->isReadOnly());
    EXPECT_EQ(table->editTriggers(), QAbstractItemView::DoubleClicked);
    EXPECT_TRUE(apply->isEnabled());
    QApplication::sendEvent(acting, &typing);
    EXPECT_EQ(acting->keys, 2);
}
