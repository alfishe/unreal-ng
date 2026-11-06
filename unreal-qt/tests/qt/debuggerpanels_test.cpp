// The debugger's machine panels (debugger enhancements TODO #4, #6, #7): the CMOS clock grid (DeviceState::Rtc +
// RtcAccess::Write), the port watch (the decoder's registry, a changed latch lit) and the port-events search
// (TTDControl's verb: its refusal shown as is).

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
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

// The journals come from a recording: the dialog's top row starts it, the search then reads them
TEST_F(DebuggerPanels_Test, PortEventsStartsTheRecordingItNeeds)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K"));
    PortEventsDialog dialog(_emulator);
    QPushButton* record = nullptr;
    for (QPushButton* button : dialog.findChildren<QPushButton*>())
        if (button->text() == "Start recording")
            record = button;
    ASSERT_NE(record, nullptr);
    ASSERT_TRUE(record->isEnabled());

    record->click();
    EXPECT_EQ(record->text(), "Stop recording");
    dialog.search();
    bool searched = false;
    for (QLabel* label : dialog.findChildren<QLabel*>())
        searched = searched || label->text().contains("hit(s)");
    EXPECT_TRUE(searched) << "recording: the search reads the port journals";

    record->click();
    EXPECT_EQ(record->text(), "Start recording");
}

// The hits export to CSV: a header row, one row per hit, the core's words quoted where they hold a comma
TEST_F(DebuggerPanels_Test, PortEventsExportsTheHitsAsCsv)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K"));
    PortEventsDialog dialog(_emulator);
    QPushButton* record = nullptr;
    QPushButton* exportButton = nullptr;
    for (QPushButton* button : dialog.findChildren<QPushButton*>())
    {
        if (button->text() == "Start recording")
            record = button;
        if (button->text().startsWith("Export CSV"))
            exportButton = button;
    }
    ASSERT_NE(record, nullptr);
    ASSERT_NE(exportButton, nullptr);
    EXPECT_FALSE(exportButton->isEnabled()) << "nothing to export yet";

    for (QComboBox* combo : dialog.findChildren<QComboBox*>())
        if (combo->findText("out") >= 0)
            combo->setCurrentText("out");  // every OUT: the ROM sets the border as it starts
    record->click();
    _emulator->RunNFrames(3);
    record->click();
    dialog.search();
    QTableWidget* table = dialog.findChild<QTableWidget*>();
    ASSERT_GT(table->rowCount(), 0) << dialog.findChildren<QLabel*>().last()->text().toStdString();
    EXPECT_TRUE(exportButton->isEnabled());

    const QStringList lines = dialog.hitsCsv().split("\r\n", Qt::SkipEmptyParts);
    ASSERT_EQ(lines.size(), table->rowCount() + 1);
    EXPECT_EQ(lines[0], "event,direction,index,frame,t_in_frame,pc,port,value,ay_register,device,tags,what");
    EXPECT_TRUE(lines[1].contains(",0x" + table->item(0, 2)->text() + ",0x" + table->item(0, 3)->text() + ",")) << lines[1].toStdString();
    EXPECT_TRUE(lines[1].endsWith("\"" + table->item(0, 6)->text() + "\"")) << "the words hold commas: quoted";

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("hits.csv");
    ASSERT_TRUE(dialog.exportCsv(path));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(QString::fromUtf8(file.readAll()), dialog.hitsCsv());
    EXPECT_FALSE(dialog.exportCsv(dir.filePath("missing/hits.csv"))) << "an unwritable path is refused";
}
