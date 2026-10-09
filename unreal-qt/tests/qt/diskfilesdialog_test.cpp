// The debugger's Disk files dialog (unreal-asm A7): the files of the disk in a drive with their source formats.

#include <QApplication>
#include <QPushButton>
#include <QTableWidget>
#include <gtest/gtest.h>

#include <memory>

#include "_helpers/testpathhelper.h"
#include "debugger/asm/asmcontrol.h"
#include "debugger/diskfilesdialog.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

TEST(DiskFilesDialog_Test, ListsTheFilesWithTheirFormats)
{
    std::shared_ptr<Emulator> emulator =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("diskfiles-dialog-test", "PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    ASSERT_TRUE(emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/trd/zx-format8.trd"), 0));
    const AsmReply written = AsmControl(emulator->GetContext())
                                 .Execute({"encode", {{"text", "START LD A,1\n RET\n"}, {"codec", "alasm"}, {"output", "disk:A/PROBE.H"}}});
    ASSERT_TRUE(written.Ok()) << written.message;

    DiskFilesDialog dialog(emulator.get());
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    int row = -1;
    for (int r = 0; r < table->rowCount(); ++r)
        if (table->item(r, 0)->text() == "PROBE")
            row = r;
    ASSERT_GE(row, 0);
    EXPECT_EQ(table->item(row, 1)->text(), "H");
    EXPECT_EQ(table->item(row, 4)->text(), "alasm");
    table->selectRow(row);
    QPushButton* open = nullptr;
    for (QPushButton* b : dialog.findChildren<QPushButton*>())
        if (b->text().contains("Open as Source"))
            open = b;
    ASSERT_NE(open, nullptr);
    EXPECT_TRUE(open->isEnabled());

    EmulatorManager::GetInstance()->RemoveEmulator(emulator->GetUUID());
}
