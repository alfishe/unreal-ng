// The per-hit record of a breakpoint that only counts (PLAN #122 V4) in the Qt breakpoint editor and list: the editor
// sets its size and which hits it keeps (only for a counting breakpoint, the core's defaults, an edit shows the
// breakpoint's own), the list expands a counting breakpoint's row to its hits and the dropped count, read through the
// manager's GetHitLog as the other planes do, and only while the row is open.

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "debugger/breakpointdialog.h"
#include "debugger/breakpointeditor.h"
#include "debugger/breakpoints/breakpointhitlog.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"

namespace
{
/// One pass: write #9000 (A = 5), then loop
const uint8_t kProgram[] = {
    0x3E, 0x05,        // 8000 LD A,5
    0x32, 0x00, 0x90,  // 8002 LD (#9000),A
    0x18, 0xF9,        // 8005 JR #8000
};
constexpr unsigned kLoopInstructions = 3;
}  // namespace

class BreakpointHits_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator =
            EmulatorManager::GetInstance()->CreateEmulatorWithModel("breakpoint-hits-test", "48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        EmulatorContext* context = _emulator->GetContext();
        for (size_t i = 0; i < sizeof(kProgram); i++)
            context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), kProgram[i]);
        Z80* z80 = context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->sp = 0x7F00;
        z80->iff1 = z80->iff2 = 0;
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    template <typename T>
    static T* ByText(QWidget& parent, const QString& text)
    {
        for (T* widget : parent.findChildren<T*>())
            if (widget->text() == text)
                return widget;
        return nullptr;
    }
    static QLineEdit* AddressEdit(BreakpointEditor& editor)
    {
        for (QLineEdit* edit : editor.findChildren<QLineEdit*>())
            if (edit->placeholderText().startsWith("Enter address"))
                return edit;
        return nullptr;
    }
    uint16_t Add(uint16_t address, uint8_t access, bool stop, uint32_t hitLog, BreakpointHitLogKeep keep)
    {
        BreakpointSpec spec;
        spec.type = BRK_MEMORY;
        spec.access = access;
        spec.address = address;
        spec.stop = stop;
        if (!stop)
        {
            spec.hitLog = hitLog;
            spec.hitLogKeep = keep;
        }
        std::string error;
        const uint16_t id = _emulator->GetBreakpointManager()->AddBreakpoint(spec, error);
        EXPECT_NE(id, BRK_INVALID) << error;
        return id;
    }
    static QTreeWidgetItem* Row(QTreeWidget* tree, uint16_t id)
    {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->data(0, Qt::UserRole).toUInt() == id)
                return tree->topLevelItem(i);
        return nullptr;
    }
    std::shared_ptr<Emulator> _emulator;
};

