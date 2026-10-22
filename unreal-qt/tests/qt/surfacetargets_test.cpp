// The main window as a view of TargetState (Stream Deck integration targeting.md): the binding's instance is the
// view's active instance, the view goes away with the window (its `view:` pins fall back), and the GUI part of T-2 -
// the status bar's surface icon with Pin here / Unpin, the controllers window's surfaces table.

#include <gtest/gtest.h>

#include <QComboBox>
#include <QMenu>
#include <QTableWidget>

#include <memory>

#include "controllers/surfacestable.h"
#include "controllers/surfacestatusicon.h"
#include "controllers/surfacetargets.h"
#include "emulator/actions/targetstate.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatormanager.h"

using actions::Pin;
using actions::TargetState;

class SurfaceTargets_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("surfaces", "48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
    }

    void TearDown() override
    {
        _binding.unbind();
        _emulator.reset();
        for (const std::string& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    std::shared_ptr<Emulator> _emulator;
    EmulatorBinding _binding;
};

TEST_F(SurfaceTargets_Test, TheBoundInstanceIsTheViewsActiveAndTheFollowedInstance)
{
    TargetState& state = TargetState::Instance();
    auto targets = std::make_unique<SurfaceTargets>(&_binding, QStringLiteral("Test window"));
    const std::string view = targets->viewId().toStdString();
    ASSERT_TRUE(state.View(view));
    EXPECT_EQ(state.View(view)->activeInstance, "");
    EXPECT_EQ(state.View(view)->owner, "unreal-qt");

    _binding.bind(_emulator);
    EXPECT_EQ(state.View(view)->activeInstance, _emulator->GetId());
    EXPECT_EQ(state.Followed(), _emulator->GetId()) << "the window was activated when it registered";
    _binding.unbind();
    EXPECT_EQ(state.View(view)->activeInstance, "");
    _binding.bind(_emulator);

    actions::SessionHandle deck = state.OpenSession({"Test deck", "companion", "", ""});
    EXPECT_EQ(targets->pinHere(QString::fromStdString(deck.Id())), QString());
    EXPECT_EQ(state.Session(deck.Id())->pin, Pin::OnView(view));
    ASSERT_EQ(targets->sessionsHere().size(), 1u);

    targets.reset();  // the window closes
    EXPECT_FALSE(state.View(view));
    EXPECT_EQ(state.Session(deck.Id())->pin, Pin::Follow()) << "the view pin fell back to following";
    EXPECT_FALSE(state.Session(deck.Id())->notice.empty());
}

TEST_F(SurfaceTargets_Test, TheStatusIconAndTheTableOfferPins)
{
    TargetState& state = TargetState::Instance();
    SurfaceTargets targets(&_binding, QStringLiteral("Test window"));
    _binding.bind(_emulator);
    SurfaceStatusIcon icon(&targets);
    SurfacesTable table(&targets);
    EXPECT_TRUE(icon.isHidden()) << "no surface: no icon";

    actions::SessionHandle deck = state.OpenSession({"Test deck", "companion", "", ""});
    icon.refresh();
    table.refresh();
    EXPECT_FALSE(icon.isHidden());
    // The deck follows the focus: this window is in front, so it drives this window's instance
    EXPECT_EQ(icon.isActive(), state.TargetFor(deck.Id())->view == targets.viewId().toStdString());

    // The menu of the deck's entry: [Pin here, Unpin], enabled by the pin it has when the menu opens
    auto entryMenu = [&icon](std::unique_ptr<QMenu>& holder) -> QMenu* {
        holder.reset(icon.buildMenu(nullptr));
        for (QAction* action : holder->actions())
            if (action->menu() != nullptr && action->text().contains(QStringLiteral("Test deck")))
                return action->menu();
        return nullptr;
    };
    std::unique_ptr<QMenu> menu;
    QMenu* entry = entryMenu(menu);
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->actions().size(), 2);
    EXPECT_TRUE(entry->actions()[0]->isEnabled());
    EXPECT_FALSE(entry->actions()[1]->isEnabled()) << "following: nothing to unpin";
    entry->actions()[0]->trigger();  // Pin here
    EXPECT_EQ(state.Session(deck.Id())->pin, Pin::OnView(targets.viewId().toStdString()));
    entry = entryMenu(menu);
    ASSERT_NE(entry, nullptr);
    EXPECT_FALSE(entry->actions()[0]->isEnabled()) << "pinned here already";
    entry->actions()[1]->trigger();  // Unpin
    EXPECT_EQ(state.Session(deck.Id())->pin, Pin::Follow());

    int row = -1;
    for (int i = 0; i < table.table()->rowCount(); i++)
        if (table.table()->item(i, 0) != nullptr && table.table()->item(i, 0)->text() == QStringLiteral("Test deck"))
            row = i;
    ASSERT_GE(row, 0);
    auto* combo = qobject_cast<QComboBox*>(table.table()->cellWidget(row, 3));
    ASSERT_NE(combo, nullptr);
    const int instance = combo->findData(QString::fromStdString(Pin::OnInstance(_emulator->GetId()).ToText()));
    ASSERT_GE(instance, 0);
    combo->setCurrentIndex(instance);
    emit combo->activated(instance);
    EXPECT_EQ(state.Session(deck.Id())->pin, Pin::OnInstance(_emulator->GetId()));
}
