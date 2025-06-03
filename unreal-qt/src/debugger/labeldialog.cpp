#include "labeldialog.h"
#include "debugger/labels/labelmanager.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QLabel>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QRegularExpressionValidator>

// Constructor for Add mode
LabelDialog::LabelDialog(LabelManager* labelManager, QWidget* parent)
    : QDialog(parent),
      _labelManager(labelManager),
      _mode(Mode::Add)
{
    setupUI();
    setWindowTitle(tr("Add Label"));
    _activeCheck->setChecked(true); // Default to active for new labels
    _nameEdit->setFocus();
    validateInput(); // Initial validation state for buttons
}

// Constructor for Edit mode
LabelDialog::LabelDialog(Label& labelToEdit, LabelManager* labelManager, QWidget* parent)
    : QDialog(parent),
      _labelManager(labelManager),
      _mode(Mode::Edit),
      _label(labelToEdit) // Copy the label to edit
{
    setupUI();
    setWindowTitle(tr("Edit Label"));
    loadLabelData();
    _nameEdit->setFocus();
    validateInput(); // Initial validation state for buttons
}

LabelDialog::~LabelDialog()
{
    // Qt handles child widget deletion
}

Label LabelDialog::getLabel() const
{
    return _label; // Assumes _label is updated by onAccept or applyUIDataToLabel
}

