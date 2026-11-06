// The breakpoint editor's condition: checked while typed against the breakpoint's kind and access (the manager's
// compile), the reason and its place shown when refused, what it reads as when not; OK takes it to the breakpoint.

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <gtest/gtest.h>

#include <memory>

#include "debugger/breakpointeditor.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"

class BreakpointEditor_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("breakpoint-editor-test", "48K",
                                                                            LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    static QLineEdit* Edit(BreakpointEditor& editor, const QString& placeholderStart)
    {
        for (QLineEdit* edit : editor.findChildren<QLineEdit*>())
            if (edit->placeholderText().startsWith(placeholderStart))
                return edit;
        return nullptr;
    }
    template <typename T>
    static T* ByText(BreakpointEditor& editor, const QString& text)
    {
        for (T* widget : editor.findChildren<T*>())
            if (widget->text() == text)
                return widget;
        return nullptr;
    }
    /// The label under the condition field (the one that speaks about the condition)
    static QString Status(BreakpointEditor& editor)
    {
        for (QLabel* label : editor.findChildren<QLabel*>())
            if (label->textFormat() == Qt::RichText)
                return label->text();
        return {};
    }
    std::shared_ptr<Emulator> _emulator;
};

TEST_F(BreakpointEditor_Test, ConditionIsCheckedWhileTypedAndTheReasonShown)
{
    BreakpointEditor editor(_emulator, BreakpointEditor::Add);
    QLineEdit* address = Edit(editor, "Enter address");
    QLineEdit* condition = Edit(editor, "optional: stop only when");
    QPushButton* ok = ByText<QPushButton>(editor, "OK");
    QCheckBox* read = ByText<QCheckBox>(editor, "Read");
    ASSERT_NE(address, nullptr);
    ASSERT_NE(condition, nullptr);
    ASSERT_NE(ok, nullptr);
    ASSERT_NE(read, nullptr);
    // A fresh dialog reproaches nothing: OK just waits for the address
    EXPECT_FALSE(ok->isEnabled());
    for (QLabel* label : editor.findChildren<QLabel*>())
        EXPECT_FALSE(label->text().contains("Invalid")) << label->text().toStdString();
    address->setText("zz");
    EXPECT_NE(ByText<QLabel>(editor, "Invalid address format. Use 0xNNNN, $NNNN, #NNNN, or decimal."), nullptr);
    address->setText("0x8000");
    EXPECT_TRUE(ok->isEnabled());
    EXPECT_TRUE(Status(editor).contains("No condition"));

    condition->setText("A == 5 & 1");
    EXPECT_TRUE(ok->isEnabled());
    EXPECT_TRUE(Status(editor).contains("Stops when: <code>(A == 5) &amp; 1</code>"))
        << "the comparison binds first: shown" << Status(editor).toStdString();

    condition->setText("HLL == 1");
    EXPECT_FALSE(ok->isEnabled());
    EXPECT_TRUE(Status(editor).contains("did you mean HL?")) << Status(editor).toStdString();
    EXPECT_FALSE(Status(editor).contains("at column")) << "the place is marked, not counted";
    EXPECT_TRUE(Status(editor).contains("<u>HLL</u>")) << "the place: the word";

    // The access decides which operands there are: RD needs a read
    condition->setText("RD == #4000");
    EXPECT_FALSE(ok->isEnabled());
    EXPECT_TRUE(Status(editor).contains("exists only on a memory read")) << Status(editor).toStdString();
    read->setChecked(true);
    EXPECT_TRUE(ok->isEnabled()) << Status(editor).toStdString();

    condition->setText("A ==");
    EXPECT_TRUE(Status(editor).contains("a value is missing"));
    EXPECT_TRUE(Status(editor).contains("<u> </u>")) << "the place past the end";
}

TEST_F(BreakpointEditor_Test, OkTakesTheConditionAndEditShowsIt)
{
    BreakpointManager* manager = _emulator->GetBreakpointManager();
    ASSERT_NE(manager, nullptr);
    uint16_t id = BRK_INVALID;
    {
        BreakpointEditor editor(_emulator, BreakpointEditor::Add);
        Edit(editor, "Enter address")->setText("0x8000");
        Edit(editor, "optional: stop only when")->setText("A == 5");
        ByText<QPushButton>(editor, "OK")->click();
        EXPECT_EQ(editor.result(), QDialog::Accepted);
    }
    for (const auto& [bpId, bp] : manager->GetAllBreakpoints())
        if (bp->z80address == 0x8000)
            id = bpId;
    ASSERT_NE(id, BRK_INVALID);
    const BreakpointSnapshot bp = manager->GetBreakpointById(id);
    ASSERT_TRUE(bp && bp->condition);
    EXPECT_EQ(bp->condition->Source(), "A == 5");

    BreakpointEditor edit(_emulator, BreakpointEditor::Edit, id);
    EXPECT_EQ(Edit(edit, "optional: stop only when")->text(), "A == 5");
    EXPECT_TRUE(Status(edit).contains("Stops when"));
}

TEST_F(BreakpointEditor_Test, TheZxNextKindsAreOfferedOnlyOnAZxNext)
{
    const auto types = [](const std::shared_ptr<Emulator>& emulator) {
        BreakpointEditor editor(emulator, BreakpointEditor::Add);
        QStringList items;
        for (QComboBox* combo : editor.findChildren<QComboBox*>())
            if (combo->itemText(0) == "Memory")
                for (int i = 0; i < combo->count(); i++)
                    items << combo->itemText(i);
        return items;
    };
    EXPECT_EQ(types(_emulator), QStringList({"Memory", "Port", "Keyboard"}));

    std::shared_ptr<Emulator> next =
        EmulatorManager::GetInstance()->CreateEmulatorWithModel("breakpoint-editor-next", "NEXT", LoggerLevel::LogError);
    ASSERT_NE(next, nullptr);
    const QStringList nextTypes = types(next);
    EmulatorManager::GetInstance()->RemoveEmulator(next->GetUUID());
    EXPECT_EQ(nextTypes.size(), 7);
    EXPECT_TRUE(nextTypes.contains("Magic opcode (ZX Next)"));
}

TEST_F(BreakpointEditor_Test, SplitsTheColumnOffTheReason)
{
    int column = 0;
    EXPECT_EQ(BreakpointEditor::SplitConditionError("condition: unknown name 'QQ' at column 3", column),
              "Unknown name 'QQ'");
    EXPECT_EQ(column, 3);
    EXPECT_EQ(BreakpointEditor::SplitConditionError("condition: NR(x): this machine has no ZX Next registers", column),
              "NR(x): this machine has no ZX Next registers");
    EXPECT_EQ(column, 0);
}
