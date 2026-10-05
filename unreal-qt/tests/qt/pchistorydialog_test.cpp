// The debugger's PC history dialog (PcHistoryDialog; debugger additions tdd §7): opening it starts the recording,
// Refresh lists the newest instructions with their window's page, Stop ends the recording.

#include <QApplication>
#include <QListWidget>
#include <QPushButton>
#include <gtest/gtest.h>

#include <memory>

#include "debugger/debugmanager.h"
#include "debugger/pchistory/pchistory.h"
#include "debugger/pchistorydialog.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

TEST(PcHistoryDialog_Test, OpenStartsRefreshListsStopEnds)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("pchistory-dialog-test", "48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    for (uint16_t a = 0x8000; a < 0x8004; ++a)
        context->pMemory->DirectWriteToZ80Memory(a, 0x00);  // NOPs
    context->pCore->GetZ80()->pc = 0x8000;

    PcHistoryDialog dialog(emulator.get());
    PcHistory* history = context->pDebugManager->GetPcHistory();
    EXPECT_TRUE(history->IsArmed()) << "opening the dialog starts the recording";
    emulator->RunSingleCPUCycle(true);
    emulator->RunSingleCPUCycle(true);
    dialog.refresh();
    auto* list = dialog.findChild<QListWidget*>();
    ASSERT_NE(list, nullptr);
    ASSERT_EQ(list->count(), 2);
    EXPECT_TRUE(list->item(0)->text().startsWith("8001")) << list->item(0)->text().toStdString();

    for (QPushButton* button : dialog.findChildren<QPushButton*>())
        if (button->text() == "Stop")
            button->click();
    EXPECT_FALSE(history->IsArmed());
    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}