void LabelDialog::setupUI()
{
    _nameEdit = new QLineEdit(this);
    _addressEdit = new QLineEdit(this);
    _bankEdit = new QLineEdit(this);
    _bankAddressEdit = new QLineEdit(this);
    _typeCombo = new QComboBox(this);
    _moduleEdit = new QLineEdit(this);
    _commentEdit = new QTextEdit(this);
    _activeCheck = new QCheckBox(tr("Enabled"), this);
    _commentEdit->setAcceptRichText(false);
    _commentEdit->setFixedHeight(60); // Sensible default height for a few lines

    _validationLabel = new QLabel(this);
    _validationLabel->setStyleSheet("QLabel { color : red; }");

    // Populate type combo box (example types, can be expanded)
    _typeCombo->addItems({"code", "data", "text", "port", "constant", "local"});

    // Validators
    // Basic hex validator for addresses (e.g., up to 4 hex chars for 16-bit)
    QRegularExpression hexRegex4("^[0-9A-Fa-f]{1,4}$");
    QValidator* hexValidator4 = new QRegularExpressionValidator(hexRegex4, this);
    _addressEdit->setPlaceholderText(tr("0000-FFFF"));
    _addressEdit->setToolTip(tr("Z80 address (0000-FFFF hex)"));
    // _addressEdit->setValidator(hexValidator4); // Manual validation is more flexible for now

    _bankEdit->setPlaceholderText(tr("0-254, FF"));
    _bankEdit->setToolTip(tr("Bank number (0-254 decimal, or FF/255 for no bank)"));

    _bankAddressEdit->setPlaceholderText(tr("0000-3FFF"));
    _bankAddressEdit->setToolTip(tr("Address within bank (0000-3FFF hex if banked)"));
    // _bankAddressEdit->setValidator(hexValidator4); // Manual validation

    QFormLayout* formLayout = new QFormLayout;
    formLayout->addRow(tr("&Name:"), _nameEdit);
    formLayout->addRow(tr("&Address (Z80):"), _addressEdit);
    formLayout->addRow(tr("&Bank:"), _bankEdit);
    formLayout->addRow(tr("B&ank Offset:"), _bankAddressEdit);
    formLayout->addRow(tr("&Type:"), _typeCombo);
    formLayout->addRow(tr("&Module:"), _moduleEdit);
    formLayout->addRow(tr("C&omment:"), _commentEdit);
    formLayout->addRow(_activeCheck); // Add checkbox for active status

    _okButton = new QPushButton(tr("OK"));
    _cancelButton = new QPushButton(tr("Cancel"));

    connect(_okButton, &QPushButton::clicked, this, &LabelDialog::onAccept);
    connect(_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    // Connect input fields to validateInput slot
    connect(_nameEdit, &QLineEdit::textChanged, this, &LabelDialog::validateInput);
    connect(_addressEdit, &QLineEdit::textChanged, this, &LabelDialog::validateInput);
    connect(_bankEdit, &QLineEdit::textChanged, this, &LabelDialog::validateInput);
    connect(_bankAddressEdit, &QLineEdit::textChanged, this, &LabelDialog::validateInput);
    // Type, Module, Comment changes don't usually invalidate, but can be added if needed

    QDialogButtonBox* buttonBox = new QDialogButtonBox();
    buttonBox->addButton(_okButton, QDialogButtonBox::AcceptRole);
    buttonBox->addButton(_cancelButton, QDialogButtonBox::RejectRole);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(_validationLabel);
    mainLayout->addWidget(buttonBox);

    setLayout(mainLayout);
    setMinimumWidth(400);
}

void LabelDialog::loadLabelData()
{
    _nameEdit->setText(QString::fromStdString(_label.name));
    _addressEdit->setText(QString("%1").arg(_label.address, 4, 16, QChar('0')).toUpper());
    _bankEdit->setText(_label.bank == 0xFF ? "FF" : QString::number(_label.bank));
    _bankAddressEdit->setText(_label.bankAddress == 0xFFFF ? "" : QString("%1").arg(_label.bankAddress, 4, 16, QChar('0')).toUpper());
    _typeCombo->setCurrentText(QString::fromStdString(_label.type));
    _moduleEdit->setText(QString::fromStdString(_label.module));
    _commentEdit->setText(QString::fromStdString(_label.comment));
    _activeCheck->setChecked(_label.active);
}

void LabelDialog::applyUIDataToLabel()
{
    _label.name = _nameEdit->text().toStdString();
    _label.type = _typeCombo->currentText().toStdString();
    _label.module = _moduleEdit->text().toStdString();
    _label.comment = _commentEdit->toPlainText().toStdString();

    bool ok;
    // Address (Z80)
    _label.address = _addressEdit->text().toUShort(&ok, 16);

    // Bank
    QString bankStr = _bankEdit->text().trimmed().toUpper();
    if (bankStr.isEmpty() || bankStr == "FF" || bankStr == "255" || bankStr == "N/A") {
        _label.bank = 0xFF;
    } else {
        _label.bank = bankStr.toUShort(&ok); // Assuming decimal for bank unless specified otherwise
    }

    // Bank Address
    QString bankAddrStr = _bankAddressEdit->text().trimmed();
    if (bankAddrStr.isEmpty() || _label.bank == 0xFF) { // If no bank, bankAddress is irrelevant or 0xFFFF
        _label.bankAddress = 0xFFFF;
    } else {
        _label.bankAddress = bankAddrStr.toUShort(&ok, 16);
    }
    _label.active = _activeCheck->isChecked();
    // Physical address will be recalculated by LabelManager if necessary upon adding/updating.
}

void LabelDialog::validateInput()
{
    QString errorMsg;
    bool isValid = true;

    // Name validation
    if (_nameEdit->text().trimmed().isEmpty())
    {
        errorMsg += tr("Name cannot be empty.\n");
        isValid = false;
    }
    // TODO: Check if name already exists in Add mode, or if new name conflicts in Edit mode (if name is key)
    // This requires access to LabelManager: if (_labelManager && _mode == Mode::Add && _labelManager->GetLabelByName(...)) ...

    // Address validation
    uint16_t addrVal;
    if (!isAddressValid(_addressEdit->text(), addrVal))
    {
        errorMsg += tr("Invalid Z80 Address. Must be hex 0000-FFFF.\n");
        isValid = false;
    }

    // Bank and Bank Address validation
    uint8_t bankVal = 0xFF;
    bool bankSpecified = !_bankEdit->text().trimmed().isEmpty() && _bankEdit->text().trimmed().toUpper() != "FF" && _bankEdit->text().trimmed() != "255";
    
    if (bankSpecified && !isBankValid(_bankEdit->text(), bankVal))
    {
        errorMsg += tr("Invalid Bank. Must be 0-254, or FF/255 for none.\n");
        isValid = false;
    }

    uint16_t bankAddrVal;
    if (bankVal != 0xFF) // Bank is specified and valid (0-254)
    {
        if (!isBankAddressValid(_bankAddressEdit->text(), bankAddrVal))
        {
            errorMsg += tr("Invalid Bank Address. Must be hex 0000-3FFF for banked labels.\n");
            isValid = false;
        }
    }
    else if (!_bankAddressEdit->text().trimmed().isEmpty() && bankVal == 0xFF) // Bank not specified, but bank address is given
    {
         errorMsg += tr("Bank Address should be empty if no bank is specified.\n");
         isValid = false;
    }

    _validationLabel->setText(errorMsg);
    _okButton->setEnabled(isValid);
}

bool LabelDialog::isAddressValid(const QString& addressStr, uint16_t& addressValue)
{
    bool ok;
    addressValue = addressStr.toUShort(&ok, 16);
    return ok && addressStr.length() >=1 && addressStr.length() <=4; // Basic length check for 0-FFFF
}

bool LabelDialog::isBankValid(const QString& bankStr, uint8_t& bankValue)
{
    QString bStr = bankStr.trimmed().toUpper();
    if (bStr == "FF") { bankValue = 0xFF; return true; }
    bool ok;
    uint tempVal = bStr.toUInt(&ok);
    if (ok && tempVal <= 254) { 
        bankValue = static_cast<uint8_t>(tempVal);
        return true;
    }
    if (ok && tempVal == 255) { // Allow 255 as alias for FF
        bankValue = 0xFF;
        return true;
    }
    return false;
}

bool LabelDialog::isBankAddressValid(const QString& bankAddrStr, uint16_t& bankAddrValue)
{
    bool ok;
    bankAddrValue = bankAddrStr.toUShort(&ok, 16);
    // Bank address must be within 0x0000 - 0x3FFF (16KB page size)
    return ok && bankAddrValue <= 0x3FFF && bankAddrStr.length() >=1 && bankAddrStr.length() <=4;
}

void LabelDialog::onAccept()
{
    validateInput(); // Final validation check
    if (!_okButton->isEnabled()) 
    {
        QMessageBox::warning(this, tr("Invalid Input"), tr("Please correct the errors before proceeding."));
        return;
    }

    applyUIDataToLabel(); // Update _label member with UI data

    if (_mode == Mode::Add)
    {
        if (_labelManager && _labelManager->GetLabelByName(_label.name)) {
            QMessageBox::warning(this, tr("Add Label Failed"), tr("A label with the name '%1' already exists.").arg(QString::fromStdString(_label.name)));
            return;
        }
        // The LabelDialog calls _labelManager->AddLabel(editor.getLabel())
    }
    else // Mode::Edit
    {
        // If name is editable and can cause conflicts, check here.
        // For now, assuming name (if key) is not changed or handled by LabelManager update logic.
        // The LabelDialog calls _labelManager->UpdateLabel(...) or similar.
    }

    QDialog::accept(); // Close dialog and signal acceptance
}

