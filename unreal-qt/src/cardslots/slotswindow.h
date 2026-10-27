/**
 * @file slotswindow.h
 * @brief SlotsWindow - Machine > Slots: simplified slot management with a list view.
 *
 * Layout:
 *   - Machine header (model name)
 *   - Built-in devices (collapsible, shows active/replaced state)
 *   - Board slots (the machine's own, the Sprinter's ISA slots from [ISA]; read-only, shown when the machine has them)
 *   - Expansion slots list with +/- buttons and "..." for configuration popup
 *   - Undo button
 */

#pragma once

#include <string>
#include <vector>

#include <QWidget>

#include "emulator/state/statenode.h"

class EmulatorBinding;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;
class QSplitter;
class QTreeWidget;
class SlotChangeController;
class SlotConfigDialog;

class SlotsWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SlotsWindow(QWidget* parent = nullptr);

    void setBinding(EmulatorBinding* binding);
    void setController(SlotChangeController* controller);

    /// Test helpers - simulate the old form-based API for backward compatibility with tests
    void choose(const QString& slot, const QString& card, const QString& options = QString());
    QString previewText() const;
    QString optionsText() const;
    void plugChosen();
    void removeChosen();
    void setChosenOptions();
    void undoLast();
    /// The expansion slots list and the board slots as lines (tests): "<slot list row>" per slot, then
    /// "board <slot> | <card> | <what it hosts or its details>" per board slot
    QString slotsText() const;

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildUi();
    void refresh();
    void fillInfoTree(const StateNode& report);
    void fillSlots(const StateNode& report);
    void fillMachineSlots(const StateNode& report);
    void onAddSlot();
    void onRemoveSlot();
    void onConfigureSlot(int row);
    void onSlotDialogAccepted(const std::string& slotId, const std::string& cardId, const std::string& options);
    std::string currentEmulatorId() const;
    std::string nextFreeSlot() const;
    QString defaultOptionsFor(const std::string& cardId) const;
    QString mergeOptionsWithDefaults(const std::string& cardId, const std::string& given) const;

    EmulatorBinding* _binding = nullptr;
    SlotChangeController* _controller = nullptr;
    QTimer* _timer = nullptr;
    std::string _shownReport;
    StateNode _report;
    StateNode _catalog;

    QLabel* _machineLabel = nullptr;
    QTreeWidget* _infoTree = nullptr;          ///< Read-only: buses, slots, cards, options, fit, state
    QTableWidget* _slotsTable = nullptr;
    QLabel* _machineSlotsLabel = nullptr;      ///< "Board Slots": the machine's own slots (the Sprinter's ISA slots)
    QTreeWidget* _machineSlotsTree = nullptr;
    QPushButton* _addButton = nullptr;
    QPushButton* _removeButton = nullptr;
    QPushButton* _undoButton = nullptr;

    /// Test state - simulates the old form-based selection
    QString _testSlot;
    QString _testCard;
    QString _testOptions;
};