TEST_F(BreakpointHits_Test, TheEditorSetsTheRecordOnlyForACountingBreakpoint)
{
    BreakpointManager* manager = _emulator->GetBreakpointManager();
    uint16_t id = BRK_INVALID;
    {
        BreakpointEditor editor(_emulator, BreakpointEditor::Add);
        QSpinBox* size = editor.findChild<QSpinBox*>();
        QCheckBox* countOnly = ByText<QCheckBox>(editor, "Count only (never stops)");
        ASSERT_TRUE(size && countOnly);
        QComboBox* keep = nullptr;
        for (QComboBox* combo : editor.findChildren<QComboBox*>())
            if (combo->findText("the newest") >= 0)
                keep = combo;
        ASSERT_NE(keep, nullptr);
        // The core's defaults, off until the breakpoint only counts
        EXPECT_EQ(size->value(), static_cast<int>(BreakpointHitLog::kDefaultCapacity));
        EXPECT_EQ(size->maximum(), static_cast<int>(BreakpointHitLog::kMaxCapacity));
        EXPECT_EQ(keep->currentText(), "the first");
        EXPECT_FALSE(size->isEnabled());
        EXPECT_FALSE(keep->isEnabled());
        countOnly->setChecked(true);
        EXPECT_TRUE(size->isEnabled());
        EXPECT_TRUE(keep->isEnabled());
        size->setValue(0);
        EXPECT_FALSE(keep->isEnabled()) << "no record: nothing to keep";
        size->setValue(50);
        keep->setCurrentIndex(keep->findText("the newest"));

        AddressEdit(editor)->setText("0x8002");
        ByText<QPushButton>(editor, "OK")->click();
        EXPECT_EQ(editor.result(), QDialog::Accepted);
    }
    for (const auto& [bpId, bp] : manager->GetAllBreakpoints())
        if (bp->z80address == 0x8002)
            id = bpId;
    ASSERT_NE(id, BRK_INVALID);
    const BreakpointSnapshot bp = manager->GetBreakpointById(id);
    ASSERT_TRUE(bp);
    EXPECT_EQ(bp->tapSink, BRK_COUNT_TAP);
    EXPECT_EQ(bp->hitLogCapacity, 50u);
    EXPECT_EQ(bp->hitLogKeep, BreakpointHitLogKeep::Last);

    // An edit shows the breakpoint's own record, and keeps no record when asked for none
    {
        BreakpointEditor edit(_emulator, BreakpointEditor::Edit, id);
        QSpinBox* size = edit.findChild<QSpinBox*>();
        EXPECT_EQ(size->value(), 50);
        EXPECT_TRUE(size->isEnabled());
        bool newest = false;
        for (QComboBox* combo : edit.findChildren<QComboBox*>())
            newest = newest || combo->currentText() == "the newest";
        EXPECT_TRUE(newest);
        size->setValue(0);
        ByText<QPushButton>(edit, "OK")->click();
        EXPECT_EQ(edit.result(), QDialog::Accepted);
    }
    for (const auto& [bpId, edited] : manager->GetAllBreakpoints())
        if (edited->z80address == 0x8002)
        {
            EXPECT_EQ(edited->tapSink, BRK_COUNT_TAP);
            EXPECT_EQ(edited->hitLogCapacity, 0u);
        }

    // A breakpoint that stops shows the default, not editable
    const uint16_t stopping = Add(0xA000, BRK_MEM_EXECUTE, true, 0, BreakpointHitLogKeep::First);
    BreakpointEditor edit(_emulator, BreakpointEditor::Edit, stopping);
    QSpinBox* size = edit.findChild<QSpinBox*>();
    EXPECT_EQ(size->value(), static_cast<int>(BreakpointHitLog::kDefaultCapacity));
    EXPECT_FALSE(size->isEnabled());
}

