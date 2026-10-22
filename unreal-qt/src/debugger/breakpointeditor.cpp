#include "breakpointeditor.h"
#include "emulator/remotecontrolguard.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QInputDialog>

BreakpointEditor::BreakpointEditor(std::shared_ptr<Emulator> emulator, Mode mode, QWidget* parent)
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

BreakpointEditor::BreakpointEditor(std::shared_ptr<Emulator> emulator, Mode mode, uint16_t breakpointId, QWidget* parent)
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
    // In BreakpointTypeEnum order: the index is the type
    _typeCombo->addItem("Memory");
    _typeCombo->addItem("Port");
    _typeCombo->addItem("Keyboard");
    _typeCombo->addItem("NextREG (ZX Next)");
    _typeCombo->addItem("Beam (ZX Next)");
    _typeCombo->addItem("Copper (ZX Next)");
    _typeCombo->addItem("Magic opcode (ZX Next)");
    
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(_typeCombo);
    mainLayout->addLayout(typeLayout);
    
    // Address input
    QHBoxLayout* addressLayout = new QHBoxLayout();
    _addressLabel = new QLabel("Address:");
    _addressEdit = new QLineEdit();
    _addressEdit->setPlaceholderText("Enter address (e.g., 0x1234, $1234, #1234, 4660)");
    
    // Create validator for hex and decimal addresses
    // A number, or (Keyboard) a key's name: space, enter, caps, symbol, a..z, 0..9, any
    QRegularExpression addressRegex("^(0x[0-9A-Fa-f]{1,4}|\\$[0-9A-Fa-f]{1,4}|#[0-9A-Fa-f]{1,4}|[0-9]{1,5}|[A-Za-z_]+)$");
    QValidator* addressValidator = new QRegularExpressionValidator(addressRegex, this);
    _addressEdit->setValidator(addressValidator);
    
    addressLayout->addWidget(_addressLabel);
    addressLayout->addWidget(_addressEdit);
    _endLabel = new QLabel("to:");
    _endEdit = new QLineEdit();
    _endEdit->setPlaceholderText("optional range end");
    _endEdit->setValidator(new QRegularExpressionValidator(addressRegex, this));
    addressLayout->addWidget(_endLabel);
    addressLayout->addWidget(_endEdit);
    mainLayout->addLayout(addressLayout);

    // Page: a physical breakpoint (that page, whatever slot shows it)
    _pageRow = new QWidget();
    QHBoxLayout* pageLayout = new QHBoxLayout(_pageRow);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    QLabel* pageLabel = new QLabel("Page:");
    _pageEdit = new QLineEdit();
    _pageEdit->setPlaceholderText("optional: ram5, rom1, cache0 - fires on that page in any slot");
    _slotOnlyCheck = new QCheckBox("This slot only");
    _slotOnlyCheck->setToolTip("Only through the slot of the address (#0000 / #4000 / #8000 / #C000)");
    pageLayout->addWidget(pageLabel);
    pageLayout->addWidget(_pageEdit);
    pageLayout->addWidget(_slotOnlyCheck);
    mainLayout->addWidget(_pageRow);
    
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

    // Keyboard: the key's code in the address field
    _keyboardBox = new QGroupBox("Key Event");
    QHBoxLayout* keyboardLayout = new QHBoxLayout();
    _keyPressCheck = new QCheckBox("Press");
    _keyReleaseCheck = new QCheckBox("Release");
    _keyPressCheck->setChecked(true);
    keyboardLayout->addWidget(_keyPressCheck);
    keyboardLayout->addWidget(_keyReleaseCheck);
    _keyboardBox->setLayout(keyboardLayout);
    mainLayout->addWidget(_keyboardBox);

    // NextREG: the register (or a range) in the address fields
    QRegularExpression byteRegex("^(0x[0-9A-Fa-f]{1,2}|\\$[0-9A-Fa-f]{1,2}|#[0-9A-Fa-f]{1,2}|[0-9]{1,3})$");
    _nextRegBox = new QGroupBox("NextREG Access");
    QGridLayout* nextRegLayout = new QGridLayout();
    _nrReadCheck = new QCheckBox("Read (IN #253B)");
    _nrWriteCheck = new QCheckBox("Write");
    _nrWriteCheck->setChecked(true);
    nextRegLayout->addWidget(_nrReadCheck, 0, 0);
    nextRegLayout->addWidget(_nrWriteCheck, 0, 1);
    nextRegLayout->addWidget(new QLabel("Through:"), 1, 0);
    _doorNextRegCheck = new QCheckBox("NEXTREG");
    _doorNextRegCheck->setToolTip("The NEXTREG n,v / NEXTREG n,A instructions");
    _doorPortCheck = new QCheckBox("Port");
    _doorPortCheck->setToolTip("OUT (#253B) after OUT (#243B); a read always comes through IN (#253B)");
    _doorCopperCheck = new QCheckBox("Copper");
    _doorCopperCheck->setToolTip("A copper MOVE");
    for (QCheckBox* door : {_doorNextRegCheck, _doorPortCheck, _doorCopperCheck})
        door->setChecked(true);
    nextRegLayout->addWidget(_doorNextRegCheck, 1, 1);
    nextRegLayout->addWidget(_doorPortCheck, 1, 2);
    nextRegLayout->addWidget(_doorCopperCheck, 1, 3);
    nextRegLayout->addWidget(new QLabel("Value:"), 2, 0);
    _nrValueEdit = new QLineEdit();
    _nrValueEdit->setPlaceholderText("any");
    _nrValueEdit->setToolTip("Stop only when this value is written / read");
    _nrValueEdit->setValidator(new QRegularExpressionValidator(byteRegex, this));
    nextRegLayout->addWidget(_nrValueEdit, 2, 1);
    nextRegLayout->addWidget(new QLabel("Mask:"), 2, 2);
    _nrValueMaskEdit = new QLineEdit();
    _nrValueMaskEdit->setPlaceholderText("0xFF");
    _nrValueMaskEdit->setToolTip("Compare only these bits of the value");
    _nrValueMaskEdit->setValidator(new QRegularExpressionValidator(byteRegex, this));
    nextRegLayout->addWidget(_nrValueMaskEdit, 2, 3);
    _nextRegBox->setLayout(nextRegLayout);
    mainLayout->addWidget(_nextRegBox);

    // Beam: the line (the copper's counter) in the address field, the pixel clock here
    _beamBox = new QGroupBox("Beam Position");
    QHBoxLayout* beamLayout = new QHBoxLayout();
    beamLayout->addWidget(new QLabel("Pixel clock (hc):"));
    _hcEdit = new QLineEdit();
    _hcEdit->setPlaceholderText("0 (0..511)");
    _hcEdit->setValidator(new QRegularExpressionValidator(addressRegex, this));
    beamLayout->addWidget(_hcEdit);
    _beamBox->setLayout(beamLayout);
    mainLayout->addWidget(_beamBox);

    // Magic: the opcode is the breakpoint, no address
    _magicBox = new QGroupBox("Magic Opcode");
    QHBoxLayout* magicLayout = new QHBoxLayout();
    _magicCombo = new QComboBox();
    _magicCombo->addItem("ED FF (jnext, ZEsarUX)", static_cast<int>(BRK_MAGIC_ED_FF));
    _magicCombo->addItem("DD 01 (CSpect)", static_cast<int>(BRK_MAGIC_DD_01));
    _magicCombo->setToolTip("Stops after the CPU ran the opcode (a NOP on the hardware)");
    magicLayout->addWidget(_magicCombo);
    _magicBox->setLayout(magicLayout);
    mainLayout->addWidget(_magicBox);
    
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
    for (QCheckBox* check : {_readCheck, _writeCheck, _executeCheck, _inCheck, _outCheck, _keyPressCheck, _keyReleaseCheck,
                             _nrReadCheck, _nrWriteCheck, _doorNextRegCheck, _doorPortCheck, _doorCopperCheck})
        connect(check, &QCheckBox::toggled, this, &BreakpointEditor::validateInput);
    connect(_cancelButton, &QPushButton::clicked, 
            this, &QDialog::reject);
    
    // Set initial size
    resize(460, 380);
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
        
    const BreakpointSnapshot bp = bpManager->GetBreakpointById(breakpointId);
    if (!bp)
        return;

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
    else if (bp->type == BRK_KEYBOARD)
    {
        _addressEdit->setText(bp->z80address == BRK_KEY_ANY
                                  ? QString("any")
                                  : QString::fromStdString(DebugKeyboardManager::GetKeyDisplayName(
                                                               static_cast<ZXKeysEnum>(bp->z80address)))
                                        .toLower());
        _keyPressCheck->setChecked(bp->keyType & BRK_KEY_PRESS);
        _keyReleaseCheck->setChecked(bp->keyType & BRK_KEY_RELEASE);
    }
    else if (bp->type == BRK_NEXTREG)
    {
        _nrReadCheck->setChecked(bp->ioType & BRK_IO_IN);
        _nrWriteCheck->setChecked(bp->ioType & BRK_IO_OUT);
        _doorNextRegCheck->setChecked(bp->nrDoors & BRK_NR_DOOR_NEXTREG);
        _doorPortCheck->setChecked(bp->nrDoors & BRK_NR_DOOR_PORT);
        _doorCopperCheck->setChecked(bp->nrDoors & BRK_NR_DOOR_COPPER);
        _nrValueEdit->setText(bp->nrMask != 0 ? QString("0x%1").arg(bp->nrValue, 2, 16, QChar('0')).toUpper() : QString());
        _nrValueMaskEdit->setText(bp->nrMask != 0 && bp->nrMask != 0xFF
                                      ? QString("0x%1").arg(bp->nrMask, 2, 16, QChar('0')).toUpper()
                                      : QString());
        _addressEdit->setText(QString("0x%1").arg(bp->z80address, 2, 16, QChar('0')).toUpper());
        _endEdit->setText(bp->isRange ? QString("0x%1").arg(bp->z80addressEnd, 2, 16, QChar('0')).toUpper() : QString());
    }
    else if (bp->type == BRK_BEAM)
    {
        _addressEdit->setText(QString::number(bp->z80address));
        _hcEdit->setText(QString::number(bp->beamHc));
    }
    else if (bp->type == BRK_COPPER)
    {
        _addressEdit->setText(QString::number(bp->z80address));
        _endEdit->setText(bp->isRange ? QString::number(bp->z80addressEnd) : QString());
    }
    else if (bp->type == BRK_MAGIC)
    {
        const int index = _magicCombo->findData(static_cast<int>(bp->z80address));
        _magicCombo->setCurrentIndex(index >= 0 ? index : 0);
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
    // Only the fields of the selected kind show
    const BreakpointTypeEnum type = static_cast<BreakpointTypeEnum>(index);
    _memoryAccessBox->setVisible(type == BRK_MEMORY);
    _portAccessBox->setVisible(type == BRK_IO);
    _keyboardBox->setVisible(type == BRK_KEYBOARD);
    _nextRegBox->setVisible(type == BRK_NEXTREG);
    _beamBox->setVisible(type == BRK_BEAM);
    _magicBox->setVisible(type == BRK_MAGIC);
    _pageRow->setVisible(type == BRK_MEMORY);
    const bool ranged = type == BRK_MEMORY || type == BRK_NEXTREG || type == BRK_COPPER;
    _endLabel->setVisible(ranged);
    _endEdit->setVisible(ranged);
    _addressLabel->setVisible(type != BRK_MAGIC);
    _addressEdit->setVisible(type != BRK_MAGIC);
    switch (type)
    {
        case BRK_IO:
            _addressLabel->setText("Port:");
            _addressEdit->setPlaceholderText("e.g. 0xFE, 0x7FFD; with a mask every port that matches");
            break;
        case BRK_KEYBOARD:
            _addressLabel->setText("Key:");
            _addressEdit->setPlaceholderText("a key: space, enter, caps, symbol, a..z, 0..9, or any");
            break;
        case BRK_NEXTREG:
            _addressLabel->setText("Register:");
            _addressEdit->setPlaceholderText("0x00..0xFF, e.g. 0x07 (CPU speed)");
            break;
        case BRK_BEAM:
            _addressLabel->setText("Line:");
            _addressEdit->setPlaceholderText("the copper's line counter, 0..511");
            break;
        case BRK_COPPER:
            _addressLabel->setText("List index:");
            _addressEdit->setPlaceholderText("the copper instruction, 0..1023");
            break;
        default:
            _addressLabel->setText("Address:");
            _addressEdit->setPlaceholderText("Enter address (e.g., 0x1234, $1234, #1234, 4660)");
            break;
    }

    validateInput();
}

