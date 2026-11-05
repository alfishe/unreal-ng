// The debugger's PC history pane (PcHistoryPane; debugger additions tdd §7): it records while shown, a refresh at a
// stop lists the newest instructions with their window's page and the instruction, a row picked names its address,
// and hiding it stops the recording.

#include <QApplication>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "debugger/debugmanager.h"
#include "debugger/pchistory/pchistory.h"
#include "debugger/pchistorypane.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

TEST(PcHistoryPane_Test, RecordsWhileShownListsInstructionsPicksAnAddress)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("pchistory-pane-test", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    for (uint16_t a = 0x8000; a < 0x8004; ++a)
        context->pMemory->DirectWriteToZ80Memory(a, 0x00);  // NOPs
    context->pCore->GetZ80()->pc = 0x8000;

    PcHistoryPane pane(emulator);
    PcHistory* history = context->pDebugManager->GetPcHistory();
    EXPECT_FALSE(history->IsArmed()) << "a hidden pane costs nothing";
    pane.setRecording(true);
    EXPECT_TRUE(history->IsArmed());
    emulator->RunSingleCPUCycle(true);
    emulator->RunSingleCPUCycle(true);
    pane.refresh();
    auto* list = pane.findChild<QTreeWidget*>();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->topLevelItemCount(), 2);
    EXPECT_EQ(list->topLevelItem(0)->text(0), "8001");
    EXPECT_EQ(list->topLevelItem(0)->text(1), "RAM2") << "48K: #8000 is RAM page 2";
    EXPECT_EQ(list->topLevelItem(0)->text(2), "nop");

    std::vector<uint16_t> picked;
    QObject::connect(&pane, &PcHistoryPane::addressPicked, [&](uint16_t address) { picked.push_back(address); });
    list->setCurrentItem(list->topLevelItem(1));
    ASSERT_EQ(picked.size(), 1u);
    EXPECT_EQ(picked[0], 0x8000);

    pane.setRecording(false);
    EXPECT_FALSE(history->IsArmed());
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}
