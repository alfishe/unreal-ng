// The card's breakpoints in the Embedded debugger (gs-debugger TODO P5.3, gui-card-debugger A4 / A6 S5): the editor
// offers the event kinds grouped (the card's only with a card fitted) and puts a card-side event into the card CPU's
// breakpoints, a host-side one into the main CPU's; a port breakpoint can go to the card's CPU; the list shows both
// managers' breakpoints with a CPU column and acts on the right one; the stop line tells an event in plain words.

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "debugger/breakpointdialog.h"
#include "debugger/breakpointeditor.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/breakpointstopline.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/slots/slotcontrol.h"
#include "emulator/sound/chips/gs/gsdebughooks.h"

namespace
{
class BreakpointCardEvents_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        if (_emulator)
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetUUID());
    }
    /// A machine with the card the slot fits ({} none)
    void Create(const char* model, const std::vector<std::pair<std::string, std::string>>& cards)
    {
        std::function<void(CONFIG&)> slotOverride;
        std::string error;
        if (!cards.empty())
            ASSERT_TRUE(SlotControl::CreateOverride(cards, slotOverride, error)) << error;
        _emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel("breakpoint-card-events-test", model,
                                                                           LoggerLevel::LogError, &error, slotOverride);
        ASSERT_NE(_emulator, nullptr) << error;
    }
    BreakpointManager* Manager(DebugCpuId cpu) { return _emulator->GetDebugManager()->GetBreakpointsManager(cpu); }
    /// The combo box that holds `text` among its items
    template <typename W>
    static QComboBox* ComboWith(W& parent, const QString& text)
    {
        for (QComboBox* combo : parent.template findChildren<QComboBox*>())
            if (combo->findText(text) >= 0)
                return combo;
        return nullptr;
    }
    static QLineEdit* EditByTip(BreakpointEditor& editor, const QString& tipStart)
    {
        for (QLineEdit* edit : editor.findChildren<QLineEdit*>())
            if (edit->toolTip().startsWith(tipStart))
                return edit;
        return nullptr;
    }
    template <typename T, typename W>
    static T* ByText(W& parent, const QString& text)
    {
        for (T* widget : parent.template findChildren<T*>())
            if (widget->text() == text)
                return widget;
        return nullptr;
    }
    static QComboBox* EventCombo(BreakpointEditor& editor)
    {
        for (QComboBox* combo : editor.findChildren<QComboBox*>())
            if (combo->findData(static_cast<int>(GSCardEvent::IntAccept)) >= 0)
                return combo;
        return nullptr;
    }
    /// Adds an event breakpoint through the editor: the kind, an optional value
    void AddEvent(GSCardEvent kind, const QString& value)
    {
        BreakpointEditor editor(_emulator, BreakpointEditor::Add);
        QComboBox* type = ComboWith(editor, "Memory");
        ASSERT_NE(type, nullptr);
        type->setCurrentIndex(type->findData(static_cast<int>(BRK_EVENT)));
        QComboBox* events = EventCombo(editor);
        ASSERT_NE(events, nullptr);
        ASSERT_GE(events->findData(static_cast<int>(kind)), 0) << GSCardEventName(kind);
        events->setCurrentIndex(events->findData(static_cast<int>(kind)));
        EditByTip(editor, "Stop only on this byte")->setText(value);
        QPushButton* ok = ByText<QPushButton>(editor, "OK");
        ASSERT_TRUE(ok->isEnabled());
        ok->click();
        EXPECT_EQ(editor.result(), QDialog::Accepted);
    }
    /// The event breakpoint of `kind` in `manager`; null when none
    static BreakpointSnapshot EventOf(BreakpointManager* manager, GSCardEvent kind)
    {
        for (const auto& [id, bp] : manager->GetAllBreakpoints())
            if (bp->type == BRK_EVENT && bp->z80address == static_cast<uint16_t>(kind))
                return manager->GetBreakpointById(id);
        return {};
    }
    static QTreeWidgetItem* Row(QTreeWidget* tree, const QString& cpu, uint16_t id)
    {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
        {
            QTreeWidgetItem* item = tree->topLevelItem(i);
            if (item->text(1) == cpu && item->data(0, Qt::UserRole).toUInt() == id)
                return item;
        }
        return nullptr;
    }
    std::shared_ptr<Emulator> _emulator;
};
}  // namespace

/// Without a card: the machine's events only, no CPU choice
TEST_F(BreakpointCardEvents_Test, WithoutACardOnlyTheMachinesEvents)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K", {}));
    BreakpointEditor editor(_emulator, BreakpointEditor::Add);
    QComboBox* events = EventCombo(editor);
    ASSERT_NE(events, nullptr);
    EXPECT_GE(events->findText("Machine"), 0);
    EXPECT_LT(events->findText("Card"), 0);
    EXPECT_LT(events->findText("Protocol, host side"), 0);
    EXPECT_LT(events->findData(static_cast<int>(GSCardEvent::CardCommandRead)), 0);
    QComboBox* cpu = ComboWith(editor, "Main CPU");
    ASSERT_NE(cpu, nullptr);
    EXPECT_EQ(cpu->count(), 1);
    EXPECT_FALSE(cpu->isVisibleTo(&editor));
}

