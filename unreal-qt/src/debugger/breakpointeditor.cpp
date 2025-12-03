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
    mainLayout->addLayout(addressLayout);
    
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
    
    // Add or update breakpoint
    if (_mode == Add)
    {
        uint16_t newId = BRK_INVALID;
        
        if (_descriptor.type == BRK_MEMORY)
        {
            newId = bpManager->AddCombinedMemoryBreakpoint(address, _descriptor.memoryType);
        }
        else if (_descriptor.type == BRK_IO)
        {
            newId = bpManager->AddCombinedPortBreakpoint(address, _descriptor.ioType);
        }
        else
        {
            // Keyboard breakpoints not implemented yet
            QMessageBox::warning(this, "Not Implemented", "Keyboard breakpoints are not yet implemented.");
            return;
        }
        
        if (newId != BRK_INVALID)
        {
            // Set group and note
            bpManager->SetBreakpointGroup(newId, _descriptor.group);
            
            // Set active state
            if (!_descriptor.active)
                bpManager->DeactivateBreakpoint(newId);
                
            // Set note
            if (!_descriptor.note.empty())
            {
                auto& breakpoints = bpManager->GetAllBreakpoints();
                if (breakpoints.find(newId) != breakpoints.end())
                {
                    breakpoints.at(newId)->note = _descriptor.note;
                }
            }
            
            accept();
        }
        else
        {
            QMessageBox::warning(this, "Error", "Failed to add breakpoint.");
        }
    }
    else // Edit mode
    {
        // Remove the old breakpoint and add a new one with the same ID
        // This is a workaround since we don't have direct edit capabilities
        
        // First, remove the old breakpoint
        bpManager->RemoveBreakpointByID(_breakpointId);
        
        // Then add a new one
        uint16_t newId = BRK_INVALID;
        
        if (_descriptor.type == BRK_MEMORY)
        {
            newId = bpManager->AddCombinedMemoryBreakpoint(address, _descriptor.memoryType);
        }
        else if (_descriptor.type == BRK_IO)
        {
            newId = bpManager->AddCombinedPortBreakpoint(address, _descriptor.ioType);
        }
        
        if (newId != BRK_INVALID)
        {
            // Set group and note
            bpManager->SetBreakpointGroup(newId, _descriptor.group);
            
            // Set active state
            if (!_descriptor.active)
                bpManager->DeactivateBreakpoint(newId);
                
            // Set note
            if (!_descriptor.note.empty())
            {
                auto& breakpoints = bpManager->GetAllBreakpoints();
                if (breakpoints.find(newId) != breakpoints.end())
                {
                    breakpoints.at(newId)->note = _descriptor.note;
                }
            }
            
            accept();
        }
        else
        {
            QMessageBox::warning(this, "Error", "Failed to update breakpoint.");
        }
    }
}

BreakpointDescriptor BreakpointEditor::getBreakpointDescriptor() const
{
    return _descriptor;
}