TEST_F(BreakpointHits_Test, ACountingRowExpandsToItsHitsAndTheDropped)
{
    const uint16_t counting = Add(0x9000, BRK_MEM_WRITE, false, 5, BreakpointHitLogKeep::Last);
    const uint16_t stopping = Add(0xA000, BRK_MEM_EXECUTE, true, 0, BreakpointHitLogKeep::First);
    const uint16_t noRecord = Add(0x8000, BRK_MEM_EXECUTE, false, 0, BreakpointHitLogKeep::First);

    BreakpointDialog dialog(_emulator);
    QTreeWidget* tree = dialog.findChild<QTreeWidget*>();
    QTimer* poll = dialog.findChild<QTimer*>();
    ASSERT_TRUE(tree && poll);
    QTreeWidgetItem* row = Row(tree, counting);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->childIndicatorPolicy(), QTreeWidgetItem::ShowIndicator);
    EXPECT_NE(Row(tree, stopping)->childIndicatorPolicy(), QTreeWidgetItem::ShowIndicator) << "a stop keeps no record";
    EXPECT_NE(Row(tree, noRecord)->childIndicatorPolicy(), QTreeWidgetItem::ShowIndicator) << "count only, no record";
    EXPECT_FALSE(poll->isActive()) << "nothing open, nothing read";

    // Open before any hit: the record says so
    row->setExpanded(true);
    ASSERT_EQ(row->childCount(), 1);
    QTableWidget* hits = dialog.findChild<QTableWidget*>("breakpointHits");
    QLabel* summary = dialog.findChild<QLabel*>("breakpointHitSummary");
    ASSERT_TRUE(hits && summary);
    EXPECT_EQ(hits->rowCount(), 0);
    EXPECT_TRUE(summary->text().contains("no hits yet")) << summary->text().toStdString();
    EXPECT_TRUE(poll->isActive());

    // 12 passes: 12 writes, the newest 5 kept
    ASSERT_EQ(_emulator->RunNCPUCycles(12 * kLoopInstructions, false), 12 * kLoopInstructions);
    dialog.refreshHitLogs();
    ASSERT_EQ(hits->rowCount(), 5);
    EXPECT_EQ(hits->item(0, 0)->text(), "8");
    EXPECT_EQ(hits->item(4, 0)->text(), "12");
    EXPECT_EQ(hits->item(4, 3)->text(), "$8002") << "the PC of the store";
    EXPECT_EQ(hits->item(4, 4)->text(), "$9000");
    EXPECT_EQ(hits->item(4, 5)->text(), "$05");
    EXPECT_EQ(hits->item(4, 6)->text(), "write");
    EXPECT_TRUE(hits->isColumnHidden(7)) << "the main CPU: no card time";
    EXPECT_TRUE(summary->text().contains("12 recorded, 5 held, 7 overwritten")) << summary->text().toStdString();
    EXPECT_EQ(row->data(5, Qt::DisplayRole).toULongLong(), 12u) << "the row's count follows";
    const uint64_t t12 = hits->item(4, 2)->text().toULongLong();

    // A rebuild of the list keeps the row open
    QMetaObject::invokeMethod(&dialog, "refreshBreakpointList");
    row = Row(tree, counting);
    ASSERT_TRUE(row && row->isExpanded());
    hits = dialog.findChild<QTableWidget*>("breakpointHits");
    ASSERT_NE(hits, nullptr);
    EXPECT_EQ(hits->rowCount(), 5);
    EXPECT_EQ(hits->item(4, 2)->text().toULongLong(), t12);

    // Closed: the row goes and the record is not read any more
    row->setExpanded(false);
    EXPECT_EQ(row->childCount(), 0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);  // the view lets its row widget go later
    EXPECT_EQ(dialog.findChild<QTableWidget*>("breakpointHits"), nullptr);
    EXPECT_FALSE(poll->isActive());
}

TEST_F(BreakpointHits_Test, ARecordOfTheFirstShowsTheFirstAndCountsTheDropped)
{
    const uint16_t counting = Add(0x9000, BRK_MEM_WRITE, false, 3, BreakpointHitLogKeep::First);
    ASSERT_EQ(_emulator->RunNCPUCycles(10 * kLoopInstructions, false), 10 * kLoopInstructions);

    BreakpointDialog dialog(_emulator);
    QTreeWidgetItem* row = Row(dialog.findChild<QTreeWidget*>(), counting);
    ASSERT_NE(row, nullptr);
    row->setExpanded(true);
    QTableWidget* hits = dialog.findChild<QTableWidget*>("breakpointHits");
    QLabel* summary = dialog.findChild<QLabel*>("breakpointHitSummary");
    ASSERT_TRUE(hits && summary);
    ASSERT_EQ(hits->rowCount(), 3);
    EXPECT_EQ(hits->item(0, 0)->text(), "1");
    EXPECT_EQ(hits->item(2, 0)->text(), "3");
    EXPECT_TRUE(summary->text().contains("Records the first 3 hits: 10 recorded, 3 held, 7 dropped"))
        << summary->text().toStdString();

    // More hits move only the counts: the first three stay
    ASSERT_EQ(_emulator->RunNCPUCycles(5 * kLoopInstructions, false), 5 * kLoopInstructions);
    dialog.refreshHitLogs();
    EXPECT_EQ(hits->rowCount(), 3);
    EXPECT_EQ(hits->item(2, 0)->text(), "3");
    EXPECT_TRUE(summary->text().contains("15 recorded, 3 held, 12 dropped")) << summary->text().toStdString();
}