void BreakpointEditor::validateInput()
{
    bool isValid = true;
    QString errorMessage;
    const BreakpointTypeEnum type = static_cast<BreakpointTypeEnum>(_typeCombo->currentIndex());

    // The magic opcode is the breakpoint: no address
    uint16_t address;
    if (type != BRK_MAGIC && !(type == BRK_KEYBOARD ? keyAddress(_addressEdit->text(), address)
                                                    : validateAddress(_addressEdit->text(), address)))
    {
        isValid = false;
        errorMessage = "Invalid address format. Use 0xNNNN, $NNNN, #NNNN, or decimal.";
    }
    else if (type == BRK_MEMORY && !(_readCheck->isChecked() || _writeCheck->isChecked() || _executeCheck->isChecked()))
    {
        isValid = false;
        errorMessage = "At least one memory access type must be selected.";
    }
    else if (type == BRK_IO && !(_inCheck->isChecked() || _outCheck->isChecked()))
    {
        isValid = false;
        errorMessage = "At least one port access type must be selected.";
    }
    else if (type == BRK_KEYBOARD && !(_keyPressCheck->isChecked() || _keyReleaseCheck->isChecked()))
    {
        isValid = false;
        errorMessage = "Press, release or both must be selected.";
    }
    else if (type == BRK_NEXTREG && !(_nrReadCheck->isChecked() || _nrWriteCheck->isChecked()))
    {
        isValid = false;
        errorMessage = "Read, write or both must be selected.";
    }
    else if (type == BRK_NEXTREG &&
             !(_doorNextRegCheck->isChecked() || _doorPortCheck->isChecked() || _doorCopperCheck->isChecked()))
    {
        isValid = false;
        errorMessage = "At least one door (NEXTREG, port, copper) must be selected.";
    }

    // Update validation label and OK button state
    _validationLabel->setText(errorMessage);
    _okButton->setEnabled(isValid);
}

