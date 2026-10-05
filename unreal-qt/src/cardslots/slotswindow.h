/**
 * @file slotswindow.h
 * @brief SlotsWindow - Machine > Slots: the machine's buses, slots and cards, a card catalog with how each card
 *        fits, the options of a card (the ZX-MultiSound's DIP switches, GS RAM and control mask), the plan of a change
 *        before it is made, and plug / remove / set options / Undo (ZX-bus slots SL-7, architecture.md §9).
 *
 * A thin adapter like every automation surface: it shows DeviceState::Slots and SlotControl::Catalog, previews
 * SlotManager::PlanChange, and applies through SlotChangeController (confirmation, restart, removed cards named with
 * Undo). Nothing slot-specific is decided here.
 */

#pragma once

#include <string>
#include <vector>

#include <QWidget>

#include "emulator/state/statenode.h"

class EmulatorBinding;
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class SlotChangeController;

class SlotsWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SlotsWindow(QWidget* parent = nullptr);

    void setBinding(EmulatorBinding* binding);
    void setController(SlotChangeController* controller);

    /// The plan preview's text now (tests)
    QString previewText() const;
    /// Selects a slot / card / options as a user would (tests and the tree)
    void choose(const QString& slot, const QString& card, const QString& options = QString());
    /// The options the editors hold, as "name=value ..." (tests)
    QString optionsText() const;

signals:
    /// Visibility changed via the window's own close box (keeps the menu in sync)
    void visibilityChanged(bool visible);

public:
    // Button actions (the buttons call them; tests too)
    void plugChosen();
    void removeChosen();
    void setChosenOptions();
    void undoLast();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildUi();
    void refresh();
    void fillTree(const StateNode& report);
    void fillCatalog(const StateNode& cards);
    void fillSlotChoices(const StateNode& report);
    void rebuildOptionEditors();
    void updatePreview();
    void onTreeSelection();
    std::string currentEmulatorId() const;

    struct OptionEditor
    {
        std::string name;
        bool set = false;
        QComboBox* combo = nullptr;                    ///< enum
        std::vector<std::pair<std::string, QCheckBox*>> boxes;   ///< set: value id, its box
    };

    EmulatorBinding* _binding = nullptr;
    SlotChangeController* _controller = nullptr;
    QTimer* _timer = nullptr;
    std::string _shownReport;       ///< the report the tree shows (JSON), to refresh only on a change
    std::string _shownModel;
    StateNode _catalog;             ///< the catalog's cards
    StateNode _report;              ///< the slot report
    bool _loading = false;

    QLabel* _machine = nullptr;
    QTreeWidget* _tree = nullptr;
    QComboBox* _slot = nullptr;
    QComboBox* _card = nullptr;
    QWidget* _optionsBox = nullptr;
    QVBoxLayout* _optionsLayout = nullptr;
    QLabel* _cardNote = nullptr;
    std::vector<OptionEditor> _editors;
    QPushButton* _plug = nullptr;
    QPushButton* _remove = nullptr;
    QPushButton* _setOptions = nullptr;
    QPushButton* _undo = nullptr;
    QPlainTextEdit* _preview = nullptr;
};
