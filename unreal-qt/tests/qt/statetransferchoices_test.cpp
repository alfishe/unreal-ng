// Machine > Transfer State To (model what-if W0b): the menu lists the other open instances and the models a new machine
// can be, and an item hands MainWindow the request every surface builds; the report box shows the verb's own report.
// Real emulators, an offscreen menu.

#include <gtest/gtest.h>

#include <QAction>
#include <QMenu>

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/statetransferchoices.h"

class StateTransferChoices_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    static std::shared_ptr<Emulator> Create(const char* model, uint32_t ramKb)
    {
        std::shared_ptr<Emulator> emulator =
            EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM("transfer-menu", model, ramKb, LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr) << model;
        return emulator;
    }

    static QMenu* NewMachineMenu(QMenu& menu)
    {
        for (QAction* action : menu.actions())
            if (action->menu())
                return action->menu();
        return nullptr;
    }
};

TEST_F(StateTransferChoices_Test, TheMenuListsTheOtherInstancesAndTheModelsAndHandsOverTheRequest)
{
    auto source = Create("128k", 128);
    auto other = Create("PENTAGON", 512);
    ASSERT_TRUE(source && other);
    other->SetSymbolicId("w0b-other");

    std::vector<StateTransferRequest> picked;
    QMenu menu;
    StateTransferChoices::Fill(&menu, source->GetId(), [&](const StateTransferRequest& r) { picked.push_back(r); });

    QAction* instance = nullptr;
    for (QAction* action : menu.actions())
    {
        EXPECT_NE(action->data().toString().toStdString(), source->GetId()) << "never the source itself";
        if (action->data().toString().toStdString() == other->GetId())
            instance = action;
    }
    ASSERT_NE(instance, nullptr);
    EXPECT_TRUE(instance->text().startsWith("w0b-other - ")) << instance->text().toStdString();
    EXPECT_TRUE(instance->text().contains("512K")) << instance->text().toStdString();
    instance->trigger();
    ASSERT_EQ(picked.size(), 1u);
    EXPECT_EQ(picked[0].sourceId, source->GetId());
    EXPECT_EQ(picked[0].to, other->GetId());
    EXPECT_TRUE(picked[0].model.empty());

    QMenu* fresh = NewMachineMenu(menu);
    ASSERT_NE(fresh, nullptr);
    QAction* pentagon512 = nullptr;
    for (QAction* action : fresh->actions())
        if (action->data().toString() == "PENTAGON:512")
            pentagon512 = action;
    ASSERT_NE(pentagon512, nullptr) << "every creatable model, one item per RAM size";
    pentagon512->trigger();
    ASSERT_EQ(picked.size(), 2u);
    EXPECT_EQ(picked[1].model, "PENTAGON");
    EXPECT_EQ(picked[1].ramSizeKB, 512u);
    EXPECT_TRUE(picked[1].to.empty());

    // The report box text is the verb's report: here a check of the picked open instance
    StateTransferRequest check = picked[0];
    check.check = true;
    const QString text = StateTransferChoices::ReportText(StateTransferControl::Execute(check));
    EXPECT_TRUE(text.startsWith("Transfer check ")) << text.toStdString();
    EXPECT_TRUE(text.contains("[copied]")) << text.toStdString();
}

TEST_F(StateTransferChoices_Test, AloneTheMenuSaysThereIsNoOtherInstance)
{
    auto source = Create("48K", 48);
    ASSERT_TRUE(source);
    QMenu menu;
    StateTransferChoices::Fill(&menu, source->GetId(), [](const StateTransferRequest&) {});
    ASSERT_FALSE(menu.actions().isEmpty());
    EXPECT_FALSE(menu.actions().first()->isEnabled());
    EXPECT_TRUE(menu.actions().first()->text().contains("no other instance")) << menu.actions().first()->text().toStdString();
    EXPECT_NE(NewMachineMenu(menu), nullptr) << "a new machine is always offered";

    StateTransferChoices::Fill(&menu, "", [](const StateTransferRequest&) {});
    ASSERT_EQ(menu.actions().size(), 1);
    EXPECT_FALSE(menu.actions().first()->isEnabled()) << "no machine bound: nothing to transfer";
}