uint8_t BreakpointEditor::selectedAccess(BreakpointTypeEnum type) const
{
    uint8_t access = 0;
    switch (type)
    {
        case BRK_MEMORY:
            if (_readCheck->isChecked()) access |= BRK_MEM_READ;
            if (_writeCheck->isChecked()) access |= BRK_MEM_WRITE;
            if (_executeCheck->isChecked()) access |= BRK_MEM_EXECUTE;
            break;
        case BRK_IO:
            if (_inCheck->isChecked()) access |= BRK_IO_IN;
            if (_outCheck->isChecked()) access |= BRK_IO_OUT;
            break;
        case BRK_KEYBOARD:
            if (_keyPressCheck->isChecked()) access |= BRK_KEY_PRESS;
            if (_keyReleaseCheck->isChecked()) access |= BRK_KEY_RELEASE;
            break;
        case BRK_NEXTREG:
            if (_nrReadCheck->isChecked()) access |= BRK_IO_IN;
            if (_nrWriteCheck->isChecked()) access |= BRK_IO_OUT;
            break;
        default:
            access = BRK_IO_OUT;  // beam, copper, magic: as the other surfaces add them
            break;
    }
    return access;
}

void BreakpointEditor::onAddressChanged(const QString& text)
{
    validateInput();
}

