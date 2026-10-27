// The label editor's Sets tab (SymbolSetsPanel; symbols S5): it lists the symbol sets behind the labels, the On box
// and the Priority cell change them through SymbolControl, and the labels follow.

#include <QApplication>
#include <QTableWidget>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>

#include "debugger/labels/labelmanager.h"
#include "debugger/symbolsetspanel.h"
#include "emulator/emulatorcontext.h"

TEST(SymbolSetsPanel_Test, TheTableSwitchesAndMovesSets)
{
    EmulatorContext context(LoggerLevel::LogError);
    LabelManager labels(&context);
    QTemporaryDir folder;
    ASSERT_TRUE(folder.isValid());
    const std::string first = folder.filePath("panel-first.sym").toStdString();
    const std::string second = folder.filePath("panel-second.sym").toStdString();
    std::ofstream(first, std::ios::binary) << "8000 SAME\n";
    std::ofstream(second, std::ios::binary) << "9000 SAME\n";
    ASSERT_TRUE(labels.LoadLabels(first));
    ASSERT_TRUE(labels.LoadLabels(second));

    SymbolSetsPanel panel(&labels);
    int changes = 0;
    QObject::connect(&panel, &SymbolSetsPanel::setsChanged, [&]() { changes++; });
    auto* table = panel.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    ASSERT_EQ(table->rowCount(), 2);
    EXPECT_EQ(table->item(1, 2)->text().toStdString(), "file:" + second);
    EXPECT_EQ(labels.GetLabelByName("SAME")->address, 0x9000);

    // The later file switched off: the earlier one's label shows
    table->item(1, 0)->setCheckState(Qt::Unchecked);
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(labels.GetLabelByName("SAME")->address, 0x8000);
    table = panel.findChild<QTableWidget*>();
    EXPECT_EQ(table->item(1, 0)->checkState(), Qt::Unchecked) << "the table shows the change";

    // Back on, then the earlier file moved above it
    table->item(1, 0)->setCheckState(Qt::Checked);
    table->item(0, 1)->setText("500");
    EXPECT_EQ(changes, 3);
    EXPECT_EQ(labels.GetLabelByName("SAME")->address, 0x8000);
    EXPECT_EQ(table->item(0, 1)->text(), "500");
}
