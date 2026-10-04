#include "breakpointeditor.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QInputDialog>

BreakpointEditor::BreakpointEditor(Emulator* emulator, Mode mode, QWidget* parent)
    : QDialog(parent), _emulator(emulator), _mode(mode), _breakpointId(BRK_INVALID)
{
    setWindowTitle(_mode == Add ? "Add Breakpoint" : "Edit Breakpoint");
    setupUI();
    
    // Initialize descriptor with defaults
    _descriptor.type = BRK_MEMORY;
    _descriptor.memoryType = BRK_MEM_EXECUTE;
    _descriptor.ioType = BRK_IO_IN | BRK_IO_OUT;
    _descriptor.active = true;
    _descriptor.group = "default";
    
    // Update UI with defaults
    _typeCombo->setCurrentIndex(0); // Memory
    _readCheck->setChecked(false);
    _writeCheck->setChecked(false);
    _executeCheck->setChecked(true);
    _inCheck->setChecked(true);
    _outCheck->setChecked(true);
    _activeCheck->setChecked(true);
    
    // Make sure address field is enabled for new breakpoints
    _addressEdit->setEnabled(true);
    
    // Set initial state based on type
    onTypeChanged(0);
}

BreakpointEditor::BreakpointEditor(Emulator* emulator, Mode mode, uint16_t breakpointId, QWidget* parent)
    : QDialog(parent), _emulator(emulator), _mode(mode), _breakpointId(breakpointId)
{
    setWindowTitle(_mode == Add ? "Add Breakpoint" : "Edit Breakpoint");
    setupUI();
    
    // Load existing breakpoint data
    loadBreakpointData(breakpointId);
}

BreakpointEditor::~BreakpointEditor()
{
}

