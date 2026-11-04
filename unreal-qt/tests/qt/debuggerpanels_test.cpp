// The debugger's machine panels (debugger enhancements TODO #4, #6, #7): the CMOS clock grid (DeviceState::Rtc +
// RtcAccess::Write), the port watch (the decoder's registry, a changed latch lit) and the port-events search
// (TTDControl's verb: its refusal shown as is).

#include <QApplication>
#include <QLabel>
#include <QTableWidget>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "debugger/porteventsdialog.h"
#include "debugger/portwatchdialog.h"
#include "debugger/rtcdialog.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/rtc/rtcaccess.h"

class DebuggerPanels_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    void Create(const char* model)
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("debugger-panels-test", model,
                                                                            LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
    }
    QTableWidgetItem* RowWithPort(QTableWidget* table, const QString& port, int column)
    {
        for (int row = 0; row < table->rowCount(); row++)
            if (table->item(row, 0) && table->item(row, 0)->text() == port && table->item(row, column) &&
                !table->item(row, column)->text().isEmpty())
                return table->item(row, column);
        return nullptr;
    }
    std::shared_ptr<Emulator> _emulator;
};

TEST_F(DebuggerPanels_Test, RtcGridShowsCellsAndWritesThroughTheChip)
{
    ASSERT_NO_FATAL_FAILURE(Create("PROFI"));
    EmulatorContext* context = _emulator->GetContext();
    std::string error;
    ASSERT_TRUE(RtcAccess::Write(context, 0x20, {0x5A}, "test", error)) << error;

    RtcDialog dialog(_emulator);
    auto* grid = dialog.findChild<QTableWidget*>();
    ASSERT_NE(grid, nullptr);
    ASSERT_GE(grid->rowCount(), 4);
    ASSERT_NE(grid->item(2, 0), nullptr);
    EXPECT_EQ(grid->item(2, 0)->text(), "5A") << "cell 20 as the guest reads it";
    EXPECT_FALSE(grid->item(0, 0x0C)->flags() & Qt::ItemIsEditable) << "register C is read only";

    grid->item(2, 1)->setText("A5");  // the user's edit of cell 21
    std::vector<uint8_t> cell;
    ASSERT_TRUE(RtcAccess::Read(context, 0x21, 1, cell, error)) << error;
    EXPECT_EQ(cell[0], 0xA5);
    EXPECT_EQ(grid->item(2, 1)->text(), "A5");

    grid->item(2, 2)->setText("zz");  // not a byte: refused, the cell shows the chip again
    ASSERT_TRUE(RtcAccess::Read(context, 0x22, 1, cell, error)) << error;
    EXPECT_EQ(grid->item(2, 2)->text(), QStringLiteral("%1").arg(cell[0], 2, 16, QLatin1Char('0')).toUpper());
}

TEST_F(DebuggerPanels_Test, RtcSaysWhyWithoutAClock)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K"));
    RtcDialog dialog(_emulator);
    EXPECT_EQ(dialog.findChild<QTableWidget*>()->rowCount(), 0);
    bool reason = false;
    for (QLabel* label : dialog.findChildren<QLabel*>())
        reason = reason || !label->text().isEmpty();
    EXPECT_TRUE(reason) << "the reason is shown";
}

TEST_F(DebuggerPanels_Test, PortWatchShowsLatchesAndLightsAChange)
{
    ASSERT_NO_FATAL_FAILURE(Create("128k"));
    EmulatorState& state = _emulator->GetContext()->emulatorState;
    state.p7FFD = 0x10;
    PortWatchDialog dialog(_emulator);
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    QTableWidgetItem* value = RowWithPort(table, "7FFD", 4);
    ASSERT_NE(value, nullptr) << "the #7FFD latch row";
    EXPECT_EQ(value->text(), "10");
    EXPECT_EQ(value->background().style(), Qt::NoBrush) << "no change yet";

    state.p7FFD = 0x17;
    dialog.refresh();
    value = RowWithPort(table, "7FFD", 4);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->text(), "17");
    EXPECT_NE(value->background().style(), Qt::NoBrush) << "a change is lit";
    for (int i = 0; i < PortWatchDialog::kHighlightReads; i++)
        dialog.refresh();
    EXPECT_EQ(RowWithPort(table, "7FFD", 4)->background().style(), Qt::NoBrush) << "and goes out";
}

TEST_F(DebuggerPanels_Test, PortEventsShowsTheVerbsRefusal)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K"));
    PortEventsDialog dialog(_emulator);
    dialog.search();
    EXPECT_EQ(dialog.findChild<QTableWidget*>()->rowCount(), 0);
    bool message = false;
    for (QLabel* label : dialog.findChildren<QLabel*>())
        message = message || label->text().contains("record", Qt::CaseInsensitive) ||
                  label->text().contains("time travel", Qt::CaseInsensitive) ||
                  label->text().contains("session", Qt::CaseInsensitive);
    EXPECT_TRUE(message) << "no recording: the verb says so";
}
