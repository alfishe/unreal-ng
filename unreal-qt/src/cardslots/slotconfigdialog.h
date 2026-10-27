/**
 * @file slotconfigdialog.h
 * @brief SlotConfigDialog - popup for configuring a single slot.
 *
 * Layout:
 *   - Current card section (shows what's installed, if any)
 *   - Available cards tree (grouped by category)
 *   - Options section (shows card name + "current"/"new" indicator)
 *   - Buttons: Clear Slot, Cancel, Apply
 */

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <QDialog>

#include "emulator/state/statenode.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

class SlotConfigDialog : public QDialog
{
    Q_OBJECT

public:
    struct ConflictInfo
    {
        std::vector<std::string> disabledBuiltIns;
        std::vector<std::string> removedCards;
        bool needsRestart = false;
    };

    using ConflictProvider = std::function<ConflictInfo(const std::string& card)>;

    explicit SlotConfigDialog(QWidget* parent = nullptr);

    void setSlotId(const std::string& slotId);
    void setCatalog(const StateNode& catalog);
    void setCurrentCard(const std::string& cardId, const std::string& options);
    void setConflictProvider(ConflictProvider provider);

    std::string selectedCard() const;
    std::string selectedOptions() const;
    ConflictInfo conflicts() const;

signals:
    void accepted(const std::string& slotId, const std::string& cardId, const std::string& options);

private:
    void buildUi();
    void populateCardTree();
    void onCardSelected();
    void rebuildOptionEditors();
    void updatePlan();
    void onSave();
    void onClearSlot();
    QString formatOptions() const;

    struct OptionEditor
    {
        std::string name;
        bool isSet = false;
        QComboBox* combo = nullptr;
        std::vector<std::pair<std::string, QCheckBox*>> boxes;
    };

    std::string _slotId;
    StateNode _catalog;
    std::string _currentCardId;
    std::string _currentOptions;
    ConflictProvider _conflictProvider;
    ConflictInfo _lastConflicts;

    QLabel* _currentCardLabel = nullptr;
    QTreeWidget* _cardTree = nullptr;
    QGroupBox* _optionsGroup = nullptr;
    QLabel* _optionsCardLabel = nullptr;
    QLabel* _optionsStatusLabel = nullptr;
    QWidget* _optionsContainer = nullptr;
    QVBoxLayout* _optionsLayout = nullptr;
    std::vector<OptionEditor> _editors;
    QPushButton* _saveButton = nullptr;
    QPushButton* _cancelButton = nullptr;
    QPushButton* _clearButton = nullptr;

    std::map<std::string, QTreeWidgetItem*> _categoryItems;
};