void BreakpointEditor::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Type selection
    QHBoxLayout* typeLayout = new QHBoxLayout();
    QLabel* typeLabel = new QLabel("Type:");
    _typeCombo = new QComboBox();
    _typeCombo->addItem("Memory");
    _typeCombo->addItem("Port");
    _typeCombo->addItem("Keyboard");
    
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(_typeCombo);
    mainLayout->addLayout(typeLayout);
    
    // Address input
    QHBoxLayout* addressLayout = new QHBoxLayout();
    QLabel* addressLabel = new QLabel("Address:");
    _addressEdit = new QLineEdit();
    _addressEdit->setPlaceholderText("Enter address (e.g., 0x1234, $1234, #1234, 4660)");
    
    // Create validator for hex and decimal addresses
    QRegularExpression addressRegex("^(0x[0-9A-Fa-f]{1,4}|\\$[0-9A-Fa-f]{1,4}|#[0-9A-Fa-f]{1,4}|[0-9]{1,5})$");
    QValidator* addressValidator = new QRegularExpressionValidator(addressRegex, this);
    _addressEdit->setValidator(addressValidator);
    
    addressLayout->addWidget(addressLabel);
    addressLayout->addWidget(_addressEdit);
    QLabel* endLabel = new QLabel("to:");
    _endEdit = new QLineEdit();
    _endEdit->setPlaceholderText("optional range end");
    _endEdit->setValidator(new QRegularExpressionValidator(addressRegex, this));
    addressLayout->addWidget(endLabel);
    addressLayout->addWidget(_endEdit);
    mainLayout->addLayout(addressLayout);

    // Page: a physical breakpoint (that page, whatever slot shows it)
    QHBoxLayout* pageLayout = new QHBoxLayout();
    QLabel* pageLabel = new QLabel("Page:");
    _pageEdit = new QLineEdit();
    _pageEdit->setPlaceholderText("optional: ram5, rom1, cache0 - fires on that page in any slot");
    _slotOnlyCheck = new QCheckBox("This slot only");
    _slotOnlyCheck->setToolTip("Only through the slot of the address (#0000 / #4000 / #8000 / #C000)");
    pageLayout->addWidget(pageLabel);
    pageLayout->addWidget(_pageEdit);
    pageLayout->addWidget(_slotOnlyCheck);
    mainLayout->addLayout(pageLayout);
    
    // Access type group boxes
    _memoryAccessBox = new QGroupBox("Memory Access Type");
    QHBoxLayout* memoryAccessLayout = new QHBoxLayout();
    _readCheck = new QCheckBox("Read");
    _writeCheck = new QCheckBox("Write");
    _executeCheck = new QCheckBox("Execute");
    memoryAccessLayout->addWidget(_readCheck);
    memoryAccessLayout->addWidget(_writeCheck);
    memoryAccessLayout->addWidget(_executeCheck);
    _memoryAccessBox->setLayout(memoryAccessLayout);
    mainLayout->addWidget(_memoryAccessBox);
    
    _portAccessBox = new QGroupBox("Port Access Type");
    QHBoxLayout* portAccessLayout = new QHBoxLayout();
    _inCheck = new QCheckBox("In");
    _outCheck = new QCheckBox("Out");
    portAccessLayout->addWidget(_inCheck);
    portAccessLayout->addWidget(_outCheck);
    _maskEdit = new QLineEdit();
    _maskEdit->setPlaceholderText("mask, e.g. 0x00FF");
    _maskEdit->setToolTip("Matches every port where (port & mask) == (address & mask)");
    _maskEdit->setValidator(new QRegularExpressionValidator(addressRegex, this));
    portAccessLayout->addWidget(new QLabel("Mask:"));
    portAccessLayout->addWidget(_maskEdit);
    _portAccessBox->setLayout(portAccessLayout);
    mainLayout->addWidget(_portAccessBox);
    
    // Group selection
    QHBoxLayout* groupLayout = new QHBoxLayout();
    QLabel* groupLabel = new QLabel("Group:");
    _groupCombo = new QComboBox();
    _groupCombo->setEditable(true);
    populateGroupComboBox();
    
    groupLayout->addWidget(groupLabel);
    groupLayout->addWidget(_groupCombo);
    mainLayout->addLayout(groupLayout);

    // Hit policy
    QHBoxLayout* hitsLayout = new QHBoxLayout();
    QLabel* hitsLabel = new QLabel("Hits:");
    _hitsEdit = new QLineEdit();
    _hitsEdit->setPlaceholderText("every hit; or 5 (the 5th only), >=5 (from the 5th on), %5 (every 5th)");
    hitsLayout->addWidget(hitsLabel);
    hitsLayout->addWidget(_hitsEdit);
    mainLayout->addLayout(hitsLayout);
    
    // Note input
    QHBoxLayout* noteLayout = new QHBoxLayout();
    QLabel* noteLabel = new QLabel("Note:");
    _noteEdit = new QLineEdit();
    _noteEdit->setPlaceholderText("Optional note for this breakpoint");
    
    noteLayout->addWidget(noteLabel);
    noteLayout->addWidget(_noteEdit);
    mainLayout->addLayout(noteLayout);
    
    // Active checkbox
    _activeCheck = new QCheckBox("Active");
    mainLayout->addWidget(_activeCheck);
    
    // Validation label
    _validationLabel = new QLabel();
    _validationLabel->setStyleSheet("color: red;");
    mainLayout->addWidget(_validationLabel);
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    _okButton = new QPushButton("OK");
    _cancelButton = new QPushButton("Cancel");
    
    _okButton->setDefault(true);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(_okButton);
    buttonLayout->addWidget(_cancelButton);
    mainLayout->addLayout(buttonLayout);
    
    // Connect signals
    connect(_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &BreakpointEditor::onTypeChanged);
    connect(_addressEdit, &QLineEdit::textChanged, 
            this, &BreakpointEditor::onAddressChanged);
    connect(_okButton, &QPushButton::clicked, 
            this, &BreakpointEditor::onAccept);
    connect(_cancelButton, &QPushButton::clicked, 
            this, &QDialog::reject);
    
    // Set initial size
    resize(400, 350);
}

void BreakpointEditor::populateGroupComboBox()
{
    _groupCombo->clear();
    _groupCombo->addItem("default");
    
    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    if (bpManager)
    {
        std::vector<std::string> groups = bpManager->GetBreakpointGroups();
        for (const auto& group : groups)
        {
            if (group != "default") // Already added
                _groupCombo->addItem(QString::fromStdString(group));
        }
    }
    
    _groupCombo->addItem("Create New Group...");
}