bool BreakpointEditor::keyAddress(const QString& text, uint16_t& address)
{
    // A keyboard breakpoint's key by name (the core's names), or any key
    if (text.compare("any", Qt::CaseInsensitive) == 0)
    {
        address = BRK_KEY_ANY;
        return true;
    }
    const ZXKeysEnum key = DebugKeyboardManager::ResolveKeyName(text.toStdString());
    if (key == ZXKEY_NONE)
        return false;
    address = static_cast<uint16_t>(key);
    return true;
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
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return;
    // Validate input
    validateInput();
    if (!_okButton->isEnabled())
        return;
        
    const BreakpointTypeEnum type = static_cast<BreakpointTypeEnum>(_typeCombo->currentIndex());

    // Parse address (the magic opcode is its own address)
    uint16_t address = 0;
    if (type == BRK_MAGIC)
        address = static_cast<uint16_t>(_magicCombo->currentData().toInt());
    else if (!(type == BRK_KEYBOARD ? keyAddress(_addressEdit->text(), address)
                                    : validateAddress(_addressEdit->text(), address)))
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
    _descriptor.type = type;
    _descriptor.z80address = address;
    _descriptor.active = _activeCheck->isChecked();
    _descriptor.note = _noteEdit->text().toStdString();
    _descriptor.group = groupName.toStdString();
    const uint8_t access = selectedAccess(type);
    if (type == BRK_MEMORY)
        _descriptor.memoryType = access;
    else if (type == BRK_KEYBOARD)
        _descriptor.keyType = access;
    else
        _descriptor.ioType = access;

    // Everything the manager validates (range, page, mask, doors, value, position, hit policy) goes through one spec;
    // the manager refuses a kind it does not take (keyboard) with the reason
    BreakpointSpec spec;
    spec.type = type;
    spec.access = access;
    spec.address = address;
    spec.note = _descriptor.note;
    spec.group = _descriptor.group;
    std::string error;
    uint16_t value = 0;
    const bool ranged = type == BRK_MEMORY || type == BRK_NEXTREG || type == BRK_COPPER;
    if (ranged && !_endEdit->text().trimmed().isEmpty())
    {
        if (!validateAddress(_endEdit->text(), value))
        {
            QMessageBox::warning(this, "Error", "The range end is not an address.");
            return;
        }
        spec.hasEnd = true;
        spec.addressEnd = value;
    }
    if (!_pageEdit->text().trimmed().isEmpty() && type == BRK_MEMORY)
    {
        if (!BreakpointManager::ParsePageInto(_pageEdit->text().trimmed().toStdString(), spec, error))
        {
            QMessageBox::warning(this, "Error", QString::fromStdString(error));
            return;
        }
        spec.slotOnly = _slotOnlyCheck->isChecked();
    }
    if (!_maskEdit->text().trimmed().isEmpty() && type == BRK_IO)
    {
        if (!validateAddress(_maskEdit->text(), value))
        {
            QMessageBox::warning(this, "Error", "The mask is not a 16-bit value.");
            return;
        }
        spec.portMask = value;
    }
    if (type == BRK_NEXTREG)
    {
        spec.nrDoors = 0;
        if (_doorNextRegCheck->isChecked()) spec.nrDoors |= BRK_NR_DOOR_NEXTREG;
        if (_doorPortCheck->isChecked()) spec.nrDoors |= BRK_NR_DOOR_PORT;
        if (_doorCopperCheck->isChecked()) spec.nrDoors |= BRK_NR_DOOR_COPPER;
        // A read comes through the port only: the other doors do not apply to it
        if (spec.access == BRK_IO_IN)
            spec.nrDoors = BRK_NR_DOOR_PORT;
        if (!_nrValueEdit->text().trimmed().isEmpty())
        {
            if (!validateAddress(_nrValueEdit->text(), value) || value > 0xFF)
            {
                QMessageBox::warning(this, "Error", "The value is not a byte (0..255).");
                return;
            }
            spec.nrValue = static_cast<uint8_t>(value);
            spec.nrMask = 0xFF;
        }
        if (!_nrValueMaskEdit->text().trimmed().isEmpty())
        {
            if (!validateAddress(_nrValueMaskEdit->text(), value) || value > 0xFF)
            {
                QMessageBox::warning(this, "Error", "The value mask is not a byte (0..255).");
                return;
            }
            spec.nrMask = static_cast<uint8_t>(value);
        }
    }
    if (type == BRK_BEAM && !_hcEdit->text().trimmed().isEmpty())
    {
        if (!validateAddress(_hcEdit->text(), value) || value > BRK_BEAM_MAX_HC)
        {
            QMessageBox::warning(this, "Error", "The pixel clock is 0..511.");
            return;
        }
        spec.beamHc = value;
    }
    if (!BreakpointManager::ParseHitSpec(_hitsEdit->text().trimmed().toStdString(), spec.hitMode, spec.hitTarget, error))
    {
        QMessageBox::warning(this, "Error", QString::fromStdString(error));
        return;
    }

    // An edit replaces the breakpoint (a new id) once the new one is in: a refused edit keeps the old one. The same
    // plain breakpoint comes back with the old id (its note and group updated): nothing to remove then
    const uint16_t newId = bpManager->AddBreakpoint(spec, error);
    if (newId == BRK_INVALID)
    {
        QMessageBox::warning(this, "Error", QString::fromStdString(error));
        return;
    }
    if (_mode != Add && newId != _breakpointId)
        bpManager->RemoveBreakpointByID(_breakpointId);
    if (!_descriptor.active)
        bpManager->DeactivateBreakpoint(newId);
    else
        bpManager->ActivateBreakpoint(newId);
    accept();
}

BreakpointDescriptor BreakpointEditor::getBreakpointDescriptor() const
{
    return _descriptor;
}