/// A card-side event lands in the card CPU's breakpoints, a host-side one in the main CPU's; the kinds are grouped
TEST_F(BreakpointCardEvents_Test, ACardEventGoesToTheCardAHostEventToMain)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    {
        BreakpointEditor editor(_emulator, BreakpointEditor::Add);
        QComboBox* events = EventCombo(editor);
        ASSERT_NE(events, nullptr);
        const int card = events->findText("Card");
        const int cardSide = events->findText("Protocol, card side");
        const int hostSide = events->findText("Protocol, host side");
        const int machine = events->findText("Machine");
        ASSERT_TRUE(card >= 0 && cardSide > card && hostSide > cardSide && machine > hostSide);
        EXPECT_EQ(events->itemData(cardSide).toInt(), -1) << "a group's title is no kind";
        EXPECT_EQ(events->findData(static_cast<int>(GSCardEvent::CardCommandRead)), cardSide + 1);
        EXPECT_EQ(events->itemText(cardSide + 1).trimmed(), "The card takes a command");
        EXPECT_LT(events->findData(static_cast<int>(GSCardEvent::DmaStart)), 0) << "NeoGS only";
    }
    ASSERT_NO_FATAL_FAILURE(AddEvent(GSCardEvent::CardCommandRead, "0x0F"));
    ASSERT_NO_FATAL_FAILURE(AddEvent(GSCardEvent::HostCommandWrite, ""));

    const BreakpointSnapshot card = EventOf(Manager(DebugCpuId::GS), GSCardEvent::CardCommandRead);
    ASSERT_TRUE(card);
    EXPECT_EQ(card->nrValue, 0x0F);
    EXPECT_EQ(card->nrMask, 0xFF);
    EXPECT_FALSE(EventOf(Manager(DebugCpuId::Main), GSCardEvent::CardCommandRead));
    const BreakpointSnapshot host = EventOf(Manager(DebugCpuId::Main), GSCardEvent::HostCommandWrite);
    ASSERT_TRUE(host);
    EXPECT_EQ(host->nrMask, 0);
    EXPECT_FALSE(EventOf(Manager(DebugCpuId::GS), GSCardEvent::HostCommandWrite));

    // An edit opens with the card's breakpoint as it is
    BreakpointEditor edit(_emulator, BreakpointEditor::Edit, card->breakpointID, nullptr, DebugCpuId::GS);
    QComboBox* events = EventCombo(edit);
    EXPECT_EQ(events->currentData().toInt(), static_cast<int>(GSCardEvent::CardCommandRead));
    EXPECT_EQ(EditByTip(edit, "Stop only on this byte")->text(), "#0F");
}

/// A port breakpoint on the card's CPU: the CPU choice picks the card
TEST_F(BreakpointCardEvents_Test, APortBreakpointOnTheCardsCpu)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    {
        BreakpointEditor editor(_emulator, BreakpointEditor::Add);
        QComboBox* type = ComboWith(editor, "Memory");
        type->setCurrentIndex(type->findData(static_cast<int>(BRK_IO)));
        QComboBox* cpu = ComboWith(editor, "Main CPU");
        ASSERT_NE(cpu, nullptr);
        ASSERT_EQ(cpu->count(), 2);
        cpu->setCurrentIndex(cpu->findData(static_cast<int>(DebugCpuId::GS)));
        QLineEdit* port = nullptr;
        for (QLineEdit* edit : editor.findChildren<QLineEdit*>())
            if (edit->placeholderText().startsWith("the card's ports"))
                port = edit;
        ASSERT_NE(port, nullptr);
        port->setText("0x01");
        ByText<QPushButton>(editor, "OK")->click();
        EXPECT_EQ(editor.result(), QDialog::Accepted);
    }
    bool onCard = false;
    for (const auto& [id, bp] : Manager(DebugCpuId::GS)->GetAllBreakpoints())
        onCard = onCard || (bp->type == BRK_IO && bp->z80address == 0x01);
    EXPECT_TRUE(onCard);
    for (const auto& [id, bp] : Manager(DebugCpuId::Main)->GetAllBreakpoints())
        EXPECT_FALSE(bp->type == BRK_IO && bp->z80address == 0x01 && BreakpointManager::IsUserBreakpoint(*bp));
}

