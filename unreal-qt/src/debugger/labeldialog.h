#pragma once

#include <QDialog>
#include "debugger/labels/labelmanager.h" // For Label struct

// Forward declarations
class QLineEdit;
class QComboBox;
class QTextEdit; // For multi-line comment
class QPushButton;
class QLabel;
class QCheckBox; // If we add 'active' state later
class LabelManager;

class LabelDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode { Add, Edit };

    // Constructor for Add mode
    explicit LabelDialog(LabelManager* labelManager, QWidget* parent = nullptr);
    // Constructor for Edit mode
    explicit LabelDialog(Label& labelToEdit, LabelManager* labelManager, QWidget* parent = nullptr);

    ~LabelDialog();

    Label getLabel() const; // Returns the configured label

private slots:
    void validateInput();
    void onAccept();

private:
    void setupUI();
    void loadLabelData();         // Load data from _label into UI elements
    void applyUIDataToLabel();    // Apply UI data back to _label member
    bool isAddressValid(const QString& addressStr, uint16_t& addressValue);
    bool isBankValid(const QString& bankStr, uint8_t& bankValue);
    bool isBankAddressValid(const QString& bankAddrStr, uint16_t& bankAddrValue);

    LabelManager* _labelManager = nullptr;
    Mode _mode;
    Label _label; // Stores the label data being edited or created

    // UI Elements
    QLineEdit* _nameEdit = nullptr;
    QLineEdit* _addressEdit = nullptr;      // Z80 Address (e.g., 0000-FFFF)
    QLineEdit* _bankEdit = nullptr;         // Bank number (0-254, or FF for no bank)
    QLineEdit* _bankAddressEdit = nullptr;  // Address within bank (e.g., 0000-3FFF)
    QComboBox* _typeCombo = nullptr;        // code, data, port, constant, local
    QLineEdit* _moduleEdit = nullptr;
    QTextEdit* _commentEdit = nullptr;
    QCheckBox* _activeCheck = nullptr;

    QPushButton* _okButton = nullptr;
    QPushButton* _cancelButton = nullptr;
    QLabel* _validationLabel = nullptr;
};
