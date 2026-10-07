// The Slots window (unreal-qt/src/cardslots/slotswindow.h) and its SlotChangeController (ZX-bus slots tdd.md §2.5,
// the Qt part): the plan preview names what a change removes before it is made; a plug replaces the incompatible cards
// (owner decision Q1) after a confirmation, restarts the machine (Q6), names the removed cards with an Undo, and the
// Undo puts the previous set back. The MIDI activity window reads the ZX-MultiSound's parts.
//
// The questions are answered by the test through the controller's `ask` hook (a QMessageBox in the application).
// Each restart builds a second machine (~20-40 ms).

#include <QApplication>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "cardslots/midiactivitywindow.h"
#include "cardslots/slotchangecontroller.h"
#include "cardslots/slotswindow.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/slots/slotplanner.h"

// Qt defines `slots` as a macro; the test names the slots namespace
#undef slots

namespace
{

std::vector<std::string> FittedOf(Emulator& emulator)
{
    std::vector<std::string> fitted;
    for (const SlotManager::Slot& slot : emulator.GetContext()->pSlotManager->Current().entries)
    {
        if (!slot.entry.disabled)
            fitted.push_back(slot.entry.slot + " = " + slot.entry.card);
    }
    return fitted;
}

class SlotsWindow_Test : public ::testing::Test
{
protected:
    void TearDown() override
    {
        _binding.unbind();
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    std::shared_ptr<Emulator> Create(const std::vector<std::pair<std::string, std::string>>& lines)
    {
        std::string error;
        auto emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel(
            "", "PENTAGON", LoggerLevel::LogError, &error, [lines](CONFIG& config) {
                SlotConfig slots;
                ParseSlotsSection(lines, slots);
                SlotManager::UseSlots(slots, config);
            });
        EXPECT_NE(emulator, nullptr) << error;
        return emulator;
    }

    /// The controller as MainWindow wires it, its questions answered from `_answers` (and recorded)
    std::unique_ptr<SlotChangeController> Controller()
    {
        SlotChangeController::Hooks hooks;
        hooks.beforeRelease = [this](Emulator&) { _binding.unbind(); };
        hooks.adopt = [this](std::shared_ptr<Emulator> emulator, bool) {
            _current = emulator;
            _binding.bind(emulator.get());
        };
        hooks.ask = [this](QWidget*, const QString& title, const QString& text, const QStringList& buttons) {
            _asked << title + ": " + text;
            const int answer = _answers.empty() ? -1 : _answers.front();
            if (!_answers.empty())
                _answers.erase(_answers.begin());
            return answer < buttons.size() ? answer : -1;
        };
        return std::make_unique<SlotChangeController>(std::move(hooks));
    }

    EmulatorBinding _binding;
    std::shared_ptr<Emulator> _current;
    std::vector<int> _answers;
    QStringList _asked;
};

} // namespace

/// The preview names the cards a plug would remove; Plug In asks, restarts with the card, names the removed cards;
/// Undo (from that question) restarts with the previous set
TEST_F(SlotsWindow_Test, PreviewPlugWarnAndUndo)
{
    _current = Create({{"ay-socket", "tsfm"}, {"zxbus.1", "gs"}});
    ASSERT_NE(_current, nullptr);
    _binding.bind(_current.get());
    auto controller = Controller();
    SlotsWindow window;
    window.setBinding(&_binding);
    window.setController(controller.get());
    window.show();   // reads the report and the catalog

    window.choose("zxbus.next", "multisound", "dip=ym,saa,gs,sd gsRam=2m");
    EXPECT_EQ(window.optionsText(), "dip=ym,saa,gs,sd gsRam=2m ctrlMask=pro");
    const QString preview = window.previewText();
    EXPECT_TRUE(preview.contains("plug multisound -> zxbus.2: allowed")) << preview.toStdString();
    EXPECT_TRUE(preview.contains("removes zxbus.1 = gs")) << preview.toStdString();
    EXPECT_TRUE(preview.contains("removes ay-socket = tsfm")) << preview.toStdString();

    const std::string firstId = _current->GetId();
    _answers = {0 /* Restart */, 1 /* Undo */, };
    window.plugChosen();
    ASSERT_GE(_asked.size(), 2);
    EXPECT_TRUE(_asked[0].contains("The machine restarts")) << _asked[0].toStdString();
    EXPECT_TRUE(_asked[0].contains("zxbus.1 = gs")) << _asked[0].toStdString();
    EXPECT_TRUE(_asked[1].startsWith("Cards removed: The change removed:")) << _asked[1].toStdString();
    // The Undo ran: the previous set on a third machine
    ASSERT_NE(_current, nullptr);
    EXPECT_NE(_current->GetId(), firstId);
    EXPECT_EQ(FittedOf(*_current), (std::vector<std::string>{"ay-socket = tsfm", "zxbus.1 = gs"}));
    EXPECT_FALSE(controller->CanUndo());

    // Cancelled at the question: nothing changes
    const std::string beforeCancel = _current->GetId();
    _answers = {1 /* Cancel */};
    window.choose("zxbus.next", "multisound");
    window.plugChosen();
    EXPECT_EQ(_current->GetId(), beforeCancel);
    EXPECT_TRUE(EmulatorManager::GetInstance()->HasEmulator(beforeCancel));
}

/// Set Options restarts with the card's new options; the window's Undo button puts the old ones back
TEST_F(SlotsWindow_Test, SetOptionsAndUndoButton)
{
    _current = Create({{"zxbus.1", "multisound"}});
    ASSERT_NE(_current, nullptr);
    _binding.bind(_current.get());
    auto controller = Controller();
    SlotsWindow window;
    window.setBinding(&_binding);
    window.setController(controller.get());
    window.show();

    window.choose("zxbus.1", "multisound", "dip=ym,gs ctrlMask=classic");
    EXPECT_TRUE(window.previewText().contains("set multisound -> zxbus.1: allowed")) << window.previewText().toStdString();
    _answers = {0 /* Restart */};
    window.setChosenOptions();
    ASSERT_NE(_current, nullptr);
    const SlotManager::Slot* slot = _current->GetContext()->pSlotManager->Current().FindSlot("zxbus.1");
    ASSERT_NE(slot, nullptr);
    static const slots::SlotPlanner planner;
    EXPECT_EQ(slots::FormatCardOptions(*planner.FindCard("multisound"), slot->entry.options),
              "dip=ym,gs gsRam=1m ctrlMask=classic");
    EXPECT_TRUE(controller->CanUndo());

    window.undoLast();
    slot = _current->GetContext()->pSlotManager->Current().FindSlot("zxbus.1");
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slots::FormatCardOptions(*planner.FindCard("multisound"), slot->entry.options),
              "dip=ym,saa,gs,sd gsRam=1m ctrlMask=pro");
}