void BreakpointEditor::loadBreakpointData(uint16_t breakpointId)
{
    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    if (!bpManager)
        return;
        
    const BreakpointMapByID& breakpoints = bpManager->GetAllBreakpoints();
    auto it = breakpoints.find(breakpointId);
    if (it == breakpoints.end())
        return;
        
    const BreakpointDescriptor* bp = it->second;
    _descriptor = *bp;
    
    // Set type
    _typeCombo->setCurrentIndex(static_cast<int>(bp->type));
    
    // Set address
    _addressEdit->setText(QString("0x%1").arg(bp->z80address, 4, 16, QChar('0')).toUpper());
    _endEdit->setText(bp->isRange ? QString("0x%1").arg(bp->z80addressEnd, 4, 16, QChar('0')).toUpper() : QString());
    _pageEdit->setText(QString::fromStdString(BreakpointManager::PageSpecName(*bp)));
    _slotOnlyCheck->setChecked(bp->slotOnly);
    _maskEdit->setText(bp->portMask != 0xFFFF ? QString("0x%1").arg(bp->portMask, 4, 16, QChar('0')).toUpper() : QString());
    _hitsEdit->setText(QString::fromStdString(BreakpointManager::HitSpecName(*bp)));
    
    // Set access type
    if (bp->type == BRK_MEMORY)
    {
        _readCheck->setChecked(bp->memoryType & BRK_MEM_READ);
        _writeCheck->setChecked(bp->memoryType & BRK_MEM_WRITE);
        _executeCheck->setChecked(bp->memoryType & BRK_MEM_EXECUTE);
    }
    else if (bp->type == BRK_IO)
    {
        _inCheck->setChecked(bp->ioType & BRK_IO_IN);
        _outCheck->setChecked(bp->ioType & BRK_IO_OUT);
    }
    
    // Set group
    int groupIndex = _groupCombo->findText(QString::fromStdString(bp->group));
    if (groupIndex != -1)
        _groupCombo->setCurrentIndex(groupIndex);
    else
        _groupCombo->setEditText(QString::fromStdString(bp->group));
    
    // Set note
    _noteEdit->setText(QString::fromStdString(bp->note));
    
    // Set active state
    _activeCheck->setChecked(bp->active);
    
    // Update UI based on type
    onTypeChanged(_typeCombo->currentIndex());
}

void BreakpointEditor::onTypeChanged(int index)
{
    // Update UI based on selected type
    BreakpointTypeEnum type = static_cast<BreakpointTypeEnum>(index);
    
    // Show/hide appropriate access type controls
    if (type == BRK_MEMORY)
    {
        _memoryAccessBox->setVisible(true);
        _portAccessBox->setVisible(false);
    }
    else if (type == BRK_IO)
    {
        _memoryAccessBox->setVisible(false);
        _portAccessBox->setVisible(true);
    }
    else // BRK_KEYBOARD
    {
        _memoryAccessBox->setVisible(false);
        _portAccessBox->setVisible(false);
    }
    
    validateInput();
}

void BreakpointEditor::validateInput()
{
    bool isValid = true;
    QString errorMessage;
    
    // Validate address
    uint16_t address;
    if (!validateAddress(_addressEdit->text(), address))
    {
        isValid = false;
        errorMessage = "Invalid address format. Use 0xNNNN, $NNNN, #NNNN, or decimal.";
    }
    
    // Validate access type
    BreakpointTypeEnum type = static_cast<BreakpointTypeEnum>(_typeCombo->currentIndex());
    if (type == BRK_MEMORY && !(_readCheck->isChecked() || _writeCheck->isChecked() || _executeCheck->isChecked()))
    {
        isValid = false;
        errorMessage = "At least one memory access type must be selected.";
    }
    else if (type == BRK_IO && !(_inCheck->isChecked() || _outCheck->isChecked()))
    {
        isValid = false;
        errorMessage = "At least one port access type must be selected.";
    }
    
    // Update validation label and OK button state
    _validationLabel->setText(errorMessage);
    _okButton->setEnabled(isValid);
}

void BreakpointEditor::onAddressChanged(const QString& text)
{
    validateInput();
}

bool BreakpointEditor::validateAddress(const QString& text, uint16_t& address)
{
    if (text.isEmpty())
        return false;
        
    bool ok = false;
    
    if (text.startsWith("0x", Qt::CaseInsensitive))
    {
        // Hex format with 0x prefix
        address = text.mid(2).toUInt(&ok, 16);
    }
    else if (text.startsWith("$"))
    {
        // Hex format with $ prefix
        address = text.mid(1).toUInt(&ok, 16);
    }
    else if (text.startsWith("#"))
    {
        // Hex format with # prefix
        address = text.mid(1).toUInt(&ok, 16);
    }
    else
    {
        // Decimal format
        address = text.toUInt(&ok, 10);
    }
    
    return ok && address <= 0xFFFF;
}