/// The list: both CPUs' breakpoints, the CPU column, the event in words, and enable / disable on the right manager
TEST_F(BreakpointCardEvents_Test, TheListShowsBothCpusAndActsOnTheRightOne)
{
    ASSERT_NO_FATAL_FAILURE(Create("PENTAGON", {{"zxbus.1", "gs"}}));
    BreakpointSpec spec;
    spec.type = BRK_EVENT;
    spec.access = 0;
    spec.address = static_cast<uint16_t>(GSCardEvent::CardCommandRead);
    spec.nrValue = 0x0F;
    spec.nrMask = 0xFF;
    std::string error;
    const uint16_t cardId = Manager(DebugCpuId::GS)->AddBreakpoint(spec, error);
    ASSERT_NE(cardId, BRK_INVALID) << error;
    spec = BreakpointSpec{};
    spec.type = BRK_EVENT;
    spec.access = 0;
    spec.address = static_cast<uint16_t>(GSCardEvent::HostCommandWrite);
    spec.nrValue = 0x30;
    spec.nrMask = 0xFF;
    const uint16_t hostId = Manager(DebugCpuId::Main)->AddBreakpoint(spec, error);
    ASSERT_NE(hostId, BRK_INVALID) << error;

    BreakpointDialog dialog(_emulator);
    QTreeWidget* tree = dialog.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->headerItem()->text(1), "CPU");
    EXPECT_FALSE(tree->isColumnHidden(1));
    QTreeWidgetItem* cardRow = Row(tree, "gs", cardId);
    QTreeWidgetItem* hostRow = Row(tree, "main", hostId);
    ASSERT_TRUE(cardRow && hostRow);
    EXPECT_EQ(cardRow->text(2), "Event");
    EXPECT_EQ(cardRow->text(3), "the card takes command #0F");
    EXPECT_EQ(hostRow->text(3), "the host sends command #30");

    // Disable acts on the card's manager
    tree->setCurrentItem(cardRow);
    ByText<QPushButton>(dialog, "Disable")->click();
    EXPECT_FALSE(Manager(DebugCpuId::GS)->GetBreakpointById(cardId)->active);
    EXPECT_TRUE(Manager(DebugCpuId::Main)->GetBreakpointById(hostId)->active);

    // The CPU filter shows one CPU's
    QComboBox* cpuFilter = dialog.findChild<QComboBox*>("breakpointCpuFilter");
    ASSERT_NE(cpuFilter, nullptr);
    cpuFilter->setCurrentIndex(cpuFilter->findData(static_cast<int>(DebugCpuId::GS)));
    EXPECT_FALSE(Row(tree, "gs", cardId)->isHidden());
    EXPECT_TRUE(Row(tree, "main", hostId)->isHidden());
}

/// Without a card the list looks as it always did: no CPU column, no CPU filter
TEST_F(BreakpointCardEvents_Test, WithoutACardNoCpuColumn)
{
    ASSERT_NO_FATAL_FAILURE(Create("48K", {}));
    BreakpointDialog dialog(_emulator);
    QTreeWidget* tree = dialog.findChild<QTreeWidget*>();
    ASSERT_NE(tree, nullptr);
    EXPECT_TRUE(tree->isColumnHidden(1));
    EXPECT_FALSE(dialog.findChild<QComboBox*>("breakpointCpuFilter")->isVisibleTo(&dialog));
}

/// The stop line tells an event in plain words
TEST_F(BreakpointCardEvents_Test, TheStopLineTellsTheEvent)
{
    EXPECT_EQ(EventBreakpointWords(GSCardEvent::CardCommandRead, 0x0F, 0xFF, true), "the card took command #0F");
    EXPECT_EQ(EventBreakpointWords(GSCardEvent::HostCommandWrite, 0, 0), "the host sends a command");
    EXPECT_EQ(EventBreakpointWords(GSCardEvent::Int, 0x05, 0x0F), "the card accepts an interrupt (INT), value #05 & #0F");
    EXPECT_EQ(EventBreakpointGroup(GSCardEvent::Int), "Card");
    EXPECT_EQ(EventBreakpointGroup(GSCardEvent::CardReplyWrite), "Protocol, card side");
    EXPECT_EQ(EventBreakpointGroup(GSCardEvent::HostDataWrite), "Protocol, host side");
    EXPECT_EQ(EventBreakpointGroup(GSCardEvent::FdcCommand), "Machine");

    BreakpointDescriptor bp;
    bp.breakpointID = 3;
    bp.type = BRK_EVENT;
    bp.z80address = static_cast<uint16_t>(GSCardEvent::CardCommandRead);
    bp.nrValue = 0x0F;
    bp.nrMask = 0xFF;
    BreakpointHit hit;
    hit.id = 3;
    hit.address = bp.z80address;
    hit.kind = BreakpointHitKind::Event;
    hit.cpu = "gs";
    hit.cpuPc = 0x0C30;
    const QString line = BreakpointStopLine(bp, hit);
    EXPECT_TRUE(line.contains("gs CPU: the card took command #0F at $0C30")) << line.toStdString();
}
