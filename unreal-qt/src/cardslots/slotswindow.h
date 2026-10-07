/**
 * @file slotswindow.h
 * @brief SlotsWindow - Machine > Slots: simplified slot management with a list view.
 *
 * Layout:
 *   - Machine header (model name)
 *   - Built-in devices (collapsible, shows active/replaced state)
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
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;
class QToolButton;
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

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void buildUi();
    void refresh();
    void fillBuiltIns(const StateNode& report);
    void fillSlots(const StateNode& report);
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
    QToolButton* _builtInsToggle = nullptr;
    QTreeWidget* _builtInsTree = nullptr;
    QListWidget* _slotsList = nullptr;
    QPushButton* _addButton = nullptr;
    QPushButton* _removeButton = nullptr;
    QPushButton* _configButton = nullptr;
    QPushButton* _undoButton = nullptr;

    /// Test state - simulates the old form-based selection
    QString _testSlot;
    QString _testCard;
    QString _testOptions;
};