/// The MIDI activity window lists the 16 parts of the card's synthesizer; without the card it says so
TEST_F(SlotsWindow_Test, MidiActivityListsTheParts)
{
    _current = Create({{"zxbus.1", "multisound"}});
    ASSERT_NE(_current, nullptr);
    _binding.bind(_current.get());
    MidiActivityWindow window;
    window.setBinding(&_binding);
    window.refresh();
    EXPECT_EQ(window.cellText(0, 0), "1");
    EXPECT_EQ(window.cellText(15, 0), "16");
    EXPECT_EQ(window.cellText(0, 1), "1") << "program 1";

    _binding.unbind();
    window.refresh();
    EXPECT_EQ(window.cellText(0, 0), "");
}

/// The Network window's card boxes (owner decision Q11): the ZX-bus network cards change through the controller - the
/// removes and plugs as one change, the restart confirmed, the removed card named with Undo
TEST_F(SlotsWindow_Test, NetworkCardsAreASlotChange)
{
    _current = Create({{"zxbus.1", "zx-wifi"}});
    ASSERT_NE(_current, nullptr);
    _binding.bind(_current.get());
    auto controller = Controller();
    const std::string firstId = _current->GetId();

    _answers = {0 /* Restart */, 0 /* OK */};
    ASSERT_TRUE(controller->ApplyNetworkCards(firstId, 0x01 /* ZXNETUSB */, nullptr));
    ASSERT_GE(_asked.size(), 2);
    EXPECT_TRUE(_asked[0].contains("The machine restarts")) << _asked[0].toStdString();
    EXPECT_TRUE(_asked[0].contains("zxbus.1 = zx-wifi")) << _asked[0].toStdString();
    EXPECT_TRUE(_asked[1].startsWith("Cards removed:")) << _asked[1].toStdString();
    ASSERT_NE(_current, nullptr);
    EXPECT_NE(_current->GetId(), firstId);
    EXPECT_EQ(controller->LastEmulatorId(), _current->GetId());
    EXPECT_EQ(FittedOf(*_current), (std::vector<std::string>{"zxbus.1 = zxnetusb"}));
    EXPECT_TRUE(controller->CanUndo());

    // The same cards again: nothing to change, no question, no restart
    _asked.clear();
    const std::string sameId = _current->GetId();
    EXPECT_TRUE(controller->ApplyNetworkCards(sameId, 0x01, nullptr));
    EXPECT_TRUE(_asked.isEmpty());
    EXPECT_EQ(_current->GetId(), sameId);
}

/// SL-8: on the Sprinter the window shows the board's own ISA slots (read-only, from [ISA]) with what the ZX-bus
/// adapter hosts, and the General Sound expansion slot on the adapter's ZX-bus
TEST_F(SlotsWindow_Test, SprinterIsaSlotsAndTheAdapterBus)
{
    std::string error;
    _current = EmulatorManager::GetInstance()->CreateEmulatorWithModel(
        "", "SPRINTER", LoggerLevel::LogError, &error, [](CONFIG& config) {
            SlotConfig slots;
            ParseSlotsSection({{"isa.1", "neogs"}, {"isa.1.adapter", "sprinter-isa-zxbus"}, {"isa.1.fit", "unrealistic"}},
                              slots);
            SlotManager::UseSlots(slots, config);
        });
    ASSERT_NE(_current, nullptr) << error;
    _binding.bind(_current.get());
    auto controller = Controller();
    SlotsWindow window;
    window.setBinding(&_binding);
    window.setController(controller.get());
    window.show();

    const QString text = window.slotsText();
    EXPECT_TRUE(text.contains("isa.1: NeoGS (on isa.1.zxbus)")) << text.toStdString();
    EXPECT_TRUE(text.contains("board isa.1 | ISA to ZX-bus adapter | hosts isa.1.zxbus: neogs")) << text.toStdString();
    EXPECT_TRUE(text.contains("board isa.2 | NE2000 Ethernet | RTL8019AS, #300, IRQ 3")) << text.toStdString();

    // A Pentagon has no board slots: the section stays hidden
    _binding.unbind();
    auto pentagon = Create({{"zxbus.1", "gs"}});
    ASSERT_NE(pentagon, nullptr);
    _binding.bind(pentagon.get());
    EXPECT_FALSE(window.slotsText().contains("board ")) << window.slotsText().toStdString();
}