void BreakpointEditor::onAccept()
{
    // Validate input
    validateInput();
    if (!_okButton->isEnabled())
        return;
        
    // Parse address
    uint16_t address;
    if (!validateAddress(_addressEdit->text(), address))
        return;
        
    // Get breakpoint manager
    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    if (!bpManager)
        return;
        
    // Handle "Create New Group..." option
    QString groupName = _groupCombo->currentText();
    if (groupName == "Create New Group...")
    {
        bool ok;
        groupName = QInputDialog::getText(this, "New Group", 
                                         "Enter new group name:", 
                                         QLineEdit::Normal, 
                                         "", &ok);
        if (!ok || groupName.isEmpty())
        {
            groupName = "default";
        }
    }
    
    // Update descriptor
    _descriptor.type = static_cast<BreakpointTypeEnum>(_typeCombo->currentIndex());
    _descriptor.z80address = address;
    _descriptor.active = _activeCheck->isChecked();
    _descriptor.note = _noteEdit->text().toStdString();
    _descriptor.group = groupName.toStdString();
    
    // Set access types
    if (_descriptor.type == BRK_MEMORY)
    {
        _descriptor.memoryType = 0;
        if (_readCheck->isChecked()) _descriptor.memoryType |= BRK_MEM_READ;
        if (_writeCheck->isChecked()) _descriptor.memoryType |= BRK_MEM_WRITE;
        if (_executeCheck->isChecked()) _descriptor.memoryType |= BRK_MEM_EXECUTE;
    }
    else if (_descriptor.type == BRK_IO)
    {
        _descriptor.ioType = 0;
        if (_inCheck->isChecked()) _descriptor.ioType |= BRK_IO_IN;
        if (_outCheck->isChecked()) _descriptor.ioType |= BRK_IO_OUT;
    }
    
    if (_descriptor.type == BRK_KEYBOARD)
    {
        QMessageBox::warning(this, "Not Implemented", "Keyboard breakpoints are not yet implemented.");
        return;
    }

    // Everything the manager validates (range, page, mask, hit policy) goes through one spec
    BreakpointSpec spec;
    spec.type = _descriptor.type;
    spec.access = _descriptor.type == BRK_MEMORY ? _descriptor.memoryType : _descriptor.ioType;
    spec.address = address;
    spec.note = _descriptor.note;
    spec.group = _descriptor.group;
    std::string error;
    uint16_t value = 0;
    if (!_endEdit->text().trimmed().isEmpty())
    {
        if (!validateAddress(_endEdit->text(), value))
        {
            QMessageBox::warning(this, "Error", "The range end is not an address.");
            return;
        }
        spec.hasEnd = true;
        spec.addressEnd = value;
    }
    if (!_pageEdit->text().trimmed().isEmpty() && _descriptor.type == BRK_MEMORY)
    {
        if (!BreakpointManager::ParsePageSpec(_pageEdit->text().trimmed().toStdString(), spec.page, spec.pageType, error))
        {
            QMessageBox::warning(this, "Error", QString::fromStdString(error));
            return;
        }
        spec.hasPage = true;
        spec.slotOnly = _slotOnlyCheck->isChecked();
    }
    if (!_maskEdit->text().trimmed().isEmpty() && _descriptor.type == BRK_IO)
    {
        if (!validateAddress(_maskEdit->text(), value))
        {
            QMessageBox::warning(this, "Error", "The mask is not a 16-bit value.");
            return;
        }
        spec.portMask = value;
    }
    if (!BreakpointManager::ParseHitSpec(_hitsEdit->text().trimmed().toStdString(), spec.hitMode, spec.hitTarget, error))
    {
        QMessageBox::warning(this, "Error", QString::fromStdString(error));
        return;
    }

    // An edit replaces the breakpoint (a new id); the manager refuses what does not fit this machine
    if (_mode != Add)
        bpManager->RemoveBreakpointByID(_breakpointId);
    const uint16_t newId = bpManager->AddBreakpoint(spec, error);
    if (newId == BRK_INVALID)
    {
        QMessageBox::warning(this, "Error", QString::fromStdString(error));
        return;
    }
    if (!_descriptor.active)
        bpManager->DeactivateBreakpoint(newId);
    accept();
}

BreakpointDescriptor BreakpointEditor::getBreakpointDescriptor() const
{
    return _descriptor;
}
